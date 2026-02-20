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

int main(int argc, char* argv[]) {
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
