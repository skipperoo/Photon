#include <cmath>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QtTest>
#include <vector>

#include "ImageDeveloper.h"
#include "RawEngine.h"

class TestRawEngine : public QObject {
  Q_OBJECT

 private slots:
  void testLoadInvalidFile();
  void testLoadValidFile();
  void testProperties();
  void testPhoton001MultipassUsesFloatTargets();
  void testPhoton001ToneRangesReferenceBehavior();
  void testToneCurveLumaMatchesReferenceMapping();
  void testSwitchingSourceResetsExposureAndContrast();
  void testApplyGeometryTransformsStraightenKeepsFullFrame();
  void testApplyGeometryTransformsCropRectOnRotatedFrame();
  void testApplyGeometryTransformsCropPreservesAspectAndFocus();
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

  QSignalSpy toneSpy(&engine, &RawEngine::tonemappingEnabledChanged);
  engine.setTonemappingEnabled(true);
  QCOMPARE(engine.tonemappingEnabled(), true);
  QCOMPARE(toneSpy.count(), 1);

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
  base["tonemappingEnabled"] = false;
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
  settings["tonemappingEnabled"] = false;
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

QTEST_MAIN(TestRawEngine)
#include "tst_RawEngine.moc"
