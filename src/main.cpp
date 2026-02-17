#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>

#include "components/RawViewport.h"
#include "managers/AppStateManager.h"

int main(int argc, char* argv[]) {
  QGuiApplication app(argc, argv);

  QQuickWindow::setGraphicsApi(QSGRendererInterface::VulkanRhi);

  QQmlApplicationEngine engine;

  // Register AppStateManager singleton
  qmlRegisterSingletonType<AppStateManager>(
      "Main", 1, 0, "AppState", &AppStateManager::createQmlInstance);

  // Register RawViewport component
  qmlRegisterType<RawViewport>("Main", 1, 0, "RawViewport");

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
