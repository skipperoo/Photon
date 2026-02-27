#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

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
#include "managers/PreviewManager.h"
#include "managers/ThumbnailImageProvider.h"
#include "managers/ThumbnailProvider.h"

using namespace photon;

// Function pointer types for raw Vulkan discovery
typedef VkResult (VKAPI_PTR *PFN_vkCreateInstance_t)(const VkInstanceCreateInfo*,
                                           const VkAllocationCallbacks*,
                                           VkInstance*);
typedef VkResult (VKAPI_PTR *PFN_vkEnumeratePhysicalDevices_t)(VkInstance, uint32_t*,
                                                     VkPhysicalDevice*);
typedef void (VKAPI_PTR *PFN_vkGetPhysicalDeviceProperties_t)(
    VkPhysicalDevice, VkPhysicalDeviceProperties*);
typedef void (VKAPI_PTR *PFN_vkDestroyInstance_t)(VkInstance,
                                        const VkAllocationCallbacks*);

int main(int argc, char* argv[]) {
  // Enable RHI info and Vulkan logging
  qputenv("QSG_INFO", "1");
  qputenv("QSG_RHI_DEBUG", "1");
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
#ifdef _WIN32
      HMODULE libvulkan = LoadLibraryA("vulkan-1.dll");
      if (libvulkan) {
        auto vkCreateInstance_ptr =
            (PFN_vkCreateInstance_t)GetProcAddress(libvulkan, "vkCreateInstance");
        auto vkEnumeratePhysicalDevices_ptr =
            (PFN_vkEnumeratePhysicalDevices_t)GetProcAddress(
                libvulkan, "vkEnumeratePhysicalDevices");
        auto vkGetPhysicalDeviceProperties_ptr =
            (PFN_vkGetPhysicalDeviceProperties_t)GetProcAddress(
                libvulkan, "vkGetPhysicalDeviceProperties");
        auto vkDestroyInstance_ptr =
            (PFN_vkDestroyInstance_t)GetProcAddress(libvulkan, "vkDestroyInstance");
#else
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
#endif

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
                  qputenv("QSG_RHI_DEVICE_INDEX", idx);
                  qputenv("QT_VULKAN_DEVICE_INDEX", idx);

                  QByteArray deviceSelect =
                      QByteArray::number(props.vendorID, 16) + ":" +
                      QByteArray::number(props.deviceID, 16);
                  qputenv("MESA_VK_DEVICE_SELECT", deviceSelect);

                  if (deviceName.contains("NVIDIA", Qt::CaseInsensitive)) {
                    qputenv("__NV_PRIME_RENDER_OFFLOAD", "1");
                    qputenv("__GLX_VENDOR_LIBRARY_NAME", "nvidia");
                    qputenv("__VK_LAYER_NV_optimus", "NVIDIA_only");
                    qputenv("QSG_RHI_PREFER_HIGH_PERFORMANCE_GPU", "1");
                  }
                  break;
                }
              }
            }
            vkDestroyInstance_ptr(instance, nullptr);
          }
        }
#ifdef _WIN32
        FreeLibrary(libvulkan);
#else
        dlclose(libvulkan);
#endif
      }
    }
  }

  QGuiApplication app(argc, argv);

  QVulkanInstance vulkanInstance;
  vulkanInstance.setLayers({});
  if (!vulkanInstance.create()) {
    qWarning("Failed to create Vulkan instance");
  }

  QQuickWindow::setGraphicsApi(QSGRendererInterface::VulkanRhi);

  QQmlApplicationEngine engine;

  ThumbnailProvider* thumbProvider = new ThumbnailProvider(&app);
  PresetManager* presetManager = new PresetManager(&app);
  photon::PreviewManager* previewManager = new photon::PreviewManager(&app);
  photon::ExportManager* exportManager = new photon::ExportManager(&app);

  auto* appState = AppStateManager::instance();
  appState->setParent(&app);
  auto* logManager = photon::LogManager::instance();
  logManager->setParent(&app);

  engine.addImageProvider("thumbnail",
                          new ThumbnailImageProvider(thumbProvider));

  qmlRegisterSingletonType<AppStateManager>(
      "Main", 1, 0, "AppState", &AppStateManager::createQmlInstance);

  qmlRegisterSingletonInstance("Main", 1, 0, "Logger", logManager);
  qmlRegisterSingletonInstance("Main", 1, 0, "PresetManager", presetManager);
  qmlRegisterSingletonInstance("Main", 1, 0, "PreviewManager", previewManager);
  qmlRegisterSingletonInstance("Main", 1, 0, "ExportManager", exportManager);
  qmlRegisterType<RawViewport>("Main", 1, 0, "RawViewport");
  engine.rootContext()->setContextProperty("thumbnailProvider", thumbProvider);
  qmlRegisterType<FileScanner>("Main", 1, 0, "FileScanner");

  const QUrl url(QStringLiteral("qrc:/Main/content/views/App.qml"));

  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreated, &app,
      [url, &vulkanInstance, previewManager, exportManager](QObject* obj,
                                                            const QUrl& objUrl) {
        if (!obj && url == objUrl) QCoreApplication::exit(-1);

        QQuickWindow* window = qobject_cast<QQuickWindow*>(obj);
        if (window) {
          window->setVulkanInstance(&vulkanInstance);
          QObject::connect(window, &QQuickWindow::sceneGraphInitialized,
                           [window, previewManager, exportManager]() {
                             if (previewManager) {
                               previewManager->setRhi(window->rhi());
                               previewManager->setWindow(window);
                             }
                             if (exportManager) {
                               exportManager->setRhi(window->rhi());
                               exportManager->setWindow(window);
                             }
                           });
        }
      },
      Qt::QueuedConnection);

  engine.load(url);

  return app.exec();
}
