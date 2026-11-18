#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

// TLS certificates for the remote UI, like mkcert: a local certificate
// authority (installed once on the remote device to trust it) signs a server
// certificate for this computer's names and addresses. The server certificate
// is re-issued when those change, without re-installing the authority.
// Files live in `dir`: photon-remote-ca.{crt,key}, photon-remote.{crt,key}.
namespace photon::remote {

struct ServerCertificate {
  QByteArray certificatePem;  // server certificate followed by the CA
  QByteArray keyPem;
  QByteArray caPem;  // offered for download (install on the device)
  QString error;     // non-empty on failure
};

// The names and addresses the certificate must cover: localhost, the host
// name (and <host>.local), every non-link-local IPv4/IPv6 address.
QStringList localNames();

// Loads the certificates from `dir`, creating or re-issuing them as needed.
ServerCertificate ensureCertificates(const QString& dir);

// Password hashing for the remote login (PBKDF2-HMAC-SHA256).
QByteArray randomBytes(int count);
QByteArray hashPassword(const QString& password, const QByteArray& salt,
                        int iterations);
// Constant-time comparison.
bool equalBytes(const QByteArray& a, const QByteArray& b);

}  // namespace photon::remote
