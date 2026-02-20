#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>

#include "components/RawViewport.h"
#include "managers/AppStateManager.h"
#include "managers/FileScanner.h"
#include "managers/PresetManager.h"
#include "managers/LogManager.h"
#include "managers/ThumbnailImageProvider.h"
#include "managers/ThumbnailProvider.h"

#include <QSettings>
#include <vector>
#include <vulkan/vulkan.h>
#include <dlfcn.h>

// Function pointer types
typedef VkResult (*PFN_vkCreateInstance_t)(const VkInstanceCreateInfo*, const VkAllocationCallbacks*, VkInstance*);
typedef VkResult (*PFN_vkEnumeratePhysicalDevices_t)(VkInstance, uint32_t*, VkPhysicalDevice*);
typedef void (*PFN_vkGetPhysicalDeviceProperties_t)(VkPhysicalDevice, VkPhysicalDeviceProperties*);
typedef void (*PFN_vkDestroyInstance_t)(VkInstance, const VkAllocationCallbacks*);

int main(int argc, char* argv[]) {
  QCoreApplication::setOrganizationName("Photon");
  QCoreApplication::setApplicationName("Photon");

  // Read preferred GPU from settings
  {
      QSettings settings(QSettings::IniFormat, QSettings::UserScope, "Photon", "Photon");
      QString preferredGpu = settings.value("performance/preferredGpu", "Auto").toString();

      if (preferredGpu != "Auto") {
          void* libvulkan = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
          if (libvulkan) {
              auto vkCreateInstance_ptr = (PFN_vkCreateInstance_t)dlsym(libvulkan, "vkCreateInstance");
              auto vkEnumeratePhysicalDevices_ptr = (PFN_vkEnumeratePhysicalDevices_t)dlsym(libvulkan, "vkEnumeratePhysicalDevices");
              auto vkGetPhysicalDeviceProperties_ptr = (PFN_vkGetPhysicalDeviceProperties_t)dlsym(libvulkan, "vkGetPhysicalDeviceProperties");
              auto vkDestroyInstance_ptr = (PFN_vkDestroyInstance_t)dlsym(libvulkan, "vkDestroyInstance");

              if (vkCreateInstance_ptr && vkEnumeratePhysicalDevices_ptr && vkGetPhysicalDeviceProperties_ptr && vkDestroyInstance_ptr) {
                  VkInstance instance = VK_NULL_HANDLE;
                  VkInstanceCreateInfo createInfo = {};
                  createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
                  
                  if (vkCreateInstance_ptr(&createInfo, nullptr, &instance) == VK_SUCCESS) {
                      uint32_t deviceCount = 0;
                      vkEnumeratePhysicalDevices_ptr(instance, &deviceCount, nullptr);
                      if (deviceCount > 0) {
                          std::vector<VkPhysicalDevice> devices(deviceCount);
                          vkEnumeratePhysicalDevices_ptr(instance, &deviceCount, devices.data());
                          for (uint32_t i = 0; i < deviceCount; ++i) {
                              VkPhysicalDeviceProperties props;
                              vkGetPhysicalDeviceProperties_ptr(devices[i], &props);
                              if (preferredGpu == QString::fromUtf8(props.deviceName)) {
                                  qputenv("QT_VULKAN_DEVICE_INDEX", QByteArray::number(i));
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

  QQuickWindow::setGraphicsApi(QSGRendererInterface::VulkanRhi);

  QQmlApplicationEngine engine;

  // Create managers
  ThumbnailProvider* thumbProvider = new ThumbnailProvider(&app);
  PresetManager* presetManager = new PresetManager(&app);

  // Register image provider
  engine.addImageProvider("thumbnail",
                          new ThumbnailImageProvider(thumbProvider));

  // Register AppStateManager singleton
  qmlRegisterSingletonType<AppStateManager>(
      "Main", 1, 0, "AppState", &AppStateManager::createQmlInstance);

  qmlRegisterSingletonInstance("Main", 1, 0, "Logger", photon::LogManager::instance());

  // Register PresetManager singleton
  qmlRegisterSingletonInstance("Main", 1, 0, "PresetManager", presetManager);

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
      [url](QObject* obj, const QUrl& objUrl) {
        if (!obj && url == objUrl) QCoreApplication::exit(-1);
      },
      Qt::QueuedConnection);

  engine.load(url);

  return app.exec();
}
