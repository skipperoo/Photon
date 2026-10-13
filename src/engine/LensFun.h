#pragma once

#include <QImage>
#include <QString>
#include <QVector>

#include <atomic>

namespace photon {
namespace lensfun {

struct DistortionCalib {
  float focal = 0.0f;
  QString model;  // "poly3" | "poly5" | "ptlens"
  // poly3/poly5: k1, k2. ptlens: a = k1, b = k2, c = k3.
  float k1 = 0.0f;
  float k2 = 0.0f;
  float k3 = 0.0f;
};

struct TcaCalib {
  float focal = 0.0f;
  QString model;  // "linear" | "poly3"
  float vr = 1.0f;
  float vb = 1.0f;
  float cr = 0.0f;
  float cb = 0.0f;
  float br = 0.0f;
  float bb = 0.0f;
};

struct VignettingCalib {
  float focal = 0.0f;
  float aperture = 0.0f;
  float distance = 0.0f;
  float k1 = 0.0f;
  float k2 = 0.0f;
  float k3 = 0.0f;
};

struct LensEntry {
  QString maker;
  QString model;
  QString mount;
  float crop = 1.0f;
  float minFocal = 0.0f;
  float maxFocal = 0.0f;
  QVector<DistortionCalib> distortion;
  QVector<TcaCalib> tca;
  QVector<VignettingCalib> vignetting;
};

struct CameraEntry {
  QString maker;
  QString model;
  QString mount;
  float crop = 1.0f;
};

// Resolved, focal-interpolated calibration for one photo.
struct Profile {
  bool valid = false;
  QString lensName;
  float focal = 0.0f;
  float crop = 1.0f;

  QString distortionModel;
  float distortionK[3] = {0.0f, 0.0f, 0.0f};

  bool hasTca = false;
  QString tcaModel;
  float tcaR[3] = {1.0f, 0.0f, 0.0f};  // v, c, b
  float tcaB[3] = {1.0f, 0.0f, 0.0f};

  bool hasVignetting = false;
  float vignetting[3] = {0.0f, 0.0f, 0.0f};
};

class Database {
 public:
  static Database& instance();

  // Parses the bundled database once (thread-safe, lazy).
  bool ensureLoaded();

  Profile match(const QString& cameraMake, const QString& cameraModel,
                const QString& lensModel, float focal, float aperture) const;

  // Test hook: replace the catalogue from explicit XML byte arrays.
  void loadFromBuiltinXml(const QVector<QByteArray>& documents);

  int cameraCount() const { return m_cameras.size(); }
  int lensCount() const { return m_lenses.size(); }

  // Brand (maker) names and the lenses of one brand, for manual selection.
  QStringList makers() const;
  QStringList lensesForMaker(const QString& maker) const;
  QString makerForLens(const QString& lensModel) const;

 private:
  Database() = default;
  void parseDocument(const QByteArray& data);

  std::atomic<bool> m_loaded{false};
  bool m_parseAttempted = false;
  QVector<CameraEntry> m_cameras;
  QVector<LensEntry> m_lenses;
};

// Applies the resolved profile to an image (RGB888 or RGBX64) and returns a
// new image of the same format. Amounts are 0..100; unbounded values are
// clamped. Returns the input unchanged when nothing applies.
QImage applyProfile(const QImage& src, const Profile& profile,
                    float distortionAmount, float caAmount,
                    float vignettingAmount);

}  // namespace lensfun
}  // namespace photon
