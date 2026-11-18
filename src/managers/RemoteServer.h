#pragma once

#include <QByteArray>
#include <QCache>
#include <QDateTime>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QSslCertificate>
#include <QSslKey>
#include <QStringList>
#include <QThreadPool>
#include <QTimer>

class QJsonObject;
class QPointingDevice;
class QUrlQuery;
class QSslSocket;
class QTcpServer;
class QWebSocket;
class QWebSocketServer;
class RemoteSession;

// Remote mode: serves Photon's UI to a browser over HTTPS on one port.
// - GET /: the client page (a canvas that shows the streamed UI and sends
//   input back); GET /photon-remote-ca.crt: the certificate authority to
//   install on the device; plain HTTP is redirected to HTTPS.
// - GET /exports/{list,thumb,file}?t=<session token>: the open folder's
//   Export folder, read-only (files directly inside it, image and video
//   types only), for the page's Exports view; /exports/zip?name=…&name=…
//   downloads several as one .zip.
// - A WebSocket on the same port: login (password, then a session token for
//   reconnects), frames (JPEG, newest wins: the next frame goes out once the
//   client has drawn the previous one), input (pointer, wheel, keys, resize).
// One client at a time (a new login takes over). The UI runs in
// RemoteSession while a client is connected and for a grace period after it
// drops, so a device waking from sleep resumes where it was.
class RemoteServer : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY stateChanged)
  Q_PROPERTY(bool running READ running NOTIFY stateChanged)
  Q_PROPERTY(int port READ port WRITE setPort NOTIFY stateChanged)
  Q_PROPERTY(bool hasPassword READ hasPassword NOTIFY stateChanged)
  Q_PROPERTY(QStringList addresses READ addresses NOTIFY stateChanged)
  Q_PROPERTY(bool clientConnected READ clientConnected NOTIFY clientChanged)
  Q_PROPERTY(QString clientAddress READ clientAddress NOTIFY clientChanged)
  Q_PROPERTY(QString error READ error NOTIFY stateChanged)

 public:
  RemoteServer(RemoteSession* session, const QString& certificateDir,
               QObject* parent = nullptr);
  ~RemoteServer() override;

  bool enabled() const { return m_enabled; }
  void setEnabled(bool on);
  bool running() const { return m_server != nullptr; }
  int port() const { return m_port; }
  void setPort(int port);
  bool hasPassword() const { return !m_passwordHash.isEmpty(); }
  // https://<address>:<port> for each local address.
  QStringList addresses() const;
  bool clientConnected() const { return m_client != nullptr; }
  QString clientAddress() const;
  QString error() const { return m_error; }

  // At least kMinPasswordLength characters; signs out every device.
  Q_INVOKABLE bool setPassword(const QString& password);
  // Disconnects the client and gives the UI back to the desktop.
  Q_INVOKABLE void endSession();

  static constexpr int kDefaultPort = 8443;
  static constexpr int kMinPasswordLength = 8;
  static constexpr int kPbkdf2Iterations = 210000;

 signals:
  void stateChanged();
  void clientChanged();

 private:
  void start();
  void stop();
  void onNewConnection();
  void onEncryptedRequest(QSslSocket* socket);
  void onWebSocket();
  void onMessage(const QString& message);
  void onBinaryMessage(const QByteArray& message);
  void handleHello(QWebSocket* socket, const QJsonObject& msg);
  void handleInput(const QJsonObject& msg);
  void dropClient(const QString& reason);
  void onFrame(const QImage& frame, qint64 renderMs);
  void encodeNext();
  void sendHttp(QSslSocket* socket, int status, const QByteArray& type,
                const QByteArray& body, const QByteArray& extraHeaders = {});
  void serveExports(QSslSocket* socket, const QByteArray& path,
                    const QUrlQuery& query, const QByteArray& range);
  // downloadName: the name the browser saves it as ("" = the file's name).
  void sendFile(QSslSocket* socket, const QString& path,
                const QByteArray& range, bool download,
                const QString& downloadName = QString());
  bool validToken(const QByteArray& token) const;
  // <open folder>/Export, or "" when no folder is open.
  QString exportDir() const;
  // The path of an exported file by name, or "" when it is not one.
  QString exportFile(const QString& name) const;
  bool throttled(const QString& address) const;
  void noteFailure(const QString& address);
  void loadSettings();
  void saveSettings() const;

  RemoteSession* m_session = nullptr;
  QString m_certificateDir;
  bool m_enabled = false;
  int m_port = kDefaultPort;
  QByteArray m_passwordHash, m_passwordSalt;
  QString m_error;

  QTcpServer* m_server = nullptr;
  QWebSocketServer* m_ws = nullptr;
  QList<QSslCertificate> m_certificates;
  QSslKey m_key;
  QByteArray m_caPem;
  // The client page, with its version (a hash of its content) filled in; a
  // page that reconnects with another version reloads itself (home-screen
  // web apps keep running an old page across Photon updates).
  QByteArray m_page;
  QString m_pageVersion;
  // The home-screen icon (iOS): 180×180, opaque (iOS masks the corners).
  QByteArray m_touchIcon;

  QPointer<QWebSocket> m_client;
  QHash<QByteArray, QDateTime> m_tokens;  // session token -> expiry
  QHash<QString, QList<QDateTime>> m_failures;
  QTimer m_graceTimer;

  // Frames: the newest rendered frame waits until the client acknowledged
  // the previous one and the encoder is free.
  QImage m_pendingFrame;
  qint64 m_pendingRenderMs = 0;
  bool m_awaitingAck = false;
  bool m_encoding = false;
  QThreadPool m_encoder;
  // Exports view thumbnails (JPEG), by path + modification time.
  QCache<QString, QByteArray> m_thumbnails{64 * 1024 * 1024};
  QThreadPool m_thumbnailer;

  // Remote pointer input arrives from this device (a mouse), not from the
  // computer's default pointing device (often a touchpad, which Qt Quick
  // does not drag-flick with).
  QPointingDevice* m_mouse = nullptr;

  qint64 m_lastInputSeq = 0;
  qint64 m_pendingSeq = 0;
  Qt::MouseButtons m_buttons;
};
