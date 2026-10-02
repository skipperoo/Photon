#include <cmath>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QtTest>
#include <vector>

#include "BitmapTestUtils.h"
#include "ExportManager.h"
#include "FileScanner.h"
#include "ImageDeveloper.h"
#include "PreviewManager.h"
#include "RawEngine.h"

class TestRawEngine : public QObject {
  Q_OBJECT

 private slots:
  void testLoadInvalidFile();
  void testLoadValidFile();
  void testProperties();
  void testPhoton001MultipassUsesFloatTargets();
  void testPhoton001ToneRangesReferenceBehavior();
  void testOklabCreativeOps();
  void testToneCurveLumaMatchesReferenceMapping();
  void testSwitchingSourceResetsExposureAndContrast();
  void testApplyGeometryTransformsStraightenKeepsFullFrame();
  void testApplyGeometryTransformsCropRectOnRotatedFrame();
  void testApplyGeometryTransformsCropPreservesAspectAndFocus();
  void testBitmapJpegLoad();
  void testBitmapTiffLoadAndGeometryBake();
  void testFileScannerIncludesBitmaps();
  void testBitmapExport();
  void testBitmapPreview();
  void testProcessedImageConcurrentSourceSwitch();
  void testProfileAndLegacyMigration();
  void testBlackAndWhiteCpuDevelop();
};

namespace {
float srgbToLinear(float c) {
  return (c <= 0.04045f) ? (c / 12.92f)
                         : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

QJsonArray makeCurve(const std::vector<std::pair<double, double>>& pts) {
  QJsonArray arr;
  for (const auto& [x, y] : pts) {
    QJsonObject p;
    p["x"] = x;
    p["y"] = y;
    arr.append(p);
  }
  return arr;
}
}  // namespace

void TestRawEngine::testLoadInvalidFile() {
  RawEngine engine;
  QSignalSpy errorSpy(&engine, &RawEngine::errorOccurred);
  engine.setSource("non_existent.arw");

  // Wait for the async load to finish (it should fail)
  QTRY_VERIFY_WITH_TIMEOUT(errorSpy.count() > 0, 5000);
}

void TestRawEngine::testLoadValidFile() {
  // Skip for now if we don't have a valid RAW file for testing
  // QSKIP("No valid RAW file available for testing");
}

void TestRawEngine::testProperties() {
  RawEngine engine;
  
  QSignalSpy exposureSpy(&engine, &RawEngine::exposureChanged);
  engine.setExposure(1.5f);
  QCOMPARE(engine.exposure(), 1.5f);
  QCOMPARE(exposureSpy.count(), 1);

  QSignalSpy tempSpy(&engine, &RawEngine::temperatureChanged);
  engine.setTemperature(50.0f);
  QCOMPARE(engine.temperature(), 50.0f);
  QCOMPARE(tempSpy.count(), 1);

  QSignalSpy tintSpy(&engine, &RawEngine::tintChanged);
  engine.setTint(-10.0f);
  QCOMPARE(engine.tint(), -10.0f);
  QCOMPARE(tintSpy.count(), 1);

  QSignalSpy profileSpy(&engine, &RawEngine::profileChanged);
  engine.setProfile("agx");
  QCOMPARE(engine.profile(), QString("agx"));
  QCOMPARE(profileSpy.count(), 1);
  engine.setProfile("blackAndWhite");
  QCOMPARE(engine.profile(), QString("black_and_white"));
  QCOMPARE(profileSpy.count(), 2);
  engine.setProfile("black_and_white");
  QCOMPARE(profileSpy.count(), 2);
  QCOMPARE(engine.profileIndex(), 2);

  const QVariantList profileOptions = engine.profileOptions();
  QCOMPARE(profileOptions.size(), 3);
  QCOMPARE(profileOptions.at(0).toMap().value("value").toString(),
           QString("normal"));
  QCOMPARE(profileOptions.at(0).toMap().value("label").toString(),
           QString("Normal"));
  QCOMPARE(profileOptions.at(2).toMap().value("value").toString(),
           QString("black_and_white"));
  QCOMPARE(profileOptions.at(2).toMap().value("label").toString(),
           QString("Black & White"));

  QSignalSpy mixSpy(&engine, &RawEngine::bwMixRedChanged);
  engine.setBwMixRed(40.0f);
  QCOMPARE(engine.bwMixRed(), 40.0f);
  QCOMPARE(mixSpy.count(), 1);

  QSignalSpy grainSpy(&engine, &RawEngine::grainAmountChanged);
  engine.setGrainAmount(25.0f);
  QCOMPARE(engine.grainAmount(), 25.0f);
  QCOMPARE(grainSpy.count(), 1);

  QSignalSpy vignetteSpy(&engine, &RawEngine::vignetteAmountChanged);
  engine.setVignetteAmount(-50.0f);
  QCOMPARE(engine.vignetteAmount(), -50.0f);
  QCOMPARE(vignetteSpy.count(), 1);
}

void TestRawEngine::testPhoton001MultipassUsesFloatTargets() {
  QString appQmlPath = QFINDTESTDATA("../../content/views/App.qml");
  if (appQmlPath.isEmpty()) {
    appQmlPath = QDir::cleanPath(
        QCoreApplication::applicationDirPath() + "/../../content/views/App.qml");
  }
  QVERIFY2(!appQmlPath.isEmpty(), "Could not locate content/views/App.qml");

  QFile qmlFile(appQmlPath);
  QVERIFY2(qmlFile.open(QIODevice::ReadOnly | QIODevice::Text),
           "Failed to open content/views/App.qml");
  const QString qml = QString::fromUtf8(qmlFile.readAll());

  auto verifySourceUsesFloatTarget = [&](const QString& sourceItem) {
    const QString sourceToken = QStringLiteral("sourceItem: %1").arg(sourceItem);
    const int sourceIdx = qml.indexOf(sourceToken);
    QVERIFY2(sourceIdx >= 0, qPrintable(QString("Missing '%1'").arg(sourceToken)));

    const int formatIdx = qml.indexOf("format: ShaderEffectSource.RGBA16F", sourceIdx);
    QVERIFY2(formatIdx > sourceIdx,
             qPrintable(QString("Missing float format near '%1'").arg(sourceToken)));

    const int nextSourceIdx = qml.indexOf("sourceItem:", sourceIdx + sourceToken.size());
    QVERIFY2(nextSourceIdx < 0 || formatIdx < nextSourceIdx,
             qPrintable(QString("Float format not scoped to '%1' block").arg(sourceToken)));
  };

  verifySourceUsesFloatTarget("photon001GaussianSmallH");
  verifySourceUsesFloatTarget("photon001GaussianSmallV");
  verifySourceUsesFloatTarget("photon001GaussianBigH");
  verifySourceUsesFloatTarget("photon001GaussianBigV");
  verifySourceUsesFloatTarget("photon001ColorFineV");
  verifySourceUsesFloatTarget("photon001ColorCoarseV");
  verifySourceUsesFloatTarget("photon001LogPass");
  verifySourceUsesFloatTarget("photon001DeltaPass");
}

void TestRawEngine::testPhoton001ToneRangesReferenceBehavior() {
  constexpr int w = 64;
  constexpr int h = 16;

  auto linearToSrgb16 = [](float linear) {
    const float s = linear <= 0.0031308f
                        ? linear * 12.92f
                        : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
    return ushort(std::clamp(s, 0.0f, 1.0f) * 65535.0f + 0.5f);
  };

  std::vector<ushort> src(w * h * 3);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const float linear = 0.002f + 0.998f * float(x) / float(w - 1);
      const ushort v = linearToSrgb16(linear);
      const int i = (y * w + x) * 3;
      src[i] = v;
      src[i + 1] = v;
      src[i + 2] = v;
    }
  }

  auto avgLuma = [](const QImage& img) {
    double sum = 0.0;
    for (int y = 0; y < img.height(); ++y) {
      const uchar* scan = img.constScanLine(y);
      for (int x = 0; x < img.width(); ++x) {
        sum += srgbToLinear(scan[x * 3 + 0] / 255.0f) * 0.2126 +
               srgbToLinear(scan[x * 3 + 1] / 255.0f) * 0.7152 +
               srgbToLinear(scan[x * 3 + 2] / 255.0f) * 0.0722;
      }
    }
    return float(sum / double(img.width() * img.height()));
  };

  QJsonObject base;
  base["contrast"] = 1.0;
  base["profile"] = "normal";
  base["denoiseEnabled"] = false;
  base["sceneWhite"] = 1.0;

  const QImage ref = photon::ImageDeveloper::develop(src.data(), w, h, base);
  QVERIFY(!ref.isNull());
  const float refLuma = avgLuma(ref);

  QVERIFY(std::abs(refLuma - 0.5f) < 0.02f);

  auto maxLumaDiff = [](const QImage& a, const QImage& b) {
    float maxDiff = 0.0f;
    for (int y = 0; y < a.height(); ++y) {
      const uchar* scanA = a.constScanLine(y);
      const uchar* scanB = b.constScanLine(y);
      for (int x = 0; x < a.width(); ++x) {
        for (int c = 0; c < 3; ++c) {
          const float la = srgbToLinear(scanA[x * 3 + c] / 255.0f);
          const float lb = srgbToLinear(scanB[x * 3 + c] / 255.0f);
          maxDiff = std::max(maxDiff, std::abs(la - lb));
        }
      }
    }
    return maxDiff;
  };

  auto imageWith = [&](const char* key, double value) {
    QJsonObject settings = base;
    settings[key] = value;
    return photon::ImageDeveloper::develop(src.data(), w, h, settings);
  };

  const char* const sliders[] = {"shadows", "highlights", "whites"};
  for (const char* key : sliders) {
    const QImage up = imageWith(key, 100.0);
    const QImage down = imageWith(key, -100.0);
    QVERIFY2(maxLumaDiff(up, ref) > 0.005f, key);
    QVERIFY2(maxLumaDiff(down, ref) > 0.005f, key);
    QVERIFY2(maxLumaDiff(up, down) > 0.005f, key);
  }

  // Shadows must not lift the highlights (cross-talk regression).
  const QImage shadowsUp = imageWith("shadows", 100.0);
  float brightDiff = 0.0f;
  const int brightStart = (w * 3) / 4;
  for (int y = 0; y < h; ++y) {
    const uchar* scanRef = ref.constScanLine(y);
    const uchar* scanUp = shadowsUp.constScanLine(y);
    for (int x = brightStart; x < w; ++x) {
      for (int c = 0; c < 3; ++c) {
        const float a = srgbToLinear(scanRef[x * 3 + c] / 255.0f);
        const float b = srgbToLinear(scanUp[x * 3 + c] / 255.0f);
        brightDiff = std::max(brightDiff, std::abs(a - b));
      }
    }
  }
  QVERIFY(brightDiff < 0.005f);

  // Whites must move the white point (bright region) in both directions.
  auto bandMean = [&](const QImage& img, int x0, int x1) {
    double sum = 0.0;
    int n = 0;
    for (int y = 0; y < h; ++y) {
      const uchar* scan = img.constScanLine(y);
      for (int x = x0; x < x1; ++x) {
        sum += srgbToLinear(scan[x * 3 + 0] / 255.0f) * 0.2126 +
               srgbToLinear(scan[x * 3 + 1] / 255.0f) * 0.7152 +
               srgbToLinear(scan[x * 3 + 2] / 255.0f) * 0.0722;
        ++n;
      }
    }
    return float(sum / double(n));
  };
  auto bandStd = [&](const QImage& img, int x0, int x1) {
    const float mean = bandMean(img, x0, x1);
    double sum = 0.0;
    int n = 0;
    for (int y = 0; y < h; ++y) {
      const uchar* scan = img.constScanLine(y);
      for (int x = x0; x < x1; ++x) {
        const float luma =
            srgbToLinear(scan[x * 3 + 0] / 255.0f) * 0.2126 +
            srgbToLinear(scan[x * 3 + 1] / 255.0f) * 0.7152 +
            srgbToLinear(scan[x * 3 + 2] / 255.0f) * 0.0722;
        sum += double(luma - mean) * double(luma - mean);
        ++n;
      }
    }
    return float(std::sqrt(sum / double(n)));
  };
  {
    const float brightRef = bandMean(ref, brightStart, w);
    QVERIFY(bandMean(imageWith("whites", 100.0), brightStart, w) >
            brightRef + 0.005f);
    QVERIFY(bandMean(imageWith("whites", -100.0), brightStart, w) <
            brightRef - 0.005f);
  }

  // Window targeting: blacks must stay in the deepest tones and not behave
  // like shadows.
  {
    const QImage blacksUp = imageWith("blacks", 100.0);
    const float deepRef = bandMean(ref, 0, 2);
    const float deepBlacks = bandMean(blacksUp, 0, 2);
    QVERIFY(std::abs(deepBlacks - deepRef) > 0.0002f);
    QVERIFY(std::abs(bandMean(blacksUp, 4, 12) - bandMean(ref, 4, 12)) <
            0.001f);
  }

  // Shadows must reach deep shadows but leave mid/bright tones alone.
  {
    const float deepMidRef = bandMean(ref, 1, 5);
    QVERIFY(std::abs(bandMean(shadowsUp, 1, 5) - deepMidRef) > 0.002f);
  }

  // Exposure must brighten progressively without crushing shadow detail, and
  // the positive half must be tapered (less sensitive) rather than raw 2^EV.
  {
    const float darkRef = bandMean(ref, 0, 16);
    float prevDark = darkRef;
    float prevMid = bandMean(ref, 24, 40);
    const double exposures[] = {0.15, 0.3, 0.5};
    for (double e : exposures) {
      QJsonObject settings = base;
      settings["exposure"] = e;
      const QImage out =
          photon::ImageDeveloper::develop(src.data(), w, h, settings);
      const float dark = bandMean(out, 0, 16);
      const float mid = bandMean(out, 24, 40);
      QVERIFY2(dark > prevDark, "exposure must not crush shadows");
      QVERIFY2(mid > prevMid, "exposure must be monotonic");
      QVERIFY2(dark < darkRef * 3.0f, "exposure must stay sane");
      prevDark = dark;
      prevMid = mid;
    }

    const float midRef = bandMean(ref, 24, 40);
    QJsonObject oneStop = base;
    oneStop["exposure"] = 1.0;
    const QImage out =
        photon::ImageDeveloper::develop(src.data(), w, h, oneStop);
    const float gain = bandMean(out, 24, 40) / midRef;
    QVERIFY2(gain > 1.4f, "positive exposure must still brighten");
    QVERIFY2(gain < 1.95f, "positive exposure must be tapered below 2^EV");

    // Highlights must keep a usable control range: no washed-out plateau at
    // moderate positive exposure and no blown white.
    const float brightRef = bandMean(ref, brightStart, w);
    const float stdRef = bandStd(ref, brightStart, w);
    const float brightOut = bandMean(out, brightStart, w);
    QVERIFY2(brightOut > brightRef + 0.005f, "highlights must brighten");
    QVERIFY2(brightOut < 0.985f, "highlights must not blow out at +1 EV");
    QVERIFY2(bandStd(out, brightStart, w) > 0.15f * stdRef,
             "highlight detail must not wash out");
  }

  auto meanAbsLumaDiff = [](const QImage& a, const QImage& b) {
    double sum = 0.0;
    for (int y = 0; y < a.height(); ++y) {
      const uchar* scanA = a.constScanLine(y);
      const uchar* scanB = b.constScanLine(y);
      for (int x = 0; x < a.width(); ++x) {
        for (int c = 0; c < 3; ++c) {
          const float la = srgbToLinear(scanA[x * 3 + c] / 255.0f);
          const float lb = srgbToLinear(scanB[x * 3 + c] / 255.0f);
          sum += std::abs(la - lb);
        }
      }
    }
    return float(sum / double(a.width() * a.height() * 3));
  };

  const QImage clarityUp = imageWith("clarity", 100.0);
  const QImage clarityDown = imageWith("clarity", -100.0);
  QVERIFY(maxLumaDiff(clarityUp, ref) > 0.005f);
  QVERIFY(maxLumaDiff(clarityDown, ref) > 0.005f);
  QVERIFY(maxLumaDiff(clarityUp, clarityDown) > 0.005f);

  const float upDiff = meanAbsLumaDiff(clarityUp, ref);
  const float downDiff = meanAbsLumaDiff(clarityDown, ref);
  QVERIFY(upDiff > 0.001f);
  QVERIFY(downDiff > 0.001f);
  QVERIFY(upDiff < 2.0f * downDiff);
  QVERIFY(downDiff < 2.0f * upDiff);
}

void TestRawEngine::testOklabCreativeOps() {
  auto oklabHueDeg = [](float r, float g, float b) {
    const float l = 0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b;
    const float m = 0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b;
    const float s = 0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b;
    const float l3 = std::cbrt(l);
    const float m3 = std::cbrt(m);
    const float s3 = std::cbrt(s);
    const float A =
        1.9779984951f * l3 - 2.4285922050f * m3 + 0.4505937099f * s3;
    const float B =
        0.0259040371f * l3 + 0.7827717662f * m3 - 0.8086757660f * s3;
    return std::atan2(B, A) * 180.0f / 3.14159265359f;
  };

  QJsonObject base;
  base["contrast"] = 1.0;
  base["profile"] = "normal";
  base["denoiseEnabled"] = false;
  base["sceneWhite"] = 1.0;

  // Muted green patch (has gamut headroom for chroma/lightness changes).
  constexpr int w = 8;
  constexpr int h = 8;
  std::vector<ushort> patch(w * h * 3);
  const ushort pr = 100 * 257;
  const ushort pg = 180 * 257;
  const ushort pb = 80 * 257;
  for (int i = 0; i < w * h; ++i) {
    patch[i * 3 + 0] = pr;
    patch[i * 3 + 1] = pg;
    patch[i * 3 + 2] = pb;
  }
  const float baseHue =
      oklabHueDeg(srgbToLinear(pr / 65535.0f), srgbToLinear(pg / 65535.0f),
                  srgbToLinear(pb / 65535.0f));

  auto centerHue = [&](const QImage& img) {
    const uchar* scan = img.constScanLine(h / 2);
    return oklabHueDeg(srgbToLinear(scan[(w / 2) * 3 + 0] / 255.0f),
                       srgbToLinear(scan[(w / 2) * 3 + 1] / 255.0f),
                       srgbToLinear(scan[(w / 2) * 3 + 2] / 255.0f));
  };

  QJsonObject lum = base;
  lum["hslGreenLuminance"] = 40.0;
  QVERIFY(std::abs(centerHue(
              photon::ImageDeveloper::develop(patch.data(), w, h, lum)) -
              baseHue) < 4.0f);

  QJsonObject sat = base;
  sat["hslGreenSaturation"] = 40.0;
  QVERIFY(std::abs(centerHue(
              photon::ImageDeveloper::develop(patch.data(), w, h, sat)) -
              baseHue) < 4.0f);

  // Saturated yellow at extreme lightness must keep its hue: the OKLab
  // conversion gamut-maps chroma instead of clipping channels.
  std::vector<ushort> yellow(w * h * 3);
  const ushort yr = 230 * 257;
  const ushort yg = 200 * 257;
  const ushort yb = 40 * 257;
  for (int i = 0; i < w * h; ++i) {
    yellow[i * 3 + 0] = yr;
    yellow[i * 3 + 1] = yg;
    yellow[i * 3 + 2] = yb;
  }
  const float yellowHue =
      oklabHueDeg(srgbToLinear(yr / 65535.0f), srgbToLinear(yg / 65535.0f),
                  srgbToLinear(yb / 65535.0f));

  auto yellowHueOut = [&](const QJsonObject& settings) {
    const QImage img =
        photon::ImageDeveloper::develop(yellow.data(), w, h, settings);
    const uchar* scan = img.constScanLine(h / 2);
    return oklabHueDeg(srgbToLinear(scan[(w / 2) * 3 + 0] / 255.0f),
                       srgbToLinear(scan[(w / 2) * 3 + 1] / 255.0f),
                       srgbToLinear(scan[(w / 2) * 3 + 2] / 255.0f));
  };
  QJsonObject yUp = base;
  yUp["hslYellowLuminance"] = 80.0;
  QVERIFY(std::abs(yellowHueOut(yUp) - yellowHue) < 5.0f);
  QJsonObject yDown = base;
  yDown["hslYellowLuminance"] = -80.0;
  QVERIFY(std::abs(yellowHueOut(yDown) - yellowHue) < 5.0f);

  // Relative chroma must stay proportional to lightness under HSL lightness
  // changes: otherwise colours leave the gamut and clip into noise.
  auto labLC = [](float r, float g, float b, float& L, float& C) {
    const float l = 0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b;
    const float m = 0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b;
    const float s = 0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b;
    const float l3 = std::cbrt(l);
    const float m3 = std::cbrt(m);
    const float s3 = std::cbrt(s);
    L = 0.2104542553f * l3 + 0.7936177850f * m3 - 0.0040720468f * s3;
    const float A =
        1.9779984951f * l3 - 2.4285922050f * m3 + 0.4505937099f * s3;
    const float B =
        0.0259040371f * l3 + 0.7827717662f * m3 - 0.8086757660f * s3;
    C = std::sqrt(A * A + B * B);
  };
  float inL = 0.0f, inC = 0.0f;
  labLC(srgbToLinear(yr / 65535.0f), srgbToLinear(yg / 65535.0f),
        srgbToLinear(yb / 65535.0f), inL, inC);

  const QImage yellowDark =
      photon::ImageDeveloper::develop(yellow.data(), w, h, yDown);
  const uchar* ydScan = yellowDark.constScanLine(h / 2);
  float outL = 0.0f, outC = 0.0f;
  labLC(srgbToLinear(ydScan[(w / 2) * 3 + 0] / 255.0f),
        srgbToLinear(ydScan[(w / 2) * 3 + 1] / 255.0f),
        srgbToLinear(ydScan[(w / 2) * 3 + 2] / 255.0f), outL, outC);
  QVERIFY(outC > 0.0f && outL > 0.0f);
  const float ratioIn = inC / inL;
  const float ratioOut = outC / outL;
  QVERIFY(std::abs(ratioOut / ratioIn - 1.0f) < 0.15f);

  // Perceptual contrast S-curve: darkens below mid, brightens above, keeps
  // mid-gray roughly in place.
  constexpr int cw = 32;
  constexpr int ch = 8;
  auto srgb16 = [](float linear) {
    const float s = linear <= 0.0031308f
                        ? linear * 12.92f
                        : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
    return ushort(std::clamp(s, 0.0f, 1.0f) * 65535.0f + 0.5f);
  };
  std::vector<ushort> grad(cw * ch * 3);
  for (int y = 0; y < ch; ++y) {
    for (int x = 0; x < cw; ++x) {
      float v = 0.5f;
      if (x < 8) v = 0.08f;
      else if (x >= 24) v = 0.85f;
      const ushort sv = srgb16(v);
      const int i = (y * cw + x) * 3;
      grad[i] = grad[i + 1] = grad[i + 2] = sv;
    }
  }
  auto bandLuma = [&](const QImage& img, int x0, int x1) {
    double sum = 0.0;
    int n = 0;
    for (int y = 0; y < ch; ++y) {
      const uchar* scan = img.constScanLine(y);
      for (int x = x0; x < x1; ++x) {
        sum += 0.2126 * srgbToLinear(scan[x * 3 + 0] / 255.0f) +
               0.7152 * srgbToLinear(scan[x * 3 + 1] / 255.0f) +
               0.0722 * srgbToLinear(scan[x * 3 + 2] / 255.0f);
        ++n;
      }
    }
    return float(sum / double(n));
  };

  const QImage cRef = photon::ImageDeveloper::develop(grad.data(), cw, ch, base);
  QJsonObject cHigh = base;
  cHigh["contrast"] = 1.5;
  const QImage cOut =
      photon::ImageDeveloper::develop(grad.data(), cw, ch, cHigh);
  QVERIFY(bandLuma(cOut, 0, 8) < bandLuma(cRef, 0, 8) - 0.005f);
  QVERIFY(bandLuma(cOut, 24, 32) > bandLuma(cRef, 24, 32) + 0.005f);
  QVERIFY(std::abs(bandLuma(cOut, 10, 22) - bandLuma(cRef, 10, 22)) < 0.06f);
}

void TestRawEngine::testToneCurveLumaMatchesReferenceMapping() {
  constexpr int w = 32;
  constexpr int h = 32;
  constexpr ushort inR = 10000;
  constexpr ushort inG = 5000;
  constexpr ushort inB = 2000;

  std::vector<ushort> src(w * h * 3);
  for (int i = 0; i < w * h; ++i) {
    src[i * 3 + 0] = inR;
    src[i * 3 + 1] = inG;
    src[i * 3 + 2] = inB;
  }

  QJsonObject settings;
  settings["contrast"] = 1.0;
  settings["profile"] = "normal";
  settings["denoiseEnabled"] = false;
  settings["toneCurveLuma"] = makeCurve({{0.0, 0.1}, {1.0, 1.0}});
  settings["toneCurveRed"] = makeCurve({{0.0, 0.0}, {1.0, 1.0}});
  settings["toneCurveGreen"] = makeCurve({{0.0, 0.0}, {1.0, 1.0}});
  settings["toneCurveBlue"] = makeCurve({{0.0, 0.0}, {1.0, 1.0}});

  QImage out = photon::ImageDeveloper::develop(src.data(), w, h, settings);
  QVERIFY(!out.isNull());

  double sumR = 0.0;
  double sumG = 0.0;
  double sumB = 0.0;
  for (int y = 0; y < out.height(); ++y) {
    const uchar* scan = out.constScanLine(y);
    for (int x = 0; x < out.width(); ++x) {
      sumR += srgbToLinear(scan[x * 3 + 0] / 255.0f);
      sumG += srgbToLinear(scan[x * 3 + 1] / 255.0f);
      sumB += srgbToLinear(scan[x * 3 + 2] / 255.0f);
    }
  }

  const double invN = 1.0 / double(out.width() * out.height());
  const double avgR = sumR * invN;
  const double avgG = sumG * invN;
  const double avgB = sumB * invN;

  const double inLinR = srgbToLinear(inR / 65535.0f);
  const double inLinG = srgbToLinear(inG / 65535.0f);
  const double inLinB = srgbToLinear(inB / 65535.0f);

  // Reference shaderTemplate behavior for a linear luma curve y = 0.1 + 0.9x:
  // map min/max through the same curve and reproject channels.
  const double expectedR = 0.1 + 0.9 * inLinR;
  const double expectedG = 0.1 + 0.9 * inLinG;
  const double expectedB = 0.1 + 0.9 * inLinB;

  QVERIFY(std::abs(avgR - expectedR) < 0.015);
  QVERIFY(std::abs(avgG - expectedG) < 0.015);
  QVERIFY(std::abs(avgB - expectedB) < 0.015);
}

void TestRawEngine::testSwitchingSourceResetsExposureAndContrast() {
  QTemporaryDir tempDir;
  QVERIFY(tempDir.isValid());

  RawEngine engine;
  QSignalSpy exposureSpy(&engine, &RawEngine::exposureChanged);
  QSignalSpy contrastSpy(&engine, &RawEngine::contrastChanged);

  engine.setSource(tempDir.filePath("first.arw"));
  engine.setExposure(1.5f);
  engine.setContrast(1.3f);
  QCOMPARE(engine.exposure(), 1.5f);
  QCOMPARE(engine.contrast(), 1.3f);

  const int exposureSignalsBeforeSwitch = exposureSpy.count();
  const int contrastSignalsBeforeSwitch = contrastSpy.count();

  engine.setSource(tempDir.filePath("second.arw"));
  QCOMPARE(engine.exposure(), 0.0f);
  QCOMPARE(engine.contrast(), 1.0f);
  QVERIFY(exposureSpy.count() > exposureSignalsBeforeSwitch);
  QVERIFY(contrastSpy.count() > contrastSignalsBeforeSwitch);
}

void TestRawEngine::testApplyGeometryTransformsStraightenKeepsFullFrame() {
  QImage input(200, 100, QImage::Format_RGBA64);
  input.fill(Qt::black);

  const QImage output = RawEngine::applyGeometryTransforms(
      input, 0, false, false, 10.0, QRectF(0, 0, 1, 1));

  QVERIFY(output.width() > input.width());
  QVERIFY(output.height() > input.height());
}

void TestRawEngine::testApplyGeometryTransformsCropRectOnRotatedFrame() {
  QImage input(240, 120, QImage::Format_RGBA64);
  input.fill(Qt::black);

  const QImage full = RawEngine::applyGeometryTransforms(
      input, 0, false, false, 12.0, QRectF(0, 0, 1, 1));
  const QRectF cropRect(0.1, 0.2, 0.5, 0.4);
  const QImage cropped = RawEngine::applyGeometryTransforms(
      input, 0, false, false, 12.0, cropRect);

  const int expectedWidth = static_cast<int>(std::ceil((cropRect.x() + cropRect.width()) * full.width())) -
                            static_cast<int>(std::floor(cropRect.x() * full.width()));
  const int expectedHeight = static_cast<int>(std::ceil((cropRect.y() + cropRect.height()) * full.height())) -
                             static_cast<int>(std::floor(cropRect.y() * full.height()));
  QCOMPARE(cropped.width(), expectedWidth);
  QCOMPARE(cropped.height(), expectedHeight);
}

void TestRawEngine::testApplyGeometryTransformsCropPreservesAspectAndFocus() {
  QImage input(333, 211, QImage::Format_RGBA64);
  input.fill(Qt::black);

  const QRectF cropRect(0.123, 0.234, 0.456, 0.321);
  const QImage full = RawEngine::applyGeometryTransforms(
      input, 0, false, false, 17.0, QRectF(0, 0, 1, 1));
  const QImage cropped = RawEngine::applyGeometryTransforms(
      input, 0, false, false, 17.0, cropRect);

  const int left = static_cast<int>(std::floor(cropRect.x() * full.width()));
  const int right = static_cast<int>(std::ceil((cropRect.x() + cropRect.width()) * full.width()));
  const int top = static_cast<int>(std::floor(cropRect.y() * full.height()));
  const int bottom = static_cast<int>(std::ceil((cropRect.y() + cropRect.height()) * full.height()));
  const double expectedCenterX = 0.5 * (left + right);
  const double expectedCenterY = 0.5 * (top + bottom);
  const double actualCenterX = left + 0.5 * cropped.width();
  const double actualCenterY = top + 0.5 * cropped.height();
  QVERIFY(std::abs(actualCenterX - expectedCenterX) <= 0.5);
  QVERIFY(std::abs(actualCenterY - expectedCenterY) <= 0.5);

  const double expectedAspect =
      static_cast<double>(right - left) / static_cast<double>(bottom - top);
  const double actualAspect = static_cast<double>(cropped.width()) / cropped.height();
  QVERIFY(std::abs(actualAspect - expectedAspect) < 0.0001);
}

void TestRawEngine::testBitmapJpegLoad() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.filePath("photo.jpg");

  const int width = 8;
  const int height = 4;
  std::vector<uint8_t> pixels(size_t(width) * size_t(height) * 3);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const size_t base = (size_t(y) * size_t(width) + size_t(x)) * 3;
      pixels[base + 0] = uint8_t(20 * x);
      pixels[base + 1] = uint8_t(40 * y);
      pixels[base + 2] = 128;
    }
  }
  QVERIFY(BitmapTestUtils::writeJpegFile(path, width, height, pixels));

  RawEngine engine;
  QSignalSpy loadedSpy(&engine, &RawEngine::imageLoaded);
  QSignalSpy errorSpy(&engine, &RawEngine::errorOccurred);
  engine.setSource(path);
  QTRY_VERIFY_WITH_TIMEOUT(loadedSpy.count() > 0 || errorSpy.count() > 0, 30000);
  QCOMPARE(errorSpy.count(), 0);
  QVERIFY(!engine.isLoading());

  const QImage decodedImage = engine.getProcessedImage();
  QVERIFY(!decodedImage.isNull());
  QCOMPARE(decodedImage.width(), width);
  QCOMPARE(decodedImage.height(), height);
  QCOMPARE(decodedImage.format(), QImage::Format_RGBX64);

  QTRY_COMPARE(engine.metadata().value("lensModel").toString(),
               QString("Unknown Lens"));
  QVERIFY(engine.metadata().contains("make"));
}

void TestRawEngine::testBitmapTiffLoadAndGeometryBake() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.filePath("photo.tif");

  const int width = 6;
  const int height = 4;
  std::vector<uint16_t> pixels(size_t(width) * size_t(height) * 3);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const size_t base = (size_t(y) * size_t(width) + size_t(x)) * 3;
      const uint16_t value = uint16_t(1000 * (y * width + x) + 1);
      pixels[base + 0] = value;
      pixels[base + 1] = value;
      pixels[base + 2] = value;
    }
  }
  QVERIFY(BitmapTestUtils::writeTiff16File(path, width, height, pixels));

  RawEngine engine;
  QSignalSpy loadedSpy(&engine, &RawEngine::imageLoaded);
  engine.setSource(path);
  QTRY_VERIFY_WITH_TIMEOUT(loadedSpy.count() > 0, 30000);

  const QImage decodedImage = engine.getProcessedImage();
  QVERIFY(!decodedImage.isNull());
  QCOMPARE(decodedImage.width(), width);
  QCOMPARE(decodedImage.height(), height);
  QCOMPARE(decodedImage.format(), QImage::Format_RGBX64);
  const QRgba64 firstPixel =
      reinterpret_cast<const QRgba64*>(decodedImage.constBits())[0];
  QCOMPARE(firstPixel.red(), uint16_t(1));
  QCOMPARE(firstPixel.green(), uint16_t(1));

  // Crop mode must keep the decoded buffer alive for rendered formats.
  engine.enterCropMode();
  const QImage cropImage = engine.getProcessedImage();
  QVERIFY(!cropImage.isNull());
  QCOMPARE(cropImage.width(), width);
  QCOMPARE(cropImage.height(), height);

  // Bake a 90 degree rotation through the bitmap geometry path.
  engine.setOrientationSteps(1);
  engine.exitCropMode();
  QTRY_VERIFY_WITH_TIMEOUT(engine.geometryBaked(), 30000);

  const QImage rotatedImage = engine.getProcessedImage();
  QVERIFY(!rotatedImage.isNull());
  QCOMPARE(rotatedImage.width(), height);
  QCOMPARE(rotatedImage.height(), width);
  QCOMPARE(rotatedImage.format(), QImage::Format_RGBA64);
}

void TestRawEngine::testFileScannerIncludesBitmaps() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());

  std::vector<uint8_t> jpegPixels(3 * 3 * 3, 100);
  QVERIFY(BitmapTestUtils::writeJpegFile(dir.filePath("a.JPG"), 3, 3,
                                         jpegPixels));
  std::vector<uint16_t> tiffPixels(3 * 3 * 3, 4000);
  QVERIFY(
      BitmapTestUtils::writeTiff16File(dir.filePath("b.tiff"), 3, 3,
                                       tiffPixels));

  QFile textFile(dir.filePath("c.txt"));
  QVERIFY(textFile.open(QIODevice::WriteOnly));
  textFile.write("not an image");
  textFile.close();

  FileScanner scanner;
  const QVariantList files = scanner.scanForRawFiles(dir.path());
  QStringList names;
  for (const QVariant& file : files) {
    names << file.toMap().value("name").toString();
  }
  QVERIFY(names.contains("a.JPG"));
  QVERIFY(names.contains("b.tiff"));
  QVERIFY(!names.contains("c.txt"));
}

void TestRawEngine::testBitmapExport() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString sourcePath = dir.filePath("export_me.jpg");

  const int width = 16;
  const int height = 8;
  std::vector<uint8_t> pixels(size_t(width) * size_t(height) * 3);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const size_t base = (size_t(y) * size_t(width) + size_t(x)) * 3;
      pixels[base + 0] = uint8_t(15 * x);
      pixels[base + 1] = uint8_t(30 * y);
      pixels[base + 2] = 90;
    }
  }
  QVERIFY(BitmapTestUtils::writeJpegFile(sourcePath, width, height, pixels));

  const QString outputFolder = dir.filePath("out");
  photon::ExportManager exportManager;
  QSignalSpy finishedSpy(&exportManager,
                         &photon::ExportManager::exportFinished);
  exportManager.startExport({sourcePath}, outputFolder, "JPG", 90);
  QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() > 0, 60000);

  const QString outputPath = outputFolder + "/export_me.jpg";
  QVERIFY(QFile::exists(outputPath));
  const QImage exported(outputPath);
  QVERIFY(!exported.isNull());
  QCOMPARE(exported.width(), width);
  QCOMPARE(exported.height(), height);
}

void TestRawEngine::testBitmapPreview() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString sourcePath = dir.filePath("preview_me.jpg");

  const int width = 12;
  const int height = 6;
  std::vector<uint8_t> pixels(size_t(width) * size_t(height) * 3, 64);
  QVERIFY(BitmapTestUtils::writeJpegFile(sourcePath, width, height, pixels));

  photon::PreviewManager previewManager;
  QSignalSpy readySpy(&previewManager,
                      &photon::PreviewManager::previewReady);
  previewManager.refreshPreview(sourcePath);
  QTRY_VERIFY_WITH_TIMEOUT(readySpy.count() > 0, 60000);

  const QString cachePath = readySpy.first().at(1).toString();
  QVERIFY(QFile::exists(cachePath));
  const QImage cached(cachePath);
  QVERIFY(!cached.isNull());
  QCOMPARE(cached.width(), width);
  QCOMPARE(cached.height(), height);
}

void TestRawEngine::testProcessedImageConcurrentSourceSwitch() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString pathA = dir.filePath("a.tif");
  const QString pathB = dir.filePath("b.tif");

  const int widthA = 64;
  const int heightA = 48;
  const int widthB = 48;
  const int heightB = 64;
  std::vector<uint16_t> pixelsA(size_t(widthA) * size_t(heightA) * 3, 8000);
  std::vector<uint16_t> pixelsB(size_t(widthB) * size_t(heightB) * 3, 20000);
  QVERIFY(BitmapTestUtils::writeTiff16File(pathA, widthA, heightA, pixelsA));
  QVERIFY(BitmapTestUtils::writeTiff16File(pathB, widthB, heightB, pixelsB));

  RawEngine engine;
  QSignalSpy loadedSpy(&engine, &RawEngine::imageLoaded);
  engine.setSource(pathA);
  QTRY_VERIFY_WITH_TIMEOUT(loadedSpy.count() > 0, 30000);

  std::atomic<bool> stop{false};
  std::atomic<int> calls{0};
  std::atomic<int> failures{0};
  QFuture<void> worker = QtConcurrent::run([&]() {
    while (!stop.load()) {
      const QImage image = engine.getProcessedImage();
      if (!image.isNull()) {
        const bool validA = image.width() == widthA && image.height() == heightA;
        const bool validB = image.width() == widthB && image.height() == heightB;
        if (!validA && !validB) ++failures;
      }
      ++calls;
    }
  });

  for (int i = 0; i < 40; ++i) {
    engine.setSource(i % 2 == 0 ? pathB : pathA);
    QTest::qWait(5);
  }
  QTest::qWait(300);
  stop = true;
  worker.waitForFinished();

  QVERIFY(calls.load() > 0);
  QCOMPARE(failures.load(), 0);
  const QImage last = engine.getProcessedImage();
  QVERIFY(!last.isNull());
}

void TestRawEngine::testProfileAndLegacyMigration() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.filePath("legacy.jpg");
  std::vector<uint8_t> jpegPixels(3 * 3 * 3, 120);
  QVERIFY(BitmapTestUtils::writeJpegFile(path, 3, 3, jpegPixels));

  const QString editsDir = dir.filePath(".PhotonData/edits");
  QVERIFY(QDir().mkpath(editsDir));
  QFile sidecar(editsDir + "/legacy.jpg.json");
  QVERIFY(sidecar.open(QIODevice::WriteOnly));
  sidecar.write(R"([{"exposure": 0.0, "tonemappingEnabled": true}])");
  sidecar.close();

  RawEngine engine;
  QSignalSpy loadedSpy(&engine, &RawEngine::imageLoaded);
  engine.setSource(path);
  QTRY_VERIFY_WITH_TIMEOUT(loadedSpy.count() > 0, 30000);
  QCOMPARE(engine.profile(), QString("agx"));

  constexpr int w = 2;
  constexpr int h = 1;
  std::vector<ushort> src = {65535, 0, 0, 0, 0, 65535};

  QJsonObject legacy;
  legacy["tonemappingEnabled"] = true;
  legacy["sceneWhite"] = 1.0;
  legacy["denoiseEnabled"] = false;

  QJsonObject modern;
  modern["profile"] = "agx";
  modern["sceneWhite"] = 1.0;
  modern["denoiseEnabled"] = false;

  const QImage legacyOut =
      photon::ImageDeveloper::develop(src.data(), w, h, legacy);
  const QImage modernOut =
      photon::ImageDeveloper::develop(src.data(), w, h, modern);
  QVERIFY(!legacyOut.isNull());
  QCOMPARE(legacyOut, modernOut);
}

void TestRawEngine::testBlackAndWhiteCpuDevelop() {
  constexpr int w = 4;
  constexpr int h = 4;
  std::vector<ushort> src(size_t(w) * size_t(h) * 3);
  auto setPixel = [&](int px, ushort r, ushort g, ushort b) {
    const size_t base = size_t(px) * 3;
    src[base + 0] = r;
    src[base + 1] = g;
    src[base + 2] = b;
  };
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const int px = y * w + x;
      if (x < 2 && y < 2)
        setPixel(px, 65535, 0, 0);
      else if (x >= 2 && y < 2)
        setPixel(px, 0, 0, 65535);
      else if (x < 2 && y >= 2)
        setPixel(px, 21845, 21845, 21845);
      else
        setPixel(px, 0, 65535, 0);
    }
  }

  QJsonObject base;
  base["profile"] = "black_and_white";
  base["sceneWhite"] = 1.0;
  base["denoiseEnabled"] = false;

  const QImage out = photon::ImageDeveloper::develop(src.data(), w, h, base);
  QVERIFY(!out.isNull());
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const QRgb pixel = out.pixel(x, y);
      QCOMPARE(qRed(pixel), qGreen(pixel));
      QCOMPARE(qGreen(pixel), qBlue(pixel));
    }
  }

  auto averageLuma = [&](const QImage& image, int x0, int y0) {
    double sum = 0.0;
    for (int y = y0; y < y0 + 2; ++y) {
      for (int x = x0; x < x0 + 2; ++x) {
        sum += qGray(image.pixel(x, y));
      }
    }
    return sum / 4.0;
  };

  const double redBase = averageLuma(out, 0, 0);
  const double neutralBase = averageLuma(out, 0, 2);

  QJsonObject redUp = base;
  redUp["bwMixRed"] = 100.0;
  QJsonObject redDown = base;
  redDown["bwMixRed"] = -100.0;
  QJsonObject blueUp = base;
  blueUp["bwMixBlue"] = 100.0;

  const QImage up = photon::ImageDeveloper::develop(src.data(), w, h, redUp);
  const QImage down = photon::ImageDeveloper::develop(src.data(), w, h, redDown);
  const QImage blue = photon::ImageDeveloper::develop(src.data(), w, h, blueUp);
  QVERIFY(!up.isNull());
  QVERIFY(!down.isNull());
  QVERIFY(!blue.isNull());

  QVERIFY(averageLuma(up, 0, 0) > redBase + 5.0);
  QVERIFY(averageLuma(down, 0, 0) < redBase - 5.0);
  QVERIFY(std::abs(averageLuma(up, 0, 2) - neutralBase) < 1.0);
  QVERIFY(std::abs(averageLuma(blue, 0, 0) - redBase) < 2.0);

  QJsonObject legacyToken = base;
  legacyToken["profile"] = "blackAndWhite";
  const QImage legacyImage =
      photon::ImageDeveloper::develop(src.data(), w, h, legacyToken);
  QCOMPARE(legacyImage, out);
}

QTEST_MAIN(TestRawEngine)
#include "tst_RawEngine.moc"
