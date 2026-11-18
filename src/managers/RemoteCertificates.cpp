#include "RemoteCertificates.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QHostInfo>
#include <QNetworkInterface>
#include <QSaveFile>
#include <QSslCertificate>
#include <QSslKey>
#include <memory>

#include "LogManager.h"

namespace photon::remote {
namespace {

void log(const QString& msg, photon::LogLevel level = photon::PHOTON_INFO) {
  photon::LogManager::instance()->log("[ RemoteCertificates.cpp ] - " + msg,
                                      level);
}

struct PkeyFree {
  void operator()(EVP_PKEY* p) const { EVP_PKEY_free(p); }
};
struct X509Free {
  void operator()(X509* p) const { X509_free(p); }
};
struct BioFree {
  void operator()(BIO* p) const { BIO_free(p); }
};
using Pkey = std::unique_ptr<EVP_PKEY, PkeyFree>;
using Cert = std::unique_ptr<X509, X509Free>;
using Bio = std::unique_ptr<BIO, BioFree>;

// iOS rejects server certificates valid for more than 825 days.
constexpr long kServerDays = 800;
constexpr long kCaDays = 3650;

QByteArray bioBytes(BIO* bio) {
  char* data = nullptr;
  const long size = BIO_get_mem_data(bio, &data);
  return QByteArray(data, int(size));
}

QByteArray keyPem(EVP_PKEY* key) {
  Bio bio(BIO_new(BIO_s_mem()));
  PEM_write_bio_PrivateKey(bio.get(), key, nullptr, nullptr, 0, nullptr,
                           nullptr);
  return bioBytes(bio.get());
}

QByteArray certPem(X509* cert) {
  Bio bio(BIO_new(BIO_s_mem()));
  PEM_write_bio_X509(bio.get(), cert);
  return bioBytes(bio.get());
}

Pkey readKey(const QByteArray& pem) {
  Bio bio(BIO_new_mem_buf(pem.constData(), int(pem.size())));
  return Pkey(PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr));
}

Cert readCert(const QByteArray& pem) {
  Bio bio(BIO_new_mem_buf(pem.constData(), int(pem.size())));
  return Cert(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
}

bool addExtension(X509* cert, X509* issuer, int nid, const char* value) {
  X509V3_CTX ctx;
  X509V3_set_ctx_nodb(&ctx);
  X509V3_set_ctx(&ctx, issuer, cert, nullptr, nullptr, 0);
  X509_EXTENSION* ext = X509V3_EXT_conf_nid(nullptr, &ctx, nid, value);
  if (!ext) return false;
  const bool ok = X509_add_ext(cert, ext, -1) == 1;
  X509_EXTENSION_free(ext);
  return ok;
}

// A certificate with a random serial, `days` of validity from an hour ago.
Cert newCertificate(EVP_PKEY* key, const QString& commonName, long days) {
  Cert cert(X509_new());
  X509_set_version(cert.get(), 2);
  const QByteArray serial = randomBytes(16);
  BIGNUM* bn = BN_bin2bn(
      reinterpret_cast<const unsigned char*>(serial.constData()), 16, nullptr);
  BN_clear_bit(bn, 127);  // positive
  BN_to_ASN1_INTEGER(bn, X509_get_serialNumber(cert.get()));
  BN_free(bn);
  X509_gmtime_adj(X509_getm_notBefore(cert.get()), -3600);
  X509_gmtime_adj(X509_getm_notAfter(cert.get()), days * 24 * 3600);
  X509_set_pubkey(cert.get(), key);
  X509_NAME* name = X509_get_subject_name(cert.get());
  X509_NAME_add_entry_by_txt(name, "O", MBSTRING_UTF8,
                             reinterpret_cast<const unsigned char*>("Photon"),
                             -1, -1, 0);
  const QByteArray cn = commonName.toUtf8();
  X509_NAME_add_entry_by_txt(
      name, "CN", MBSTRING_UTF8,
      reinterpret_cast<const unsigned char*>(cn.constData()), -1, -1, 0);
  return cert;
}

QByteArray readFile(const QString& path) {
  QFile f(path);
  return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

bool writeFile(const QString& path, const QByteArray& data, bool secret) {
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly)) return false;
  f.write(data);
  if (!f.commit()) return false;
  if (secret)
    QFile::setPermissions(path,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  return true;
}

// The names a certificate covers (its subjectAltName entries).
QStringList coveredNames(const QByteArray& pem) {
  QStringList names;
  const QList<QSslCertificate> certs =
      QSslCertificate::fromData(pem, QSsl::Pem);
  if (certs.isEmpty()) return names;
  const auto alt = certs.first().subjectAlternativeNames();
  for (auto it = alt.cbegin(); it != alt.cend(); ++it) names << it.value();
  return names;
}

bool stillValid(const QByteArray& pem, qint64 marginSecs) {
  const QList<QSslCertificate> certs =
      QSslCertificate::fromData(pem, QSsl::Pem);
  return !certs.isEmpty() &&
         certs.first().expiryDate() >
             QDateTime::currentDateTimeUtc().addSecs(marginSecs);
}

ServerCertificate failed(const QString& error) {
  ServerCertificate r;
  r.error = error;
  return r;
}

}  // namespace

QStringList localNames() {
  QStringList names{QStringLiteral("localhost")};
  const QString host = QHostInfo::localHostName();
  if (!host.isEmpty()) {
    names << host;
    if (!host.contains(QLatin1Char('.')))
      names << host + QStringLiteral(".local");
  }
  for (const QHostAddress& a : QNetworkInterface::allAddresses()) {
    if (a.isLinkLocal() || a.isNull()) continue;
    QHostAddress plain(a);
    plain.setScopeId(QString());
    names << plain.toString();
  }
  names.removeDuplicates();
  return names;
}

QByteArray randomBytes(int count) {
  QByteArray out(count, Qt::Uninitialized);
  RAND_bytes(reinterpret_cast<unsigned char*>(out.data()), count);
  return out;
}

QByteArray hashPassword(const QString& password, const QByteArray& salt,
                        int iterations) {
  const QByteArray pw = password.toUtf8();
  QByteArray out(32, Qt::Uninitialized);
  PKCS5_PBKDF2_HMAC(pw.constData(), int(pw.size()),
                    reinterpret_cast<const unsigned char*>(salt.constData()),
                    int(salt.size()), iterations, EVP_sha256(), int(out.size()),
                    reinterpret_cast<unsigned char*>(out.data()));
  return out;
}

bool equalBytes(const QByteArray& a, const QByteArray& b) {
  return a.size() == b.size() &&
         CRYPTO_memcmp(a.constData(), b.constData(), size_t(a.size())) == 0;
}

ServerCertificate ensureCertificates(const QString& dir) {
  ServerCertificate result;
  QDir().mkpath(dir);
  const QString caCrtPath = dir + "/photon-remote-ca.crt",
                caKeyPath = dir + "/photon-remote-ca.key";
  const QString crtPath = dir + "/photon-remote.crt",
                keyPath = dir + "/photon-remote.key";

  // The authority: kept as long as it is readable and valid for 30+ days.
  QByteArray caCrt = readFile(caCrtPath), caKeyBytes = readFile(caKeyPath);
  Pkey caKey = readKey(caKeyBytes);
  Cert ca = readCert(caCrt);
  bool newCa = false;
  if (!caKey || !ca || !stillValid(caCrt, 30LL * 24 * 3600)) {
    caKey.reset(EVP_EC_gen("P-256"));
    if (!caKey) return failed("could not create a key");
    const QString host = QHostInfo::localHostName();
    ca =
        newCertificate(caKey.get(), "Photon Remote CA (" + host + ")", kCaDays);
    X509_set_issuer_name(ca.get(), X509_get_subject_name(ca.get()));
    addExtension(ca.get(), ca.get(), NID_basic_constraints,
                 "critical,CA:TRUE,pathlen:0");
    addExtension(ca.get(), ca.get(), NID_key_usage,
                 "critical,keyCertSign,cRLSign");
    addExtension(ca.get(), ca.get(), NID_subject_key_identifier, "hash");
    if (!X509_sign(ca.get(), caKey.get(), EVP_sha256()))
      return failed("could not sign the authority");
    caCrt = certPem(ca.get());
    if (!writeFile(caKeyPath, keyPem(caKey.get()), true) ||
        !writeFile(caCrtPath, caCrt, false))
      return failed("could not write the certificates to " + dir);
    newCa = true;
    log("created a new certificate authority");
  }

  // The server certificate: re-issued for a new authority, new names or
  // addresses, or when it is about to expire.
  const QStringList names = localNames();
  QByteArray crt = readFile(crtPath), key = readFile(keyPath);
  const QStringList covered = coveredNames(crt);
  bool coversAll = !covered.isEmpty();
  for (const QString& n : names) coversAll = coversAll && covered.contains(n);
  if (newCa || !coversAll || key.isEmpty() ||
      !stillValid(crt, 7LL * 24 * 3600)) {
    Pkey serverKey(EVP_EC_gen("P-256"));
    if (!serverKey) return failed("could not create a key");
    Cert server = newCertificate(
        serverKey.get(), "Photon Remote (" + QHostInfo::localHostName() + ")",
        kServerDays);
    X509_set_issuer_name(server.get(), X509_get_subject_name(ca.get()));
    QStringList alt;
    for (const QString& n : names)
      alt << (QHostAddress(n).isNull() ? "DNS:" : "IP:") + n;
    const QByteArray altValue = alt.join(',').toUtf8();
    addExtension(server.get(), ca.get(), NID_basic_constraints,
                 "critical,CA:FALSE");
    addExtension(server.get(), ca.get(), NID_key_usage,
                 "critical,digitalSignature");
    addExtension(server.get(), ca.get(), NID_ext_key_usage, "serverAuth");
    addExtension(server.get(), ca.get(), NID_subject_alt_name,
                 altValue.constData());
    addExtension(server.get(), ca.get(), NID_authority_key_identifier,
                 "keyid:always");
    if (!X509_sign(server.get(), caKey.get(), EVP_sha256()))
      return failed("could not sign the certificate");
    crt = certPem(server.get());
    key = keyPem(serverKey.get());
    if (!writeFile(keyPath, key, true) || !writeFile(crtPath, crt, false))
      return failed("could not write the certificates to " + dir);
    log("issued the server certificate for " + names.join(", "));
  }
  result.certificatePem = crt + caCrt;
  result.keyPem = key;
  result.caPem = caCrt;
  return result;
}

}  // namespace photon::remote
