#include "LensFun.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QStringList>
#include <QXmlStreamReader>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>

#include "LogManager.h"

namespace photon {
namespace lensfun {

namespace {

QString normalizeKey(const QString& value) {
  QString out;
  out.reserve(value.size());
  for (const QChar c : value) {
    if (c.isLetterOrNumber()) out.append(c.toLower());
  }
  return out;
}

int longestCommonSubstring(const QString& a, const QString& b) {
  if (a.isEmpty() || b.isEmpty()) return 0;
  QVector<int> prev(b.size() + 1, 0);
  QVector<int> cur(b.size() + 1, 0);
  int best = 0;
  for (int i = 1; i <= a.size(); ++i) {
    for (int j = 1; j <= b.size(); ++j) {
      if (a[i - 1] == b[j - 1]) {
        cur[j] = prev[j - 1] + 1;
        best = std::max(best, cur[j]);
      } else {
        cur[j] = 0;
      }
    }
    std::swap(prev, cur);
    std::fill(cur.begin(), cur.end(), 0);
  }
  return best;
}

float matchScore(const QString& a, const QString& b) {
  if (a.isEmpty() || b.isEmpty()) return 0.0f;
  const int lcs = longestCommonSubstring(a, b);
  return float(lcs) / float(std::max(a.size(), b.size()));
}

// Reads the text of the current element, skipping any child elements.
// (QXmlStreamReader::readElementText() raises an error on child elements.)
QString readTextSafely(QXmlStreamReader& xml) {
  QString text;
  while (!xml.atEnd()) {
    const QXmlStreamReader::TokenType token = xml.readNext();
    if (token == QXmlStreamReader::Characters) {
      text += xml.text();
    } else if (token == QXmlStreamReader::StartElement) {
      xml.skipCurrentElement();
    } else if (token == QXmlStreamReader::EndElement) {
      break;
    }
  }
  return text.trimmed();
}

float readFloat(const QXmlStreamAttributes& attrs, const char* name,
                float fallback = 0.0f) {
  const QStringView ref = attrs.value(QLatin1String(name));
  if (ref.isEmpty()) return fallback;
  bool ok = false;
  const float v = ref.toFloat(&ok);
  return ok ? v : fallback;
}

float clampFocal(float focal, const LensEntry& lens) {
  if (lens.minFocal > 0.0f && focal < lens.minFocal) return lens.minFocal;
  if (lens.maxFocal > 0.0f && focal > lens.maxFocal) return lens.maxFocal;
  return focal;
}

template <typename T>
float lerp(T a, T b, float t) {
  return float(a) + (float(b) - float(a)) * t;
}

}  // namespace

Database& Database::instance() {
  static Database db;
  return db;
}

void Database::parseDocument(const QByteArray& data) {
  QXmlStreamReader xml(data);
  CameraEntry* camera = nullptr;
  LensEntry* lens = nullptr;
  QString lensMaker;
  QString lensModel;

  while (!xml.atEnd()) {
    xml.readNext();
    if (!xml.isStartElement()) {
      if (xml.isEndElement()) {
        if (xml.name() == QLatin1String("camera")) {
          if (camera && !camera->model.isEmpty()) m_cameras.append(*camera);
          camera = nullptr;
        } else if (xml.name() == QLatin1String("lens")) {
          if (lens && !lensModel.isEmpty()) {
            lens->maker = lensMaker;
            lens->model = lensModel;
            m_lenses.append(*lens);
          }
          lens = nullptr;
        }
      }
      continue;
    }

    const QString name = xml.name().toString();
    const QXmlStreamAttributes attrs = xml.attributes();

    if (name == QLatin1String("camera")) {
      camera = new CameraEntry();
    } else if (name == QLatin1String("lens")) {
      lens = new LensEntry();
      lensMaker.clear();
      lensModel.clear();
    } else if (name == QLatin1String("maker")) {
      const QString text = readTextSafely(xml);
      if (camera) camera->maker = text;
      if (lens && lensModel.isEmpty()) lensMaker = text;
    } else if (name == QLatin1String("model")) {
      const QString text = readTextSafely(xml);
      if (camera) {
        if (camera->model.isEmpty()) camera->model = text;
      } else if (lens) {
        if (lensModel.isEmpty()) lensModel = text;
      }
    } else if (name == QLatin1String("mount")) {
      const QString text = readTextSafely(xml);
      if (camera) camera->mount = text;
      if (lens) lens->mount = text;
    } else if (name == QLatin1String("cropfactor")) {
      const float value = readTextSafely(xml).toFloat();
      if (camera) camera->crop = value;
      if (lens) lens->crop = value;
    } else if (name == QLatin1String("distortion") && lens) {
      DistortionCalib calib;
      calib.model = attrs.value(QLatin1String("model")).toString();
      calib.focal = readFloat(attrs, "focal");
      if (calib.model == QLatin1String("ptlens")) {
        calib.k1 = readFloat(attrs, "a");
        calib.k2 = readFloat(attrs, "b");
        calib.k3 = readFloat(attrs, "c");
      } else {
        calib.k1 = readFloat(attrs, "k1");
        calib.k2 = readFloat(attrs, "k2");
      }
      if (calib.focal > 0.0f) lens->distortion.append(calib);
    } else if (name == QLatin1String("tca") && lens) {
      TcaCalib calib;
      calib.model = attrs.value(QLatin1String("model")).toString();
      calib.focal = readFloat(attrs, "focal");
      calib.vr = readFloat(attrs, "vr", 1.0f);
      calib.vb = readFloat(attrs, "vb", 1.0f);
      calib.cr = readFloat(attrs, "cr");
      calib.cb = readFloat(attrs, "cb");
      calib.br = readFloat(attrs, "br");
      calib.bb = readFloat(attrs, "bb");
      if (calib.focal > 0.0f) lens->tca.append(calib);
    } else if (name == QLatin1String("vignetting") && lens) {
      VignettingCalib calib;
      calib.focal = readFloat(attrs, "focal");
      calib.aperture = readFloat(attrs, "aperture");
      calib.distance = readFloat(attrs, "distance");
      calib.k1 = readFloat(attrs, "k1");
      calib.k2 = readFloat(attrs, "k2");
      calib.k3 = readFloat(attrs, "k3");
      if (calib.focal > 0.0f) lens->vignetting.append(calib);
    }
  }

  delete camera;
  delete lens;
}

bool Database::ensureLoaded() {
  static QMutex loadMutex;
  QMutexLocker locker(&loadMutex);
  if (m_loaded.load()) return true;
  if (m_parseAttempted) return m_loaded.load();
  m_parseAttempted = true;

  QDir dir(QStringLiteral(":/lensfun"));
  if (!dir.exists()) {
    LogManager::instance()->log(
        "[ LensFun.cpp ] - bundled lensfun database not found in resources",
        PHOTON_ERROR);
    return false;
  }

  const QStringList files =
      dir.entryList(QStringList() << QStringLiteral("*.xml"), QDir::Files);
  for (const QString& name : files) {
    QFile file(dir.filePath(name));
    if (!file.open(QIODevice::ReadOnly)) continue;
    parseDocument(file.readAll());
  }

  m_lenses.shrink_to_fit();
  m_cameras.shrink_to_fit();
  m_loaded.store(true);
  LogManager::instance()->log(
      QString("[ LensFun.cpp ] - loaded %1 cameras / %2 lenses from %3 files")
          .arg(m_cameras.size())
          .arg(m_lenses.size())
          .arg(files.size()),
      PHOTON_INFO);
  return true;
}

void Database::loadFromBuiltinXml(const QVector<QByteArray>& documents) {
  m_cameras.clear();
  m_lenses.clear();
  for (const QByteArray& doc : documents) parseDocument(doc);
  m_loaded.store(true);
  m_parseAttempted = true;
}

QStringList Database::makers() const {
  QStringList out;
  for (const LensEntry& lens : m_lenses) {
    if (!lens.maker.isEmpty() && !out.contains(lens.maker)) out << lens.maker;
  }
  out.sort(Qt::CaseInsensitive);
  return out;
}

QStringList Database::lensesForMaker(const QString& maker) const {
  QStringList out;
  if (maker.isEmpty()) return out;
  for (const LensEntry& lens : m_lenses) {
    if (lens.maker.compare(maker, Qt::CaseInsensitive) == 0)
      out << lens.model;
  }
  out.removeDuplicates();
  out.sort(Qt::CaseInsensitive);
  return out;
}

QString Database::makerForLens(const QString& lensModel) const {
  const QString key = normalizeKey(lensModel);
  if (key.isEmpty()) return QString();
  QString bestMaker;
  float bestScore = 0.0f;
  for (const LensEntry& lens : m_lenses) {
    const QString lensKey = normalizeKey(lens.model);
    if (lensKey == key) return lens.maker;
    const float score = matchScore(key, lensKey);
    if (score > bestScore) {
      bestScore = score;
      bestMaker = lens.maker;
    }
  }
  return bestScore >= 0.5f ? bestMaker : QString();
}

Profile Database::match(const QString& cameraMake, const QString& cameraModel,
                        const QString& lensModel, float focal,
                        float aperture) const {
  Profile profile;
  if (!m_loaded.load()) return profile;

  const QString cameraKey = normalizeKey(cameraModel);
  const QString lensKey = normalizeKey(lensModel);
  if (lensKey.size() < 4) return profile;

  // Best lens candidate by name similarity.
  const LensEntry* best = nullptr;
  float bestScore = 0.0f;
  for (const LensEntry& lens : m_lenses) {
    const float score = matchScore(lensKey, normalizeKey(lens.model));
    if (score > bestScore) {
      bestScore = score;
      best = &lens;
    }
    if (score >= 0.999f) break;
  }
  if (!best || bestScore < 0.5f) return profile;

  profile.valid = true;
  profile.lensName = best->model;
  profile.crop = best->crop;
  profile.focal = clampFocal(focal, *best);

  // Camera crop factor (fallback to the lens entry).
  const QString cameraKeyFull = normalizeKey(cameraMake + cameraModel);
  for (const CameraEntry& cam : m_cameras) {
    const QString camModel = normalizeKey(cam.model);
    const QString camFull = normalizeKey(cam.maker + cam.model);
    const bool modelMatch =
        !cameraKey.isEmpty() && camModel.size() >= 4 &&
        (camModel == cameraKey || camModel.endsWith(cameraKey) ||
         cameraKey.endsWith(camModel));
    const bool fullMatch =
        !cameraKeyFull.isEmpty() && camFull.size() >= 4 &&
        (camFull == cameraKeyFull || camFull.endsWith(cameraKeyFull) ||
         cameraKeyFull.endsWith(camFull));
    if (modelMatch || fullMatch) {
      profile.crop = cam.crop;
      break;
    }
  }

  // Distortion: interpolate between the bracketing focal entries.
  if (!best->distortion.isEmpty()) {
    QVector<DistortionCalib> entries = best->distortion;
    std::sort(entries.begin(), entries.end(),
              [](const DistortionCalib& a, const DistortionCalib& b) {
                return a.focal < b.focal;
              });
    const DistortionCalib* lo = &entries.first();
    const DistortionCalib* hi = &entries.last();
    for (const DistortionCalib& e : entries) {
      if (e.focal <= profile.focal) lo = &e;
      if (e.focal >= profile.focal) {
        hi = &e;
        break;
      }
    }
    if (lo->model == hi->model && hi->focal > lo->focal) {
      const float t = (profile.focal - lo->focal) / (hi->focal - lo->focal);
      profile.distortionModel = lo->model;
      profile.distortionK[0] = lerp(lo->k1, hi->k1, t);
      profile.distortionK[1] = lerp(lo->k2, hi->k2, t);
      profile.distortionK[2] = lerp(lo->k3, hi->k3, t);
    } else {
      const DistortionCalib* nearest =
          std::abs(lo->focal - profile.focal) <=
                  std::abs(hi->focal - profile.focal)
              ? lo
              : hi;
      profile.distortionModel = nearest->model;
      profile.distortionK[0] = nearest->k1;
      profile.distortionK[1] = nearest->k2;
      profile.distortionK[2] = nearest->k3;
    }
  }

  // TCA: same bracketing interpolation.
  if (!best->tca.isEmpty()) {
    QVector<TcaCalib> entries = best->tca;
    std::sort(entries.begin(), entries.end(),
              [](const TcaCalib& a, const TcaCalib& b) {
                return a.focal < b.focal;
              });
    const TcaCalib* lo = &entries.first();
    const TcaCalib* hi = &entries.last();
    for (const TcaCalib& e : entries) {
      if (e.focal <= profile.focal) lo = &e;
      if (e.focal >= profile.focal) {
        hi = &e;
        break;
      }
    }
    const TcaCalib* pick = lo;
    float t = 0.0f;
    if (lo->model == hi->model && hi->focal > lo->focal) {
      t = (profile.focal - lo->focal) / (hi->focal - lo->focal);
    } else if (std::abs(hi->focal - profile.focal) <
               std::abs(lo->focal - profile.focal)) {
      pick = hi;
    }
    profile.hasTca = true;
    profile.tcaModel = pick->model;
    profile.tcaR[0] = lerp(pick->vr, hi->vr, t);
    profile.tcaR[1] = lerp(pick->cr, hi->cr, t);
    profile.tcaR[2] = lerp(pick->br, hi->br, t);
    profile.tcaB[0] = lerp(pick->vb, hi->vb, t);
    profile.tcaB[1] = lerp(pick->cb, hi->cb, t);
    profile.tcaB[2] = lerp(pick->bb, hi->bb, t);
  }

  // Vignetting: nearest focal + nearest aperture (no interpolation).
  if (!best->vignetting.isEmpty()) {
    const VignettingCalib* pick = nullptr;
    float bestCost = 1e30f;
    for (const VignettingCalib& e : best->vignetting) {
      float cost = std::abs(e.focal - profile.focal);
      if (e.aperture > 0.0f && aperture > 0.0f)
        cost += std::abs(e.aperture - aperture) * 0.5f;
      if (cost < bestCost) {
        bestCost = cost;
        pick = &e;
      }
    }
    if (pick) {
      profile.hasVignetting = true;
      profile.vignetting[0] = pick->k1;
      profile.vignetting[1] = pick->k2;
      profile.vignetting[2] = pick->k3;
    }
  }

  return profile;
}

namespace {

// Per-channel sampling helpers.
inline QRgba64 sampleRgbx64(const QRgba64* bits, int w, int h, float x,
                            float y) {
  x = std::clamp(x, 0.0f, float(w - 1));
  y = std::clamp(y, 0.0f, float(h - 1));
  const int x0 = int(x);
  const int y0 = int(y);
  const int x1 = std::min(x0 + 1, w - 1);
  const int y1 = std::min(y0 + 1, h - 1);
  const float fx = x - float(x0);
  const float fy = y - float(y0);
  const QRgba64 p00 = bits[y0 * w + x0];
  const QRgba64 p10 = bits[y0 * w + x1];
  const QRgba64 p01 = bits[y1 * w + x0];
  const QRgba64 p11 = bits[y1 * w + x1];
  auto mix16 = [&](ushort a, ushort b, ushort c, ushort d) {
    const float top = a + (b - a) * fx;
    const float bottom = c + (d - c) * fx;
    return ushort(std::clamp(top + (bottom - top) * fy, 0.0f, 65535.0f));
  };
  QRgba64 out = QRgba64::fromRgba64(mix16(p00.red(), p10.red(), p01.red(), p11.red()),
                                    mix16(p00.green(), p10.green(), p01.green(), p11.green()),
                                    mix16(p00.blue(), p10.blue(), p01.blue(), p11.blue()),
                                    65535);
  return out;
}

inline void sampleRgb888(const uchar* bits, int w, int h, int bytesPerLine,
                         float x, float y, int component, float& out) {
  x = std::clamp(x, 0.0f, float(w - 1));
  y = std::clamp(y, 0.0f, float(h - 1));
  const int x0 = int(x);
  const int y0 = int(y);
  const int x1 = std::min(x0 + 1, w - 1);
  const int y1 = std::min(y0 + 1, h - 1);
  const float fx = x - float(x0);
  const float fy = y - float(y0);
  auto at = [&](int px, int py) {
    return float(bits[py * bytesPerLine + px * 3 + component]);
  };
  const float top = at(x0, y0) + (at(x1, y0) - at(x0, y0)) * fx;
  const float bottom = at(x0, y1) + (at(x1, y1) - at(x0, y1)) * fx;
  out = top + (bottom - top) * fy;
}

float distortionRatio(const Profile& p, float r) {
  const float k1 = p.distortionK[0];
  const float k2 = p.distortionK[1];
  const float k3 = p.distortionK[2];
  if (p.distortionModel == QLatin1String("poly3")) {
    return 1.0f + k1 * r * r;
  }
  if (p.distortionModel == QLatin1String("poly5")) {
    const float r2 = r * r;
    return 1.0f + k1 * r2 + k2 * r2 * r2;
  }
  if (p.distortionModel == QLatin1String("ptlens")) {
    return k1 * r * r * r + k2 * r * r + k3 * r + 1.0f;
  }
  return 1.0f;
}

float tcaRatio(const float terms[3], const QString& model, float r) {
  if (model == QLatin1String("poly3")) {
    return terms[0] + terms[1] * r + terms[2] * r * r;
  }
  return terms[0];
}

}  // namespace

QImage applyProfile(const QImage& src, const Profile& profile,
                    float distortionAmount, float caAmount,
                    float vignettingAmount) {
  if (!profile.valid || src.isNull()) return src;

  const bool doDistortion =
      distortionAmount > 0.5f && !profile.distortionModel.isEmpty();
  const bool doTca = caAmount > 0.5f && profile.hasTca;
  const bool doVignetting =
      vignettingAmount > 0.5f && profile.hasVignetting;
  if (!doDistortion && !doTca && !doVignetting) return src;

  const QImage source =
      src.format() == QImage::Format_RGB888 || src.format() == QImage::Format_RGBX64
          ? src
          : src.convertToFormat(QImage::Format_RGBX64);
  const int w = source.width();
  const int h = source.height();
  if (w < 2 || h < 2) return src;

  const float cx = w * 0.5f - 0.5f;
  const float cy = h * 0.5f - 0.5f;
  const float halfShort = std::min(w, h) * 0.5f;
  const float halfDiag = std::hypot(float(w), float(h)) * 0.5f;

  const float distAmount = std::clamp(distortionAmount, 0.0f, 100.0f) / 100.0f;
  const float caDiv = std::clamp(caAmount, 0.0f, 100.0f) / 100.0f;
  const float vigAmount = std::clamp(vignettingAmount, 0.0f, 100.0f) / 100.0f;

  // Auto-scale so a correction never samples beyond the source bounds.
  float zoom = 1.0f;
  if (doDistortion) {
    const float dx = float(w) * 0.5f;
    const float dy = float(h) * 0.5f;
    const float cornerR = std::hypot(dx, dy) / halfShort;
    const float samples[5] = {1.0f, dx / halfShort, dy / halfShort, cornerR,
                              0.0f};
    for (const float r : samples) {
      const float ratio = 1.0f + (distortionRatio(profile, r) - 1.0f) * distAmount;
      zoom = std::max(zoom, ratio);
    }
  }

  QImage output(source.size(), source.format());
  const bool is16 = source.format() == QImage::Format_RGBX64;
  const QRgba64* srcBits16 =
      is16 ? reinterpret_cast<const QRgba64*>(source.constBits()) : nullptr;
  const uchar* srcBits8 = is16 ? nullptr : source.constBits();
  const int srcStride8 = source.bytesPerLine();

  QVector<int> rows(h);
  std::iota(rows.begin(), rows.end(), 0);
  QtConcurrent::blockingMap(rows, [&](int y) {
    uchar* dstRow = output.scanLine(y);
    for (int x = 0; x < w; ++x) {
      const float dx = (float(x) + 0.5f) - cx;
      const float dy = (float(y) + 0.5f) - cy;
      const float radius = std::hypot(dx, dy) / halfShort;
      float scale = 1.0f;
      if (doDistortion) {
        const float ratio =
            1.0f + (distortionRatio(profile, radius) - 1.0f) * distAmount;
        scale = ratio / zoom;
      }
      float scaleR = scale;
      float scaleB = scale;
      if (doTca) {
        scaleR *= 1.0f +
                  (tcaRatio(profile.tcaR, profile.tcaModel, radius) - 1.0f) *
                      caDiv;
        scaleB *= 1.0f +
                  (tcaRatio(profile.tcaB, profile.tcaModel, radius) - 1.0f) *
                      caDiv;
      }

      float vigGain = 1.0f;
      if (doVignetting) {
        const float r = radius * (halfShort / halfDiag);
        const float r2 = r * r;
        const float r4 = r2 * r2;
        const float r6 = r4 * r2;
        const float response = 1.0f + profile.vignetting[0] * r2 +
                               profile.vignetting[1] * r4 +
                               profile.vignetting[2] * r6;
        // The calibration describes the lens response; the correction is the
        // inverse, blended with unity by the amount.
        const float gain = response > 1e-4f ? 1.0f / response : 1.0f;
        vigGain = 1.0f + (gain - 1.0f) * vigAmount;
      }

      if (is16) {
        const QRgba64 r =
            sampleRgbx64(srcBits16, w, h, cx + dx * scaleR, cy + dy * scaleR);
        const QRgba64 g =
            sampleRgbx64(srcBits16, w, h, cx + dx * scale, cy + dy * scale);
        const QRgba64 b =
            sampleRgbx64(srcBits16, w, h, cx + dx * scaleB, cy + dy * scaleB);
        auto applyVig = [&](ushort v) {
          return ushort(std::clamp(float(v) * vigGain, 0.0f, 65535.0f));
        };
        reinterpret_cast<QRgba64*>(dstRow)[x] = QRgba64::fromRgba64(
            applyVig(r.red()), applyVig(g.green()), applyVig(b.blue()), 65535);
      } else {
        float r = 0.0f, g = 0.0f, b = 0.0f;
        sampleRgb888(srcBits8, w, h, srcStride8, cx + dx * scaleR,
                     cy + dy * scaleR, 0, r);
        sampleRgb888(srcBits8, w, h, srcStride8, cx + dx * scale, cy + dy * scale,
                     1, g);
        sampleRgb888(srcBits8, w, h, srcStride8, cx + dx * scaleB,
                     cy + dy * scaleB, 2, b);
        dstRow[x * 3 + 0] =
            uchar(std::clamp(r * vigGain, 0.0f, 255.0f) + 0.5f);
        dstRow[x * 3 + 1] =
            uchar(std::clamp(g * vigGain, 0.0f, 255.0f) + 0.5f);
        dstRow[x * 3 + 2] =
            uchar(std::clamp(b * vigGain, 0.0f, 255.0f) + 0.5f);
      }
    }
  });

  return output;
}

}  // namespace lensfun
}  // namespace photon
