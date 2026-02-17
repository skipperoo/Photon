#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>

#include "managers/AppStateManager.h"

int main(int argc, char* argv[]) {
  QGuiApplication app(argc, argv);

  QQuickWindow::setGraphicsApi(QSGRendererInterface::VulkanRhi);

  QQmlApplicationEngine engine;

  // Register AppStateManager singleton
  qmlRegisterSingletonType<AppStateManager>(
      "Main", 1, 0, "AppState", &AppStateManager::createQmlInstance);

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
