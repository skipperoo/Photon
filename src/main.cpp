#include <dlfcn.h>
#include <vulkan/vulkan.h>

#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSettings>
#include <QVulkanFunctions>
#include <QVulkanInstance>
#include <vector>

#include "components/RawViewport.h"
#include "managers/AppStateManager.h"
#include "managers/ExportManager.h"
#include "managers/FileScanner.h"
#include "managers/LogManager.h"
#include "managers/PresetManager.h"
#include "managers/ThumbnailImageProvider.h"
#include "managers/ThumbnailProvider.h"

// Function pointer types for raw Vulkan discovery
typedef VkResult (*PFN_vkCreateInstance_t)(const VkInstanceCreateInfo*,
                                           const VkAllocationCallbacks*,
                                           VkInstance*);
typedef VkResult (*PFN_vkEnumeratePhysicalDevices_t)(VkInstance, uint32_t*,
                                                     VkPhysicalDevice*);
typedef void (*PFN_vkGetPhysicalDeviceProperties_t)(
    VkPhysicalDevice, VkPhysicalDeviceProperties*);
typedef void (*PFN_vkDestroyInstance_t)(VkInstance,
                                        const VkAllocationCallbacks*);

int main(int argc, char* argv[]) {
  // Enable RHI info and Vulkan logging
  qputenv("QSG_INFO", "1");
  qputenv("QSG_RHI_DEBUG", "1");
  qputenv("QT_LOGGING_RULES", "qt.vulkan=true");
  qputenv("QSG_RHI_BACKEND", "vulkan");

  // Set basic app info early for QSettings
  QCoreApplication::setOrganizationName("Photon");
  QCoreApplication::setApplicationName("Photon");

  // --- Aggressive GPU Selection ---
  {
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "Photon",
                       "Photon");
    QString preferredGpu =
        settings.value("performance/preferredGpu", "Auto").toString();

    if (preferredGpu != "Auto") {
      void* libvulkan = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
      if (libvulkan) {
        auto vkCreateInstance_ptr =
            (PFN_vkCreateInstance_t)dlsym(libvulkan, "vkCreateInstance");
        auto vkEnumeratePhysicalDevices_ptr =
            (PFN_vkEnumeratePhysicalDevices_t)dlsym(
                libvulkan, "vkEnumeratePhysicalDevices");
        auto vkGetPhysicalDeviceProperties_ptr =
            (PFN_vkGetPhysicalDeviceProperties_t)dlsym(
                libvulkan, "vkGetPhysicalDeviceProperties");
        auto vkDestroyInstance_ptr =
            (PFN_vkDestroyInstance_t)dlsym(libvulkan, "vkDestroyInstance");

        if (vkCreateInstance_ptr && vkEnumeratePhysicalDevices_ptr &&
            vkGetPhysicalDeviceProperties_ptr && vkDestroyInstance_ptr) {
          VkInstance instance = VK_NULL_HANDLE;
          VkInstanceCreateInfo createInfo = {};
          createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;

          if (vkCreateInstance_ptr(&createInfo, nullptr, &instance) ==
              VK_SUCCESS) {
            uint32_t deviceCount = 0;
            vkEnumeratePhysicalDevices_ptr(instance, &deviceCount, nullptr);
            if (deviceCount > 0) {
              std::vector<VkPhysicalDevice> devices(deviceCount);
              vkEnumeratePhysicalDevices_ptr(instance, &deviceCount,
                                             devices.data());
              for (uint32_t i = 0; i < deviceCount; ++i) {
                VkPhysicalDeviceProperties props;
                vkGetPhysicalDeviceProperties_ptr(devices[i], &props);
                QString deviceName = QString::fromUtf8(props.deviceName);

                if (preferredGpu == deviceName) {
                  QByteArray idx = QByteArray::number(i);
                  // Force Qt RHI to use this index
                  qputenv("QSG_RHI_DEVICE_INDEX", idx);
                  qputenv("QT_VULKAN_DEVICE_INDEX", idx);

                  // Linux-specific: Force device selection layer
                  // (Mesa/AMD/Intel)
                  QByteArray deviceSelect =
                      QByteArray::number(props.vendorID, 16) + ":" +
                      QByteArray::number(props.deviceID, 16);
                  qputenv("MESA_VK_DEVICE_SELECT", deviceSelect);

                  // NVIDIA Prime / Optimus specific:
                  if (deviceName.contains("NVIDIA", Qt::CaseInsensitive)) {
                    qputenv("__NV_PRIME_RENDER_OFFLOAD", "1");
                    qputenv("__GLX_VENDOR_LIBRARY_NAME", "nvidia");
                    qputenv("__VK_LAYER_NV_optimus", "NVIDIA_only");
                    qputenv("QSG_RHI_PREFER_HIGH_PERFORMANCE_GPU", "1");
                  }

                  fprintf(stderr,
                          "Photon: Forcing GPU [%u] %s (Vendor: %04x, Device: "
                          "%04x)\n",
                          i, props.deviceName, props.vendorID, props.deviceID);
                  fprintf(stderr, "Photon: QSG_RHI_DEVICE_INDEX=%s\n",
                          qgetenv("QSG_RHI_DEVICE_INDEX").constData());
                  break;
                }
              }
            }
            vkDestroyInstance_ptr(instance, nullptr);
          }
        }
        dlclose(libvulkan);
      }
    }
  }

  QGuiApplication app(argc, argv);

  // Setup Vulkan Instance - Non-static local to ensure it's destroyed before
  // app returns
  QVulkanInstance vulkanInstance;
  vulkanInstance.setLayers({});
  if (!vulkanInstance.create()) {
    qWarning("Failed to create Vulkan instance");
  }

  // Use Vulkan by default for the RHI
  QQuickWindow::setGraphicsApi(QSGRendererInterface::VulkanRhi);

  QQmlApplicationEngine engine;

  // Create managers
  ThumbnailProvider* thumbProvider = new ThumbnailProvider(&app);
  PresetManager* presetManager = new PresetManager(&app);
  photon::ExportManager* exportManager = new photon::ExportManager(&app);

  // Register singletons early and set parents to ensure they are destroyed with
  // the app
  auto* appState = AppStateManager::instance();
  appState->setParent(&app);
  auto* logManager = photon::LogManager::instance();
  logManager->setParent(&app);

  // Register image provider
  engine.addImageProvider("thumbnail",
                          new ThumbnailImageProvider(thumbProvider));

  // Register AppStateManager singleton
  qmlRegisterSingletonType<AppStateManager>(
      "Main", 1, 0, "AppState", &AppStateManager::createQmlInstance);

  qmlRegisterSingletonInstance("Main", 1, 0, "Logger", logManager);

  // Register PresetManager singleton
  qmlRegisterSingletonInstance("Main", 1, 0, "PresetManager", presetManager);

  // Register ExportManager singleton
  qmlRegisterSingletonInstance("Main", 1, 0, "ExportManager", exportManager);

  // Register RawViewport component
  qmlRegisterType<RawViewport>("Main", 1, 0, "RawViewport");

  // Register ThumbnailProvider component (as an instance for QML to call
  // generateThumbnailAsync)
  engine.rootContext()->setContextProperty("thumbnailProvider", thumbProvider);

  // Register FileScanner component
  qmlRegisterType<FileScanner>("Main", 1, 0, "FileScanner");

  const QUrl url(QStringLiteral("qrc:/Main/content/views/App.qml"));

  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreated, &app,
      [url, &vulkanInstance](QObject* obj, const QUrl& objUrl) {
        if (!obj && url == objUrl) QCoreApplication::exit(-1);

        QQuickWindow* window = qobject_cast<QQuickWindow*>(obj);
        if (window) {
          window->setVulkanInstance(&vulkanInstance);
        }
      },
      Qt::QueuedConnection);

  engine.load(url);

  return app.exec();
}
