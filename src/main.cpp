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

#include <QSettings>
#include <vector>
#include <QVulkanInstance>
#include <QVulkanFunctions>

int main(int argc, char* argv[]) {
  // Enable RHI and Vulkan info logging
  qputenv("QSG_INFO", "1");
  qputenv("QT_LOGGING_RULES", "qt.vulkan=true");

  QCoreApplication::setOrganizationName("Photon");
  QCoreApplication::setApplicationName("Photon");

  QGuiApplication app(argc, argv);

  // Setup Vulkan Instance
  QVulkanInstance vulkanInstance;
  
  // Read preferred GPU from settings
  QSettings settings(QSettings::IniFormat, QSettings::UserScope, "Photon", "Photon");
  QString preferredGpu = settings.value("performance/preferredGpu", "Auto").toString();

  if (preferredGpu != "Auto") {
      // We need to create a temporary instance to enumerate devices if we want to be sure about the index
      // But QVulkanInstance::create() already does that.
      vulkanInstance.setLayers({});
      if (vulkanInstance.create()) {
          auto *f = vulkanInstance.functions();
          uint32_t deviceCount = 0;
          f->vkEnumeratePhysicalDevices(vulkanInstance.vkInstance(), &deviceCount, nullptr);
          if (deviceCount > 0) {
              std::vector<VkPhysicalDevice> devices(deviceCount);
              f->vkEnumeratePhysicalDevices(vulkanInstance.vkInstance(), &deviceCount, devices.data());
                  for (uint32_t i = 0; i < deviceCount; ++i) {
                      VkPhysicalDeviceProperties props;
                      f->vkGetPhysicalDeviceProperties(devices[i], &props);
                      QString deviceName = QString::fromUtf8(props.deviceName);
                      if (preferredGpu == deviceName) {
                          fprintf(stderr, "Photon: Explicitly selecting GPU: %s (ID: %04x:%04x)\n", 
                                  props.deviceName, props.vendorID, props.deviceID);
                          
                          QByteArray idx = QByteArray::number(i);
                          qputenv("QSG_RHI_DEVICE_INDEX", idx);
                          
                          // For Mesa-based systems (Intel/AMD), this is very reliable
                          QByteArray deviceSelect = QByteArray::number(props.vendorID, 16) + ":" + QByteArray::number(props.deviceID, 16);
                          qputenv("MESA_VK_DEVICE_SELECT", deviceSelect);
                          
                          // For NVIDIA, sometimes setting this helps if using the Optimus layer
                          qputenv("__NV_PRIME_RENDER_OFFLOAD", "1");
                          qputenv("__GLX_VENDOR_LIBRARY_NAME", "nvidia");
                          qputenv("__VK_LAYER_NV_optimus", "NVIDIA_only");
                          
                          break;
                      }
                  }
          }
      }
  }

  if (!vulkanInstance.isValid()) {
      vulkanInstance.create();
  }

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
      [url, &vulkanInstance](QObject* obj, const QUrl& objUrl) {
        if (!obj && url == objUrl) QCoreApplication::exit(-1);
        
        QQuickWindow *window = qobject_cast<QQuickWindow *>(obj);
        if (window) {
            window->setVulkanInstance(&vulkanInstance);
        }
      },
      Qt::QueuedConnection);

  engine.load(url);

  return app.exec();
}
