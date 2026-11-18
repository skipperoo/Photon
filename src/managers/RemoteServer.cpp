#include "RemoteServer.h"

#include <QtCore/private/qzipwriter_p.h>
#include <qpa/qwindowsysteminterface.h>

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHostAddress>
#include <QHostInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QPainter>
#include <QPointer>
#include <QPointingDevice>
#include <QQuickWindow>
#include <QSettings>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QTcpServer>
#include <QTemporaryFile>
#include <QUrl>
#include <QUrlQuery>
#include <QWebSocket>
#include <QWebSocketServer>
#include <QtConcurrent>
#include <QtEndian>
#include <algorithm>
#include <memory>

#include "AppStateManager.h"
#include "LogManager.h"
#include "RemoteCertificates.h"
#include "RemoteSession.h"

namespace {

void log(const QString& msg, photon::LogLevel level = photon::PHOTON_INFO) {
  photon::LogManager::instance()->log("[ RemoteServer.cpp ] - " + msg, level);
}

constexpr int kMaxHeaderBytes = 16 * 1024;
constexpr int kRequestTimeoutMs = 15000;
constexpr int kLoginTimeoutMs = 10000;
constexpr int kGraceMs = 60000;
constexpr int kTokenDays = 7;
constexpr int kMaxFailures = 5;
constexpr int kFailureWindowSecs = 600;
constexpr int kJpegQuality = 82;
constexpr qint64 kMaxClientMessage = 64 * 1024;
constexpr int kThumbnailSize = 480;
constexpr qint64 kFileChunk = 256 * 1024;
constexpr qint64 kMaxBuffered = 1024 * 1024;

// What the Exports view lists and serves (by extension).
const QHash<QString, QByteArray>& exportTypes() {
  static const QHash<QString, QByteArray> types = {
      {"jpg", "image/jpeg"},     {"jpeg", "image/jpeg"}, {"png", "image/png"},
      {"webp", "image/webp"},    {"tif", "image/tiff"},  {"tiff", "image/tiff"},
      {"avif", "image/avif"},    {"heic", "image/heic"}, {"mp4", "video/mp4"},
      {"mov", "video/quicktime"}};
  return types;
}

// Accepts plain TCP connections as QSslSocket, so each one can be served as
// HTTPS or, when it does not start with a TLS handshake, redirected.
class TlsServer : public QTcpServer {
 public:
  using QTcpServer::QTcpServer;

 protected:
  void incomingConnection(qintptr descriptor) override {
    auto* socket = new QSslSocket(this);
    if (socket->setSocketDescriptor(descriptor))
      addPendingConnection(socket);
    else
      delete socket;
  }
};

struct HttpRequest {
  QByteArray method, path;
  QHash<QByteArray, QByteArray> headers;  // lower-case names
  int headerBytes = 0;                    // 0 = incomplete
  bool valid = false;
};

HttpRequest parseRequest(const QByteArray& data) {
  HttpRequest r;
  const int end = data.indexOf("\r\n\r\n");
  if (end < 0) return r;
  r.headerBytes = end + 4;
  const QList<QByteArray> lines = data.left(end).split('\n');
  const QList<QByteArray> first = lines.value(0).trimmed().split(' ');
  if (first.size() < 3) return r;
  r.method = first.at(0);
  r.path = first.at(1);
  for (int i = 1; i < lines.size(); ++i) {
    const int colon = lines.at(i).indexOf(':');
    if (colon > 0)
      r.headers.insert(lines.at(i).left(colon).trimmed().toLower(),
                       lines.at(i).mid(colon + 1).trimmed());
  }
  r.valid = true;
  return r;
}

Qt::MouseButton jsButton(int button) {
  switch (button) {
    case 0:
      return Qt::LeftButton;
    case 1:
      return Qt::MiddleButton;
    case 2:
      return Qt::RightButton;
    default:
      return Qt::NoButton;
  }
}

Qt::MouseButtons jsButtons(int buttons) {
  Qt::MouseButtons out;
  if (buttons & 1) out |= Qt::LeftButton;
  if (buttons & 2) out |= Qt::RightButton;
  if (buttons & 4) out |= Qt::MiddleButton;
  return out;
}

// Bits: 1 Shift, 2 Ctrl, 4 Alt, 8 Meta. Cmd (Meta) on an iPad keyboard acts as
// Ctrl, so Cmd+Z undoes like Ctrl+Z on the desktop.
Qt::KeyboardModifiers jsModifiers(int mods) {
  Qt::KeyboardModifiers out;
  if (mods & 1) out |= Qt::ShiftModifier;
  if (mods & (2 | 8)) out |= Qt::ControlModifier;
  if (mods & 4) out |= Qt::AltModifier;
  return out;
}

// KeyboardEvent.key -> Qt key (0 = unknown).
int jsKey(const QString& key) {
  static const QHash<QString, int> named = {
      {"Enter", Qt::Key_Return},
      {"Escape", Qt::Key_Escape},
      {"Backspace", Qt::Key_Backspace},
      {"Tab", Qt::Key_Tab},
      {"Delete", Qt::Key_Delete},
      {"ArrowLeft", Qt::Key_Left},
      {"ArrowRight", Qt::Key_Right},
      {"ArrowUp", Qt::Key_Up},
      {"ArrowDown", Qt::Key_Down},
      {"Home", Qt::Key_Home},
      {"End", Qt::Key_End},
      {"PageUp", Qt::Key_PageUp},
      {"PageDown", Qt::Key_PageDown},
      {"Shift", Qt::Key_Shift},
      {"Control", Qt::Key_Control},
      {"Alt", Qt::Key_Alt},
      {"Meta", Qt::Key_Control},
      {"Insert", Qt::Key_Insert},
      {"F1", Qt::Key_F1},
      {"F2", Qt::Key_F2},
      {"F3", Qt::Key_F3},
      {"F4", Qt::Key_F4},
      {"F5", Qt::Key_F5},
      {"F11", Qt::Key_F11},
  };
  if (const auto it = named.constFind(key); it != named.cend())
    return it.value();
  if (key.size() == 1) {
    const QChar c = key.at(0);
    return c.isLetter() ? int(c.toUpper().unicode()) : int(c.unicode());
  }
  return 0;
}

}  // namespace

RemoteServer::RemoteServer(RemoteSession* session,
                           const QString& certificateDir, QObject* parent)
    : QObject(parent), m_session(session), m_certificateDir(certificateDir) {
  m_encoder.setMaxThreadCount(1);
  m_graceTimer.setSingleShot(true);
  m_graceTimer.setInterval(kGraceMs);
  connect(&m_graceTimer, &QTimer::timeout, this, [this]() {
    if (!m_client) m_session->stop();
  });
  connect(m_session, &RemoteSession::frameReady, this, &RemoteServer::onFrame);
  connect(m_session, &RemoteSession::exportsRequested, this, [this]() {
    if (m_client)
      m_client->sendTextMessage(QStringLiteral("{\"type\":\"exports\"}"));
  });
  m_thumbnailer.setMaxThreadCount(2);
  m_mouse = new QPointingDevice(
      QStringLiteral("Photon remote mouse"), 0x50484f54 /* "PHOT" */,
      QInputDevice::DeviceType::Mouse, QPointingDevice::PointerType::Generic,
      QInputDevice::Capability::Position | QInputDevice::Capability::Hover |
          QInputDevice::Capability::Scroll |
          QInputDevice::Capability::PixelScroll,
      1, 3, QString(), QPointingDeviceUniqueId(), this);
  QWindowSystemInterface::registerInputDevice(m_mouse);
  QFile page(QStringLiteral(":/Main/assets/remote/index.html"));
  page.open(QIODevice::ReadOnly);
  const QByteArray html = page.readAll();
  m_pageVersion = QString::fromLatin1(
      QCryptographicHash::hash(html, QCryptographicHash::Sha256)
          .toHex()
          .left(16));
  m_page = QByteArray(html).replace("__PHOTON_PAGE_VERSION__",
                                    m_pageVersion.toLatin1());
  {
    // The icon's own corners are transparent (rounded): fill them with its
    // dark background so iOS's mask shows no black slivers.
    const QImage icon(QStringLiteral(":/Main/assets/icons/photon.png"));
    QImage touch(180, 180, QImage::Format_RGB32);
    touch.fill(icon.isNull()
                   ? QColor(9, 9, 11)
                   : icon.pixelColor(icon.width() / 8, icon.height() / 8));
    QPainter painter(&touch);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(touch.rect(), icon);
    painter.end();
    QBuffer buffer(&m_touchIcon);
    buffer.open(QIODevice::WriteOnly);
    touch.save(&buffer, "PNG");
  }
  loadSettings();
  if (m_enabled) start();
}

RemoteServer::~RemoteServer() {
  stop();
  m_encoder.waitForDone();
  m_thumbnailer.waitForDone();
}

// --- Settings
// -------------------------------------------------------------------

void RemoteServer::loadSettings() {
  QSettings s(QSettings::IniFormat, QSettings::UserScope, "Photon", "Photon");
  m_port = s.value("remote/port", kDefaultPort).toInt();
  m_passwordHash =
      QByteArray::fromBase64(s.value("remote/passwordHash").toByteArray());
  m_passwordSalt =
      QByteArray::fromBase64(s.value("remote/passwordSalt").toByteArray());
  m_enabled = s.value("remote/enabled", false).toBool() && hasPassword();
}

void RemoteServer::saveSettings() const {
  QSettings s(QSettings::IniFormat, QSettings::UserScope, "Photon", "Photon");
  s.setValue("remote/enabled", m_enabled);
  s.setValue("remote/port", m_port);
  s.setValue("remote/passwordHash", m_passwordHash.toBase64());
  s.setValue("remote/passwordSalt", m_passwordSalt.toBase64());
}

void RemoteServer::setEnabled(bool on) {
  if (on == m_enabled) return;
  if (on && !hasPassword()) {
    m_error = QStringLiteral("Set a password first");
    emit stateChanged();
    return;
  }
  m_enabled = on;
  saveSettings();
  if (on)
    start();
  else
    stop();
  emit stateChanged();
}

void RemoteServer::setPort(int port) {
  if (port < 1024 || port > 65535 || port == m_port) return;
  m_port = port;
  saveSettings();
  if (running()) {
    stop();
    start();
  }
  emit stateChanged();
}

bool RemoteServer::setPassword(const QString& password) {
  if (password.size() < kMinPasswordLength) return false;
  m_passwordSalt = photon::remote::randomBytes(16);
  m_passwordHash =
      photon::remote::hashPassword(password, m_passwordSalt, kPbkdf2Iterations);
  m_tokens.clear();
  saveSettings();
  if (m_client) dropClient(QStringLiteral("The password was changed"));
  emit stateChanged();
  return true;
}

QStringList RemoteServer::addresses() const {
  // Interfaces a device can reach: up, connected, not virtual (containers,
  // bridges, VMs). The certificate still covers every address.
  static const QStringList virtualPrefixes = {
      "docker", "br-", "veth",    "virbr", "vmnet", "vboxnet",
      "podman", "cni", "flannel", "lxc",   "lxd"};
  QStringList out;
  for (const QNetworkInterface& iface : QNetworkInterface::allInterfaces()) {
    const auto flags = iface.flags();
    if (!(flags & QNetworkInterface::IsUp) ||
        !(flags & QNetworkInterface::IsRunning) ||
        (flags & QNetworkInterface::IsLoopBack))
      continue;
    const QString name = iface.name();
    if (std::any_of(virtualPrefixes.cbegin(), virtualPrefixes.cend(),
                    [&](const QString& p) { return name.startsWith(p); }))
      continue;
    for (const QNetworkAddressEntry& entry : iface.addressEntries()) {
      const QHostAddress a = entry.ip();
      if (a.protocol() == QAbstractSocket::IPv4Protocol && !a.isLoopback() &&
          !a.isLinkLocal())
        out << QStringLiteral("https://%1:%2").arg(a.toString()).arg(m_port);
    }
  }
  const QString host = QHostInfo::localHostName();
  if (!host.isEmpty())
    out << QStringLiteral("https://%1:%2")
               .arg(host.contains('.') ? host : host + ".local")
               .arg(m_port);
  out.removeDuplicates();
  return out;
}

QString RemoteServer::clientAddress() const {
  return m_client ? m_client->peerAddress().toString() : QString();
}

// --- Server
// ------------------------------------------------------------------------

void RemoteServer::start() {
  if (running()) return;
  m_error.clear();
  const photon::remote::ServerCertificate cert =
      photon::remote::ensureCertificates(m_certificateDir);
  if (!cert.error.isEmpty()) {
    m_error = cert.error;
    log("certificates: " + cert.error, photon::PHOTON_ERROR);
    emit stateChanged();
    return;
  }
  m_certificates = QSslCertificate::fromData(cert.certificatePem, QSsl::Pem);
  m_key = QSslKey(cert.keyPem, QSsl::Ec, QSsl::Pem);
  m_caPem = cert.caPem;
  if (!QSslSocket::supportsSsl() || m_certificates.isEmpty() ||
      m_key.isNull()) {
    m_error = QStringLiteral("TLS is not available");
    log(m_error, photon::PHOTON_ERROR);
    emit stateChanged();
    return;
  }

  m_ws = new QWebSocketServer(QStringLiteral("Photon"),
                              QWebSocketServer::NonSecureMode, this);
  connect(m_ws, &QWebSocketServer::newConnection, this,
          &RemoteServer::onWebSocket);
  auto* server = new TlsServer(this);
  if (!server->listen(QHostAddress::Any, quint16(m_port))) {
    m_error = QStringLiteral("Port %1 is not available: %2")
                  .arg(m_port)
                  .arg(server->errorString());
    log(m_error, photon::PHOTON_ERROR);
    delete server;
    delete m_ws;
    m_ws = nullptr;
    emit stateChanged();
    return;
  }
  m_server = server;
  connect(m_server, &QTcpServer::newConnection, this,
          &RemoteServer::onNewConnection);
  log(QString("listening on port %1").arg(m_port));
  emit stateChanged();
}

void RemoteServer::stop() {
  if (!running()) return;
  if (m_client) dropClient(QStringLiteral("Remote access was turned off"));
  m_graceTimer.stop();
  m_session->stop();
  // Later, not now: stop() can run inside a handler of these objects' own
  // signals (remote access switched off from the device).
  m_server->close();
  m_server->deleteLater();
  m_server = nullptr;
  m_ws->close();
  m_ws->deleteLater();
  m_ws = nullptr;
  m_tokens.clear();
  log("stopped");
  emit stateChanged();
}

void RemoteServer::onNewConnection() {
  while (QTcpSocket* tcp = m_server->nextPendingConnection()) {
    auto* socket = qobject_cast<QSslSocket*>(tcp);
    if (!socket) {
      tcp->deleteLater();
      continue;
    }
    connect(socket, &QAbstractSocket::disconnected, socket,
            &QObject::deleteLater);
    // Requests that never complete are dropped (WebSockets are exempt).
    QTimer::singleShot(kRequestTimeoutMs, socket, [socket]() {
      if (!socket->property("photonWebSocket").toBool()) socket->abort();
    });
    // The first byte tells HTTPS (a TLS handshake record) from plain HTTP.
    connect(socket, &QIODevice::readyRead, this, [this, socket]() {
      if (socket->isEncrypted() ||
          socket->mode() != QSslSocket::UnencryptedMode)
        return;
      char first = 0;
      if (socket->peek(&first, 1) != 1) return;
      if (first == 0x16) {
        QSslConfiguration config = QSslConfiguration::defaultConfiguration();
        config.setLocalCertificateChain(m_certificates);
        config.setPrivateKey(m_key);
        config.setProtocol(QSsl::TlsV1_2OrLater);
        config.setPeerVerifyMode(QSslSocket::VerifyNone);
        socket->setSslConfiguration(config);
        connect(socket, &QSslSocket::encrypted, this, [this, socket]() {
          connect(socket, &QIODevice::readyRead, this,
                  [this, socket]() { onEncryptedRequest(socket); });
          if (socket->bytesAvailable()) onEncryptedRequest(socket);
        });
        socket->startServerEncryption();
        return;
      }
      // Plain HTTP: redirect to HTTPS on the same host and port.
      const QByteArray data = socket->peek(kMaxHeaderBytes);
      const HttpRequest req = parseRequest(data);
      if (!req.valid) {
        if (data.size() >= kMaxHeaderBytes) socket->abort();
        return;
      }
      socket->readAll();
      QByteArray host = req.headers.value("host");
      if (host.isEmpty())
        host = QByteArray("localhost:") + QByteArray::number(m_port);
      const QByteArray location = "https://" + host + "/";
      socket->write("HTTP/1.1 301 Moved Permanently\r\nLocation: " + location +
                    "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
      socket->disconnectFromHost();
    });
  }
}

void RemoteServer::sendHttp(QSslSocket* socket, int status,
                            const QByteArray& type, const QByteArray& body,
                            const QByteArray& extraHeaders) {
  static const QHash<int, QByteArray> reasons = {
      {200, "OK"},
      {403, "Forbidden"},
      {404, "Not Found"},
      {405, "Method Not Allowed"},
      {431, "Request Header Fields Too Large"}};
  QByteArray head =
      "HTTP/1.1 " + QByteArray::number(status) + ' ' +
      reasons.value(status, "Error") + "\r\nContent-Type: " + type +
      "\r\nContent-Length: " + QByteArray::number(body.size()) +
      "\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff"
      "\r\nReferrer-Policy: no-referrer\r\nX-Frame-Options: DENY"
      "\r\nConnection: close\r\n" +
      extraHeaders + "\r\n";
  socket->write(head + body);
  socket->disconnectFromHost();
}

void RemoteServer::onEncryptedRequest(QSslSocket* socket) {
  if (socket->property("photonWebSocket").toBool() ||
      socket->property("photonAnswered").toBool())
    return;
  const QByteArray data = socket->peek(kMaxHeaderBytes);
  const HttpRequest req = parseRequest(data);
  if (!req.valid) {
    if (req.headerBytes > 0 || data.size() >= kMaxHeaderBytes) {
      socket->setProperty("photonAnswered", true);
      sendHttp(socket, 431, "text/plain", "Bad request");
    }
    return;
  }
  const QByteArray host = req.headers.value("host");

  if (req.headers.value("upgrade").toLower() == "websocket") {
    // Only pages served by this server may connect (no cross-site sockets).
    if (host.isEmpty() || req.headers.value("origin") != "https://" + host) {
      socket->setProperty("photonAnswered", true);
      sendHttp(socket, 403, "text/plain", "Forbidden");
      return;
    }
    socket->setProperty("photonWebSocket", true);
    QObject::disconnect(socket, nullptr, this, nullptr);  // our HTTP handlers
    // The QWebSocket made from it uses the socket (it is freed when it
    // disconnects); as the TLS server's child it would be deleted with the
    // server while still in use (remote access turned off while connected).
    socket->setParent(this);
    m_ws->handleConnection(socket);
    // The handshake is already buffered (peeked above) and no new readyRead
    // will come for it: signal it once more. Qt's handler only peeks, so an
    // extra call is harmless.
    QMetaObject::invokeMethod(socket, "readyRead", Qt::QueuedConnection);
    return;
  }

  socket->read(req.headerBytes);
  socket->setProperty("photonAnswered", true);
  if (req.method != "GET") {
    sendHttp(socket, 405, "text/plain", "Method not allowed");
    return;
  }
  const QByteArray path = req.path.split('?').first();
  if (path == "/" || path == "/index.html") {
    const QByteArray csp =
        "Content-Security-Policy: default-src 'self'; script-src 'self' "
        "'unsafe-inline'; "
        "style-src 'self' 'unsafe-inline'; img-src 'self' blob: data:; "
        "connect-src 'self' wss://" +
        host + "; frame-ancestors 'none'\r\n";
    sendHttp(socket, 200, "text/html; charset=utf-8", m_page, csp);
  } else if (path == "/photon-remote-ca.crt") {
    // This content type makes iOS offer to install the profile.
    sendHttp(socket, 200, "application/x-x509-ca-cert", m_caPem,
             "Content-Disposition: attachment; "
             "filename=\"photon-remote-ca.crt\"\r\n");
  } else if (path.startsWith("/exports/")) {
    serveExports(
        socket, path,
        QUrlQuery(QString::fromUtf8(req.path.mid(req.path.indexOf('?') + 1))),
        req.headers.value("range"));
  } else if (path == "/apple-touch-icon.png" ||
             path == "/apple-touch-icon-precomposed.png") {
    sendHttp(socket, 200, "image/png", m_touchIcon);
  } else if (path == "/icon.png") {
    QFile icon(QStringLiteral(":/Main/assets/icons/photon.png"));
    icon.open(QIODevice::ReadOnly);
    sendHttp(socket, 200, "image/png", icon.readAll());
  } else {
    sendHttp(socket, 404, "text/plain", "Not found");
  }
}

// --- WebSocket
// ----------------------------------------------------------------------

void RemoteServer::onWebSocket() {
  while (QWebSocket* socket = m_ws->nextPendingConnection()) {
    socket->setParent(this);
    socket->setMaxAllowedIncomingMessageSize(kMaxClientMessage);
    socket->setMaxAllowedIncomingFrameSize(kMaxClientMessage);
    connect(socket, &QWebSocket::textMessageReceived, this,
            &RemoteServer::onMessage);
    connect(socket, &QWebSocket::binaryMessageReceived, this,
            &RemoteServer::onBinaryMessage);
    connect(socket, &QWebSocket::disconnected, this, [this, socket]() {
      if (socket == m_client) {
        m_client.clear();
        m_awaitingAck = false;
        m_graceTimer.start();
        log("client disconnected");
        emit clientChanged();
      }
      socket->deleteLater();
    });
    QTimer::singleShot(kLoginTimeoutMs, socket, [socket]() {
      if (!socket->property("photonAuthenticated").toBool()) socket->close();
    });
  }
}

void RemoteServer::onBinaryMessage(const QByteArray&) {
}  // the client sends JSON only

void RemoteServer::onMessage(const QString& message) {
  auto* socket = qobject_cast<QWebSocket*>(sender());
  if (!socket) return;
  const QJsonObject msg = QJsonDocument::fromJson(message.toUtf8()).object();
  const QString type = msg.value("type").toString();
  if (!socket->property("photonAuthenticated").toBool()) {
    if (type == QLatin1String("hello")) handleHello(socket, msg);
    return;
  }
  if (socket != m_client) return;
  if (type == QLatin1String("ack")) {
    m_awaitingAck = false;
    encodeNext();
  } else if (type == QLatin1String("ping")) {
    QJsonObject pong{{"type", "pong"}, {"t", msg.value("t")}};
    socket->sendTextMessage(
        QString::fromUtf8(QJsonDocument(pong).toJson(QJsonDocument::Compact)));
  } else if (type == QLatin1String("resize")) {
    m_session->resize(QSize(qBound(320, msg.value("width").toInt(), 4096),
                            qBound(320, msg.value("height").toInt(), 4096)),
                      qBound(1.0, msg.value("dpr").toDouble(1.0), 3.0));
  } else {
    handleInput(msg);
  }
}

bool RemoteServer::throttled(const QString& address) const {
  const QDateTime since =
      QDateTime::currentDateTimeUtc().addSecs(-kFailureWindowSecs);
  int recent = 0;
  for (const QDateTime& t : m_failures.value(address))
    if (t > since) ++recent;
  return recent >= kMaxFailures;
}

void RemoteServer::noteFailure(const QString& address) {
  QList<QDateTime>& list = m_failures[address];
  list.append(QDateTime::currentDateTimeUtc());
  while (list.size() > kMaxFailures) list.removeFirst();
}

void RemoteServer::handleHello(QWebSocket* socket, const QJsonObject& msg) {
  const QString address = socket->peerAddress().toString();
  auto deny = [socket](const QString& reason) {
    QJsonObject out{{"type", "denied"}, {"reason", reason}};
    socket->sendTextMessage(
        QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact)));
    socket->close();
  };
  if (throttled(address)) {
    deny(QStringLiteral("Too many attempts. Try again in a few minutes."));
    return;
  }
  const QByteArray token = msg.value("token").toString().toUtf8();
  const QString password = msg.value("password").toString();
  bool ok = false;
  QByteArray newToken;
  if (!token.isEmpty()) {
    const auto it = m_tokens.constFind(token);
    ok = it != m_tokens.cend() && it.value() > QDateTime::currentDateTimeUtc();
    if (ok) newToken = token;
  } else if (!password.isEmpty() && hasPassword()) {
    ok = photon::remote::equalBytes(
        photon::remote::hashPassword(password, m_passwordSalt,
                                     kPbkdf2Iterations),
        m_passwordHash);
  }
  if (!ok) {
    if (token.isEmpty()) noteFailure(address);
    log("login refused from " + address, photon::PHOTON_WARNING);
    // A wrong password is answered after a pause; an expired token at once
    // (the page then asks for the password).
    if (token.isEmpty())
      QTimer::singleShot(1000, socket,
                         [deny]() { deny(QStringLiteral("Wrong password")); });
    else
      deny(QStringLiteral("Session expired"));
    return;
  }
  if (newToken.isEmpty()) {
    newToken = photon::remote::randomBytes(32).toBase64(
        QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
  }
  m_tokens.insert(newToken,
                  QDateTime::currentDateTimeUtc().addDays(kTokenDays));

  if (m_client && m_client != socket)
    dropClient(QStringLiteral("Another device connected"));
  socket->setProperty("photonAuthenticated", true);
  m_client = socket;
  m_awaitingAck = false;
  m_buttons = {};
  m_graceTimer.stop();
  QJsonObject welcome{{"type", "welcome"},
                      {"token", QString::fromUtf8(newToken)},
                      {"version", m_pageVersion}};
  socket->sendTextMessage(
      QString::fromUtf8(QJsonDocument(welcome).toJson(QJsonDocument::Compact)));

  const QSize size(qBound(320, msg.value("width").toInt(1024), 4096),
                   qBound(320, msg.value("height").toInt(768), 4096));
  const qreal dpr = qBound(1.0, msg.value("dpr").toDouble(1.0), 3.0);
  if (m_session->active()) {
    m_session->resize(size, dpr);
    m_session->requestFrame();
  } else if (!m_session->start(size, dpr)) {
    dropClient(QStringLiteral("The remote screen could not be created"));
    return;
  }
  log("client connected from " + address);
  emit clientChanged();
}

void RemoteServer::dropClient(const QString& reason) {
  if (!m_client) return;
  QWebSocket* socket = m_client;
  m_client.clear();
  m_awaitingAck = false;
  QJsonObject bye{{"type", "bye"}, {"reason", reason}};
  socket->sendTextMessage(
      QString::fromUtf8(QJsonDocument(bye).toJson(QJsonDocument::Compact)));
  socket->close();
  emit clientChanged();
}

void RemoteServer::endSession() {
  dropClient(QStringLiteral("The session was ended on the computer"));
  m_graceTimer.stop();
  m_session->stop();
}

// --- Input
// ----------------------------------------------------------------------------

void RemoteServer::handleInput(const QJsonObject& msg) {
  QQuickWindow* window = m_session->window();
  if (!window) return;
  // The offscreen window is the focus window while it is driven, so keyboard
  // focus, shortcuts and text cursors behave as in the desktop window.
  if (QGuiApplication::focusWindow() != window)
    QWindowSystemInterface::handleFocusWindowChanged<
        QWindowSystemInterface::SynchronousDelivery>(window,
                                                     Qt::OtherFocusReason);

  m_lastInputSeq = qMax(m_lastInputSeq, qint64(msg.value("seq").toDouble()));
  const QString type = msg.value("type").toString();
  const QPointF pos(msg.value("x").toDouble(), msg.value("y").toDouble());
  const Qt::KeyboardModifiers mods = jsModifiers(msg.value("mods").toInt());

  if (type == QLatin1String("pointer")) {
    const QString kind = msg.value("kind").toString();
    if (kind == QLatin1String("leave")) {
      QWindowSystemInterface::handleLeaveEvent<
          QWindowSystemInterface::SynchronousDelivery>(window);
      return;
    }
    const Qt::MouseButton button = jsButton(msg.value("button").toInt(-1));
    const Qt::MouseButtons buttons = jsButtons(msg.value("buttons").toInt());
    QEvent::Type eventType = QEvent::MouseMove;
    if (kind == QLatin1String("down"))
      eventType = QEvent::MouseButtonPress;
    else if (kind == QLatin1String("up"))
      eventType = QEvent::MouseButtonRelease;
    QWindowSystemInterface::handleMouseEvent<
        QWindowSystemInterface::SynchronousDelivery>(
        window, m_mouse, pos, pos, buttons,
        eventType == QEvent::MouseMove ? Qt::NoButton : button, eventType,
        mods);
    m_buttons = buttons;
  } else if (type == QLatin1String("wheel")) {
    const QPoint pixels(-qRound(msg.value("dx").toDouble()),
                        -qRound(msg.value("dy").toDouble()));
    // A wheel notch is ~100 px in browsers and 120 units in Qt.
    const QPoint angle(qRound(pixels.x() * 1.2), qRound(pixels.y() * 1.2));
    // Wheel events are only queued; deliver now to keep the input order.
    QWindowSystemInterface::handleWheelEvent(
        window, ulong(QDateTime::currentMSecsSinceEpoch()), m_mouse, pos, pos,
        pixels, angle, mods);
    QWindowSystemInterface::flushWindowSystemEvents();
  } else if (type == QLatin1String("key")) {
    const QString jsName = msg.value("key").toString();
    const int key = jsKey(jsName);
    QString text = msg.value("text").toString();
    if (key == 0 && text.isEmpty()) return;
    if (mods & Qt::ControlModifier) text.clear();
    const QEvent::Type eventType =
        msg.value("kind").toString() == QLatin1String("up") ? QEvent::KeyRelease
                                                            : QEvent::KeyPress;
    QWindowSystemInterface::handleKeyEvent<
        QWindowSystemInterface::SynchronousDelivery>(
        window, eventType, key, mods, text, msg.value("repeat").toBool());
  }
}

// --- Frames
// ----------------------------------------------------------------------------

void RemoteServer::onFrame(const QImage& frame, qint64 renderMs) {
  if (!m_client) return;
  m_pendingFrame = frame;
  m_pendingRenderMs = renderMs;
  m_pendingSeq = m_lastInputSeq;
  encodeNext();
}

void RemoteServer::encodeNext() {
  if (!m_client || m_awaitingAck || m_encoding || m_pendingFrame.isNull())
    return;
  const QImage frame = m_pendingFrame;
  const qint64 renderMs = m_pendingRenderMs, seq = m_pendingSeq;
  m_pendingFrame = QImage();
  m_encoding = true;
  m_encoder.start([this, frame, renderMs, seq]() {
    QElapsedTimer timer;
    timer.start();
    QByteArray jpeg;
    QBuffer buffer(&jpeg);
    buffer.open(QIODevice::WriteOnly);
    QImageWriter writer(&buffer, "jpeg");
    writer.setQuality(kJpegQuality);
    writer.write(frame.convertToFormat(QImage::Format_RGB888));
    const qint64 encodeMs = timer.elapsed();
    QMetaObject::invokeMethod(
        this,
        [this, jpeg, frame, renderMs, encodeMs, seq]() {
          m_encoding = false;
          if (m_client) {
            const QJsonObject header{{"seq", double(seq)},
                                     {"width", frame.width()},
                                     {"height", frame.height()},
                                     {"dpr", frame.devicePixelRatio()},
                                     {"renderMs", double(renderMs)},
                                     {"encodeMs", double(encodeMs)}};
            const QByteArray json =
                QJsonDocument(header).toJson(QJsonDocument::Compact);
            QByteArray message(4, Qt::Uninitialized);
            qToBigEndian<quint32>(quint32(json.size()), message.data());
            message += json + jpeg;
            m_client->sendBinaryMessage(message);
            m_awaitingAck = true;
          }
          encodeNext();
        },
        Qt::QueuedConnection);
  });
}

// --- Exports view
// -------------------------------------------------------------------

bool RemoteServer::validToken(const QByteArray& token) const {
  const auto it = m_tokens.constFind(token);
  return !token.isEmpty() && it != m_tokens.cend() &&
         it.value() > QDateTime::currentDateTimeUtc();
}

QString RemoteServer::exportDir() const {
  const QString folder = AppStateManager::instance()->currentFolder();
  return folder.isEmpty() ? QString()
                          : QDir::cleanPath(folder) + QStringLiteral("/Export");
}

QString RemoteServer::exportFile(const QString& name) const {
  const QString dir = exportDir();
  // A plain file name: no separators, no hidden files, a served type.
  if (dir.isEmpty() || name.isEmpty() || name.contains(QLatin1Char('/')) ||
      name.contains(QLatin1Char('\\')) || name.startsWith(QLatin1Char('.')) ||
      !exportTypes().contains(QFileInfo(name).suffix().toLower()))
    return QString();
  const QFileInfo fi(dir + QLatin1Char('/') + name);
  // Symlinks could point outside the folder.
  if (!fi.isFile() || fi.isSymLink() ||
      fi.absolutePath() != QFileInfo(dir).absoluteFilePath())
    return QString();
  return fi.absoluteFilePath();
}

void RemoteServer::serveExports(QSslSocket* socket, const QByteArray& path,
                                const QUrlQuery& query,
                                const QByteArray& range) {
  if (!validToken(query.queryItemValue("t").toUtf8())) {
    sendHttp(socket, 403, "text/plain", "Forbidden");
    return;
  }
  if (path == "/exports/list") {
    const QString dir = exportDir();
    QJsonArray files;
    if (!dir.isEmpty()) {
      const QFileInfoList entries = QDir(dir).entryInfoList(
          QDir::Files | QDir::NoSymLinks, QDir::Time);  // newest first
      for (const QFileInfo& fi : entries) {
        const QString ext = fi.suffix().toLower();
        if (!exportTypes().contains(ext) ||
            fi.fileName().startsWith(QLatin1Char('.')))
          continue;
        files.append(QJsonObject{
            {"name", fi.fileName()},
            {"size", double(fi.size())},
            {"modified", double(fi.lastModified().toMSecsSinceEpoch())},
            {"video", exportTypes().value(ext).startsWith("video/")}});
      }
    }
    const QString folder = AppStateManager::instance()->currentFolder();
    const QJsonObject out{
        {"folder", folder.isEmpty() ? QString() : QFileInfo(folder).fileName()},
        {"exists", !dir.isEmpty() && QFileInfo(dir).isDir()},
        {"files", files}};
    sendHttp(socket, 200, "application/json",
             QJsonDocument(out).toJson(QJsonDocument::Compact));
    return;
  }

  if (path == "/exports/zip") {
    QStringList files;
    qint64 total = 0;
    for (const QString& name :
         query.allQueryItemValues("name", QUrl::FullyDecoded)) {
      const QString f = exportFile(name);
      if (f.isEmpty()) {
        sendHttp(socket, 404, "text/plain", "Not found");
        return;
      }
      files << f;
      total += QFileInfo(f).size();
    }
    files.removeDuplicates();
    // QZipWriter writes plain (non-zip64) archives: under 4 GB.
    if (files.isEmpty() || total > 3900LL * 1024 * 1024) {
      sendHttp(socket, 404, "text/plain",
               files.isEmpty() ? "Not found" : "Too large for one download");
      return;
    }
    const QString folder =
        QFileInfo(AppStateManager::instance()->currentFolder()).fileName();
    QPointer<QSslSocket> guard(socket);
    // Exported images are compressed already: stored, built off the UI thread.
    QFuture<QString> future = QtConcurrent::run(&m_thumbnailer, [files]() {
      QTemporaryFile temp(QDir::tempPath() +
                          QStringLiteral("/photon-exports-XXXXXX.zip"));
      temp.setAutoRemove(false);
      if (!temp.open()) return QString();
      const QString tempPath = temp.fileName();
      {
        QZipWriter zip(&temp);
        zip.setCompressionPolicy(QZipWriter::NeverCompress);
        for (const QString& f : files) {
          QFile in(f);
          if (in.open(QIODevice::ReadOnly))
            zip.addFile(QFileInfo(f).fileName(), &in);
        }
        zip.close();
        if (zip.status() != QZipWriter::NoError) {
          temp.close();
          QFile::remove(tempPath);
          return QString();
        }
      }
      temp.close();
      return tempPath;
    });
    future.then(this, [this, guard, folder](const QString& zipPath) {
      if (zipPath.isEmpty()) {
        if (guard)
          sendHttp(guard, 404, "text/plain",
                   "The download could not be prepared");
        return;
      }
      if (!guard) {
        QFile::remove(zipPath);
        return;
      }
      sendFile(guard, zipPath, QByteArray(), true,
               (folder.isEmpty() ? QStringLiteral("Photon") : folder) +
                   QStringLiteral(" exports.zip"));
      // Deleted once streamed (the QFile is the socket's child; closed before
      // destroyed).
      if (QFile* file = guard->findChild<QFile*>())
        connect(file, &QObject::destroyed,
                [zipPath]() { QFile::remove(zipPath); });
      else
        QFile::remove(zipPath);
    });
    return;
  }

  const QString file =
      exportFile(query.queryItemValue("name", QUrl::FullyDecoded));
  if (file.isEmpty()) {
    sendHttp(socket, 404, "text/plain", "Not found");
    return;
  }
  if (path == "/exports/file") {
    sendFile(socket, file, range,
             query.queryItemValue("download") == QLatin1String("1"));
    return;
  }
  if (path != "/exports/thumb" || exportTypes()
                                      .value(QFileInfo(file).suffix().toLower())
                                      .startsWith("video/")) {
    sendHttp(socket, 404, "text/plain", "Not found");
    return;
  }
  const QString key =
      file + QLatin1Char('|') +
      QString::number(QFileInfo(file).lastModified().toMSecsSinceEpoch());
  if (const QByteArray* cached = m_thumbnails.object(key)) {
    sendHttp(socket, 200, "image/jpeg", *cached);
    return;
  }
  // Decoding a full-size export takes a while: off the UI thread.
  QPointer<QSslSocket> guard(socket);
  QFuture<QByteArray> future = QtConcurrent::run(&m_thumbnailer, [file]() {
    QImageReader reader(file);
    reader.setAutoTransform(true);
    const QSize size = reader.size();
    if (size.isValid())
      reader.setScaledSize(
          size.scaled(kThumbnailSize, kThumbnailSize, Qt::KeepAspectRatio));
    QImage image = reader.read();
    if (image.isNull()) return QByteArray();
    if (image.width() > kThumbnailSize || image.height() > kThumbnailSize)
      image = image.scaled(kThumbnailSize, kThumbnailSize, Qt::KeepAspectRatio,
                           Qt::SmoothTransformation);
    QByteArray jpeg;
    QBuffer buffer(&jpeg);
    buffer.open(QIODevice::WriteOnly);
    image.convertToFormat(QImage::Format_RGB888).save(&buffer, "JPEG", 80);
    return jpeg;
  });
  future.then(this, [this, guard, key](const QByteArray& jpeg) {
    if (!jpeg.isEmpty())
      m_thumbnails.insert(key, new QByteArray(jpeg), int(jpeg.size()));
    if (!guard) return;
    if (jpeg.isEmpty())
      sendHttp(guard, 404, "text/plain", "No preview");
    else
      sendHttp(guard, 200, "image/jpeg", jpeg);
  });
}

void RemoteServer::sendFile(QSslSocket* socket, const QString& path,
                            const QByteArray& range, bool download,
                            const QString& downloadName) {
  auto* file = new QFile(path, socket);
  if (!file->open(QIODevice::ReadOnly)) {
    sendHttp(socket, 404, "text/plain", "Not found");
    return;
  }
  const qint64 total = file->size();
  qint64 start = 0, end = total - 1;
  int status = 200;
  // One byte range ("bytes=a-b", "bytes=a-", "bytes=-n"): Safari needs it to
  // play videos.
  if (range.startsWith("bytes=") && !range.contains(',')) {
    const QByteArray spec = range.mid(6);
    const int dash = spec.indexOf('-');
    bool okA = false, okB = false;
    const qint64 a = spec.left(dash).toLongLong(&okA),
                 b = spec.mid(dash + 1).toLongLong(&okB);
    if (dash >= 0 && okA) {
      start = a;
      end = okB ? qMin(b, total - 1) : total - 1;
    } else if (dash == 0 && okB) {
      start = qMax<qint64>(0, total - b);
    }
    if (start > end || start >= total) {
      socket->write(
          "HTTP/1.1 416 Range Not Satisfiable\r\nContent-Range: bytes */" +
          QByteArray::number(total) +
          "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
      socket->disconnectFromHost();
      return;
    }
    status = 206;
  }
  const QString name =
      downloadName.isEmpty() ? QFileInfo(path).fileName() : downloadName;
  const QByteArray type =
      QFileInfo(path).suffix().compare(QLatin1String("zip"),
                                       Qt::CaseInsensitive) == 0
          ? QByteArray("application/zip")
          : exportTypes().value(QFileInfo(path).suffix().toLower(),
                                "application/octet-stream");
  QByteArray head = (status == 206 ? "HTTP/1.1 206 Partial Content\r\n"
                                   : "HTTP/1.1 200 OK\r\n");
  head += "Content-Type: " + type +
          "\r\nContent-Length: " + QByteArray::number(end - start + 1) +
          "\r\nAccept-Ranges: bytes\r\nCache-Control: private, max-age=300"
          "\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: "
          "no-referrer\r\nConnection: close\r\n";
  if (status == 206)
    head += "Content-Range: bytes " + QByteArray::number(start) + '-' +
            QByteArray::number(end) + '/' + QByteArray::number(total) + "\r\n";
  head += QByteArray(download ? "Content-Disposition: attachment"
                              : "Content-Disposition: inline") +
          "; filename*=UTF-8''" + QUrl::toPercentEncoding(name) + "\r\n\r\n";
  socket->write(head);
  file->seek(start);

  // Streamed in chunks as the socket drains (exports can be hundreds of MB).
  auto remaining = std::make_shared<qint64>(end - start + 1);
  auto pump = [socket, file, remaining]() {
    while (*remaining > 0 &&
           socket->bytesToWrite() + socket->encryptedBytesToWrite() <
               kMaxBuffered) {
      const QByteArray chunk = file->read(qMin(kFileChunk, *remaining));
      if (chunk.isEmpty()) {
        *remaining = 0;
        break;
      }
      socket->write(chunk);
      *remaining -= chunk.size();
    }
    if (*remaining <= 0 && file->isOpen()) {
      file->close();
      socket->disconnectFromHost();
    }
  };
  connect(socket, &QSslSocket::encryptedBytesWritten, socket, pump);
  pump();
}
