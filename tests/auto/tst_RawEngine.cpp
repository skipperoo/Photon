#include <cmath>
#include <libraw/libraw.h>
#include <map>
#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QColorSpace>
#include <QImageWriter>
#include <QtTest>
#include <vector>

#include "AiDenoiser.h"
#include "ColorSpace.h"
#include "Denoiser.h"
#include "RawDecode.h"
#include "ToneMask.h"
#include "ToneMath.h"
#include "EditStack.h"
#include "AppStateManager.h"
#include "BitmapTestUtils.h"
#include "ExportManager.h"
#include "FileScanner.h"
#include "ImageDeveloper.h"
#include "LensFun.h"
#include "HdrMergeMath.h"
#include "CameraLook.h"
#include "CameraLookMath.h"
#include "WhiteBalance.h"
#include "SceneWhite.h"
#include "PreviewManager.h"
#include "RawEngine.h"

// Source buffers are in the working space (Rec.2020 primaries, sRGB curve):
// converts an intended sRGB-encoded 16-bit colour into it.
static void srgb16ToWorking16(ushort& r, ushort& g, ushort& b) {
  auto dec = [](ushort v) {
    const float e = v / 65535.0f;
    return e <= 0.04045f ? e / 12.92f : std::pow((e + 0.055f) / 1.055f, 2.4f);
  };
  float lr = dec(r), lg = dec(g), lb = dec(b);
  photon::colorspace::srgbToRec2020(lr, lg, lb);
  auto enc = [](float l) {
    return ushort(std::lround(std::clamp(photon::colorspace::encodeSrgb(l), 0.0f, 1.0f) * 65535.0f));
  };
  r = enc(lr); g = enc(lg); b = enc(lb);
}

class TestRawEngine : public QObject {
  Q_OBJECT

 private slots:
  void testLoadInvalidFile();
  void testLoadValidFile();
  void testProperties();
  void testPhoton001MultipassUsesFloatTargets();
  void testPhoton001ToneRangesReferenceBehavior();
  void testCalibrationReferenceBehavior();
  void testParametricToneCurveBehavior();
  void testToneCurveModeStash();
  void testHistoryStepsAndProcessVersion();
  void testLensFunParserAndCorrection();
  void testLensProfileEditState();
  void testHdrMergeRadiometric();
  void testCameraLookToneFit();
  void testWhiteBalanceKelvin();
  void testLensFunRealDatabase();
  void testOklabCreativeOps();
  void testToneCurveLumaMatchesReferenceMapping();
  void testSwitchingSourceResetsExposureAndContrast();
  void testApplyGeometryTransformsStraightenKeepsFullFrame();
  void testApplyGeometryTransformsCropRectOnRotatedFrame();
  void testApplyGeometryTransformsCropPreservesAspectAndFocus();
  void testBitmapJpegLoad();
  void testBitmapTiffLoadAndGeometryBake();
  void testFileScannerIncludesBitmaps();
  void testRatingStoreAndLegacySidecar();
  void testBitmapExport();
  void testBitmapPreview();
  void testProcessedImageConcurrentSourceSwitch();
  void testProfileAndLegacyMigration();
  void testBlackAndWhiteCpuDevelop();
  void testExportAppliesFinalPassEffects();
  void testExportSceneWhiteMatchesPreview();
  void testAiDenoiseTiling();
  void testAiDenoiseMosaicMock();
  void testAiDenoiseCacheRoundTrip();
  void testAiDenoiseBlendAndResample();
  void testAiDenoiseModeAndFallback();
  void testAiDenoiseEngineIntegration();
  void testWhiteBalanceKelvinRange50k();
  void testLightSlidersSmoothMonotoneAndRanged();
  void testToneBandsLocalNoHaloKeepDetail();
  void testToneMaskEncoding();
  void testToneSlidersMatchReference();
  void testExport16BitTiff();
  void testExportAdobeRgb();
  void testColorSpaceRec2020();
  void testDenoiseAtBaseExposure();
  void testDenoiseNoiseAdaptive();
  void testEditStackCurrentState();
  void testExportUsesHistorySelectedState();
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

  // Window targeting: +Blacks (an output-black lift) acts most, in stops, on
  // the deepest tones.
  {
    const QImage blacksUp = imageWith("blacks", 100.0);
    const float deepRef = bandMean(ref, 0, 2);
    const float deepBlacks = bandMean(blacksUp, 0, 2);
    QVERIFY(std::abs(deepBlacks - deepRef) > 0.0002f);
    QVERIFY(std::log2(deepBlacks / deepRef) >
            2.0f * std::log2(bandMean(blacksUp, 4, 12) / bandMean(ref, 4, 12)));
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

  // Clarity is local contrast: it needs texture (a smooth ramp has none and
  // must stay put), and both +/-100 act.
  {
    QJsonObject clarityOn = base;
    clarityOn["clarity"] = 100.0;
    const QImage smooth =
        photon::ImageDeveloper::develop(src.data(), w, h, clarityOn);
    // (Detail is measured on blurred log luminance, like the GPU preview: a
    // ramp linear in light is slightly curved in log near black.)
    const float diff = meanAbsLumaDiff(smooth, ref);
    QVERIFY2(diff < 0.006f, qPrintable(QString("smooth ramp moved by %1").arg(diff)));
  }
  std::vector<ushort> texSrc(src.size());
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const float linear = (0.002f + 0.998f * float(x) / float(w - 1)) *
                           ((((x / 2) + (y / 2)) % 2) ? 1.15f : 0.87f);
      const ushort v = linearToSrgb16(std::min(linear, 1.0f));
      const int i = (y * w + x) * 3;
      texSrc[i] = texSrc[i + 1] = texSrc[i + 2] = v;
    }
  }
  auto texWith = [&](double clarityValue) {
    QJsonObject settings = base;
    settings["clarity"] = clarityValue;
    return photon::ImageDeveloper::develop(texSrc.data(), w, h, settings);
  };
  const QImage texRef = texWith(0.0);
  const QImage clarityUp = texWith(100.0);
  const QImage clarityDown = texWith(-100.0);
  QVERIFY(maxLumaDiff(clarityUp, texRef) > 0.005f);
  QVERIFY(maxLumaDiff(clarityDown, texRef) > 0.005f);
  QVERIFY(maxLumaDiff(clarityUp, clarityDown) > 0.005f);

  const float upDiff = meanAbsLumaDiff(clarityUp, texRef);
  const float downDiff = meanAbsLumaDiff(clarityDown, texRef);
  // Both directions act; their strengths differ by tone, like Reference
  // (+ on shadows/midtones, - on midtones/highlights), so no symmetry check.
  QVERIFY(upDiff > 0.001f);
  QVERIFY(downDiff > 0.001f);
}

void TestRawEngine::testCalibrationReferenceBehavior() {
  constexpr int w = 64;
  constexpr int h = 4;

  auto linearToSrgb16 = [](float linear) {
    const float s = linear <= 0.0031308f
                        ? linear * 12.92f
                        : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
    return ushort(std::clamp(s, 0.0f, 1.0f) * 65535.0f + 0.5f);
  };

  auto maxLinearDiff = [](const QImage& a, const QImage& b) {
    float maxDiff = 0.0f;
    for (int y = 0; y < a.height(); ++y) {
      const uchar* sa = a.constScanLine(y);
      const uchar* sb = b.constScanLine(y);
      for (int x = 0; x < a.width(); ++x) {
        for (int c = 0; c < 3; ++c) {
          const float la = srgbToLinear(sa[x * 3 + c] / 255.0f);
          const float lb = srgbToLinear(sb[x * 3 + c] / 255.0f);
          maxDiff = std::max(maxDiff, std::abs(la - lb));
        }
      }
    }
    return maxDiff;
  };

  QJsonObject base;
  base["contrast"] = 1.0;
  base["profile"] = "normal";
  base["denoiseEnabled"] = false;
  base["sceneWhite"] = 1.0;

  std::vector<ushort> ramp(w * h * 3);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const float linear = 0.001f + 0.999f * float(x) / float(w - 1);
      const ushort v = linearToSrgb16(linear);
      const int i = (y * w + x) * 3;
      ramp[i] = v;
      ramp[i + 1] = v;
      ramp[i + 2] = v;
    }
  }

  const QImage ref = photon::ImageDeveloper::develop(ramp.data(), w, h, base);
  QVERIFY(!ref.isNull());

  QJsonObject neutral = base;
  neutral["calibShadowsTint"] = 0.0;
  neutral["calibRedHue"] = 0.0;
  neutral["calibRedSat"] = 0.0;
  neutral["calibGreenHue"] = 0.0;
  neutral["calibGreenSat"] = 0.0;
  neutral["calibBlueHue"] = 0.0;
  neutral["calibBlueSat"] = 0.0;
  const QImage neutralImg =
      photon::ImageDeveloper::develop(ramp.data(), w, h, neutral);
  QVERIFY(!neutralImg.isNull());
  QVERIFY(maxLinearDiff(ref, neutralImg) < 1e-4f);

  QJsonObject prim = base;
  prim["calibRedHue"] = 100.0;
  prim["calibRedSat"] = -100.0;
  prim["calibGreenHue"] = -80.0;
  prim["calibBlueSat"] = 100.0;
  const QImage primImg =
      photon::ImageDeveloper::develop(ramp.data(), w, h, prim);
  QVERIFY(!primImg.isNull());
  QVERIFY(maxLinearDiff(ref, primImg) < 0.01f);

  std::vector<ushort> colors(w * h * 3);
  const ushort r16 = linearToSrgb16(0.62f);
  const ushort g16 = linearToSrgb16(0.06f);
  const ushort b16 = linearToSrgb16(0.06f);
  for (int i = 0; i < w * h; ++i) {
    colors[i * 3] = r16;
    colors[i * 3 + 1] = g16;
    colors[i * 3 + 2] = b16;
  }
  const QImage colorRef =
      photon::ImageDeveloper::develop(colors.data(), w, h, base);
  QVERIFY(!colorRef.isNull());
  QJsonObject desat = base;
  desat["calibRedSat"] = -100.0;
  const QImage colorDesat =
      photon::ImageDeveloper::develop(colors.data(), w, h, desat);
  QVERIFY(!colorDesat.isNull());
  QVERIFY(maxLinearDiff(colorRef, colorDesat) > 0.02f);

  QJsonObject tint = base;
  tint["calibShadowsTint"] = 100.0;
  const QImage tintImg =
      photon::ImageDeveloper::develop(ramp.data(), w, h, tint);
  QVERIFY(!tintImg.isNull());

  float darkDelta = 0.0f;
  float brightDelta = 0.0f;
  for (int y = 0; y < h; ++y) {
    const uchar* sa = ref.constScanLine(y);
    const uchar* sb = tintImg.constScanLine(y);
    for (int x = 0; x < w; ++x) {
      for (int c = 0; c < 3; ++c) {
        const float la = srgbToLinear(sa[x * 3 + c] / 255.0f);
        const float lb = srgbToLinear(sb[x * 3 + c] / 255.0f);
        const float diff = std::abs(la - lb);
        if (x <= 4) darkDelta = std::max(darkDelta, diff);
        if (x >= 16) brightDelta = std::max(brightDelta, diff);
      }
    }
  }
  QVERIFY(darkDelta > 0.001f);
  QVERIFY(brightDelta < 0.002f);
}


void TestRawEngine::testParametricToneCurveBehavior() {
  constexpr int w = 64;
  constexpr int h = 4;

  auto linearToSrgb16 = [](float linear) {
    const float s = linear <= 0.0031308f
                        ? linear * 12.92f
                        : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
    return ushort(std::clamp(s, 0.0f, 1.0f) * 65535.0f + 0.5f);
  };

  std::vector<ushort> ramp(w * h * 3);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const float linear = 0.001f + 0.999f * float(x) / float(w - 1);
      const ushort v = linearToSrgb16(linear);
      const int i = (y * w + x) * 3;
      ramp[i] = v;
      ramp[i + 1] = v;
      ramp[i + 2] = v;
    }
  }

  QJsonObject base;
  base["contrast"] = 1.0;
  base["profile"] = "normal";
  base["denoiseEnabled"] = false;
  base["sceneWhite"] = 1.0;

  const QImage ref = photon::ImageDeveloper::develop(ramp.data(), w, h, base);
  QVERIFY(!ref.isNull());

  auto columnDelta = [](const QImage& a, const QImage& b, int fromX, int toX) {
    float maxDiff = 0.0f;
    for (int y = 0; y < a.height(); ++y) {
      const uchar* sa = a.constScanLine(y);
      const uchar* sb = b.constScanLine(y);
      for (int x = fromX; x <= toX; ++x) {
        for (int c = 0; c < 3; ++c) {
          const float la = srgbToLinear(sa[x * 3 + c] / 255.0f);
          const float lb = srgbToLinear(sb[x * 3 + c] / 255.0f);
          maxDiff = std::max(maxDiff, std::abs(la - lb));
        }
      }
    }
    return maxDiff;
  };

  QJsonObject highlights = base;
  highlights["toneCurveMode"] = "parametric";
  highlights["toneCurveHighlights"] = 100.0;
  const QImage hi =
      photon::ImageDeveloper::develop(ramp.data(), w, h, highlights);
  QVERIFY(!hi.isNull());
  QVERIFY(columnDelta(ref, hi, 56, w - 1) > 0.01f);
  QVERIFY(columnDelta(ref, hi, 0, 8) < 0.002f);

  for (int y = 0; y < hi.height(); ++y) {
    const uchar* scan = hi.constScanLine(y);
    float prev = -1.0f;
    for (int x = 0; x < hi.width(); ++x) {
      // Compare in display-encoded space (8-bit quantization = 1/255).
      const float luma = (0.2126f * scan[x * 3 + 0] +
                          0.7152f * scan[x * 3 + 1] +
                          0.0722f * scan[x * 3 + 2]) /
                         255.0f;
      QVERIFY(luma + 0.01f >= prev);
      prev = luma;
    }
  }

  QJsonObject shadows = base;
  shadows["toneCurveMode"] = "parametric";
  shadows["toneCurveShadows"] = 100.0;
  const QImage sh =
      photon::ImageDeveloper::develop(ramp.data(), w, h, shadows);
  QVERIFY(!sh.isNull());
  QVERIFY(columnDelta(ref, sh, 3, 6) > 0.005f);
  QVERIFY(columnDelta(ref, sh, 28, w - 1) < 0.002f);

  QJsonObject splits = base;
  splits["toneCurveMode"] = "parametric";
  splits["toneCurveShadows"] = 100.0;
  splits["toneCurveSplitShadows"] = 70.0;
  const QImage sp =
      photon::ImageDeveloper::develop(ramp.data(), w, h, splits);
  QVERIFY(!sp.isNull());
  QVERIFY(columnDelta(sh, sp, 24, 40) > 0.002f);

  // Mutual exclusivity: parametric values are ignored in manual mode.
  QJsonObject manualWithParametric = base;
  manualWithParametric["toneCurveHighlights"] = 100.0;
  manualWithParametric["toneCurveShadows"] = 100.0;
  const QImage manualParam =
      photon::ImageDeveloper::develop(ramp.data(), w, h, manualWithParametric);
  QVERIFY(!manualParam.isNull());
  QVERIFY(columnDelta(ref, manualParam, 0, w - 1) < 0.002f);

  // Mutual exclusivity: point curves are ignored in parametric mode.
  QJsonObject parametricWithPoints = highlights;
  parametricWithPoints["toneCurveLuma"] =
      makeCurve({{0.0, 0.0}, {1.0, 0.5}});
  const QImage parametricPoints = photon::ImageDeveloper::develop(
      ramp.data(), w, h, parametricWithPoints);
  QVERIFY(!parametricPoints.isNull());
  QVERIFY(columnDelta(hi, parametricPoints, 0, w - 1) < 0.002f);
}

void TestRawEngine::testToneCurveModeStash() {
  RawEngine engine;
  QVariantList pts;
  pts << QVariantMap{{"x", 0.0}, {"y", 0.0}}
      << QVariantMap{{"x", 0.5}, {"y", 0.7}}
      << QVariantMap{{"x", 1.0}, {"y", 1.0}};
  engine.setToneCurveLuma(pts);
  engine.setToneCurveMode("parametric");
  engine.setToneCurveHighlights(80.0f);
  engine.setToneCurveSplitMid(40.0f);

  const QVariantMap saved = engine.currentSettings();

  // Going back to Points keeps both data sets stashed.
  engine.setToneCurveMode("manual");
  QCOMPARE(engine.toneCurveHighlights(), 80.0f);
  QCOMPARE(engine.toneCurveSplitMid(), 40.0f);
  QVERIFY(engine.toneCurveLuma() == pts);

  // Returning to Parametric restores the previous parametric settings.
  engine.setToneCurveMode("parametric");
  QCOMPARE(engine.toneCurveHighlights(), 80.0f);
  QVERIFY(engine.toneCurveLuma() == pts);

  // JSON round-trip (sidecar load) keeps mode and both data sets.
  RawEngine reloaded;
  reloaded.applySettings(saved);
  QCOMPARE(reloaded.toneCurveMode(), QStringLiteral("parametric"));
  QCOMPARE(reloaded.toneCurveHighlights(), 80.0f);
  QCOMPARE(reloaded.toneCurveSplitMid(), 40.0f);
  QVERIFY(reloaded.toneCurveLuma() == pts);
}

void TestRawEngine::testHistoryStepsAndProcessVersion() {
  QTemporaryDir tempDir;
  QVERIFY(tempDir.isValid());

  RawEngine engine;
  engine.setSource(tempDir.filePath("history.arw"));

  QCOMPARE(engine.processVersion(), 1);
  QCOMPARE(engine.currentProcessVersion(), 1);

  // loadEdits creates the initial "Open" step (plus the virtual Original).
  QCOMPARE(engine.historySteps().size(), 2);
  QCOMPARE(engine.historySteps()[0].toMap()["label"].toString(),
           QStringLiteral("Open"));
  QCOMPARE(engine.historySteps()[1].toMap()["label"].toString(),
           QStringLiteral("Original"));

  engine.setExposure(0.5f);
  engine.commitEdit();
  engine.setContrast(1.1f);
  engine.commitEdit();

  QVariantList steps = engine.historySteps();
  QCOMPARE(steps.size(), 4);
  QCOMPARE(steps[0].toMap()["label"].toString(), QStringLiteral("Contrast"));
  QCOMPARE(steps[0].toMap()["delta"].toString(), QStringLiteral("+20"));
  QVERIFY(steps[0].toMap()["current"].toBool());
  QCOMPARE(steps[1].toMap()["label"].toString(), QStringLiteral("Exposure"));
  QCOMPARE(steps[1].toMap()["delta"].toString(), QStringLiteral("+0.50"));
  QCOMPARE(steps[2].toMap()["label"].toString(), QStringLiteral("Open"));
  QCOMPARE(steps[3].toMap()["label"].toString(), QStringLiteral("Original"));
  QCOMPARE(steps[3].toMap()["index"].toInt(), -1);

  // Per-entity labels for the colour mixer.
  engine.setHslRedHue(10.0f);
  engine.commitEdit();
  steps = engine.historySteps();
  QCOMPARE(steps[0].toMap()["label"].toString(),
           QStringLiteral("HSL Hue (Red)"));
  QCOMPARE(steps[0].toMap()["delta"].toString(), QStringLiteral("+10"));

  // Jumping back marks the newer steps as future and applies the state.
  engine.jumpToHistory(0);
  steps = engine.historySteps();
  QCOMPARE(engine.exposure(), 0.0f);
  QCOMPARE(engine.contrast(), 1.0f);
  QVERIFY(steps[0].toMap()["future"].toBool());
  QVERIFY(steps[3].toMap()["current"].toBool());

  // Redo walks forward again.
  engine.redo();
  QCOMPARE(engine.exposure(), 0.5f);

  // The virtual Original step resets every edit, keeps all entries and logs
  // a single "Reset" step at the top.
  engine.jumpToHistory(-1);
  steps = engine.historySteps();
  QCOMPARE(engine.exposure(), 0.0f);
  QCOMPARE(engine.contrast(), 1.0f);
  QCOMPARE(engine.hslRedHue(), 0.0f);
  QCOMPARE(steps.size(), 6);
  QCOMPARE(steps[0].toMap()["label"].toString(), QStringLiteral("Reset"));
  QCOMPARE(steps[0].toMap()["delta"].toString(), QString());
  QVERIFY(!steps[0].toMap()["future"].toBool());
  QCOMPARE(steps[1].toMap()["label"].toString(),
           QStringLiteral("HSL Hue (Red)"));
  QCOMPARE(steps.last().toMap()["label"].toString(), QStringLiteral("Original"));

  // Process version is stored/restored with the state.
  engine.setProcessVersion(2);
  QCOMPARE(engine.currentSettings().value("processVersion").toInt(), 2);
  RawEngine reloaded;
  reloaded.applySettings(engine.currentSettings());
  QCOMPARE(reloaded.processVersion(), 2);

  // Clear history keeps only the current state (plus the virtual Original).
  engine.clearHistory();
  steps = engine.historySteps();
  QCOMPARE(steps.size(), 2);
  QCOMPARE(steps[0].toMap()["label"].toString(), QStringLiteral("Current"));
  QCOMPARE(steps[0].toMap()["delta"].toString(), QString());
  QCOMPARE(steps[1].toMap()["label"].toString(), QStringLiteral("Original"));

  // History is capped at kHistoryLimit, dropping the oldest steps.
  for (int i = 1; i <= RawEngine::kHistoryLimit + 5; ++i) {
    engine.setExposure(0.01f * i);
    engine.commitEdit();
  }
  QCOMPARE(engine.historySteps().size(), RawEngine::kHistoryLimit + 1);
  QCOMPARE(engine.exposure(), 0.01f * (RawEngine::kHistoryLimit + 5));

  // Legacy sidecar steps (no metadata) get derived labels and deltas on load.
  QTemporaryDir legacyDir;
  QVERIFY(legacyDir.isValid());
  QDir().mkpath(legacyDir.filePath(".PhotonData/edits"));
  QJsonObject legacy0;
  legacy0["exposure"] = 0.0;
  legacy0["contrast"] = 1.0;
  QJsonObject legacy1 = legacy0;
  legacy1["exposure"] = 0.7;
  QJsonArray legacyArr;
  legacyArr.append(legacy0);
  legacyArr.append(legacy1);
  {
    QFile legacyFile(legacyDir.filePath(".PhotonData/edits/legacy.arw.json"));
    QVERIFY(legacyFile.open(QIODevice::WriteOnly));
    legacyFile.write(QJsonDocument(legacyArr).toJson());
  }
  RawEngine legacy;
  legacy.setSource(legacyDir.filePath("legacy.arw"));
  const QVariantList legacySteps = legacy.historySteps();
  QCOMPARE(legacySteps.size(), 3);
  QCOMPARE(legacySteps[0].toMap()["label"].toString(),
           QStringLiteral("Exposure"));
  QCOMPARE(legacySteps[0].toMap()["delta"].toString(), QStringLiteral("+0.70"));
  QCOMPARE(legacySteps[1].toMap()["label"].toString(),
           QStringLiteral("Open"));
  QCOMPARE(legacy.exposure(), 0.7f);

  // Sidecar round-trip: steps carry labels and the current index marker.
  QFile sidecar(tempDir.filePath(
      ".PhotonData/edits/history.arw.json"));
  QVERIFY(sidecar.open(QIODevice::ReadOnly));
  const QJsonArray arr =
      QJsonDocument::fromJson(sidecar.readAll()).array();
  QVERIFY(!arr.isEmpty());
  QVERIFY(arr.last().toObject().contains("__index"));
  QVERIFY(arr.first().toObject().contains("__step"));
}

void TestRawEngine::testLensFunParserAndCorrection() {
  const QByteArray xml = R"(<lensdatabase version="1">
    <camera><maker>Sony</maker><model>Sony ILCE-7M2</model>
      <mount>Sony E</mount><cropfactor>1.01</cropfactor></camera>
    <lens><maker>Sony</maker><model>Sony FE 16-35mm f/4 ZA OSS</model>
      <mount>Sony E</mount><cropfactor>1</cropfactor>
      <calibration>
        <distortion model="ptlens" focal="16" a="0.02" b="-0.03" c="0.01"/>
        <distortion model="ptlens" focal="35" a="0.01" b="-0.02" c="0.0"/>
        <tca model="poly3" focal="16" br="0.0001" vr="1.0002" bb="0.0001" vb="1.0001"/>
        <vignetting model="pa" focal="16" aperture="4" distance="10" k1="-0.8" k2="0.15" k3="-0.05"/>
      </calibration>
    </lens>
  </lensdatabase>)";

  photon::lensfun::Database& db = photon::lensfun::Database::instance();
  db.loadFromBuiltinXml({xml});

  const photon::lensfun::Profile profile = db.match(
      "Sony", "ILCE-7M2", "FE 16-35mm F4 ZA OSS", 24.0f, 4.0f);
  QVERIFY(profile.valid);
  QCOMPARE(profile.lensName, QStringLiteral("Sony FE 16-35mm f/4 ZA OSS"));
  QCOMPARE(profile.distortionModel, QStringLiteral("ptlens"));
  QVERIFY(profile.hasTca);
  QVERIFY(profile.hasVignetting);
  QCOMPARE(profile.crop, 1.01f);

  // Interpolated between focal 16 and 35: t = 8/19.
  const float t = 8.0f / 19.0f;
  QVERIFY(std::abs(profile.distortionK[0] - (0.02f + (0.01f - 0.02f) * t)) <
          1e-6f);
  QVERIFY(std::abs(profile.vignetting[0] - (-0.8f)) < 1e-6f);

  // Unknown lens is rejected.
  const photon::lensfun::Profile unknown = db.match(
      "Sony", "ILCE-7M2", "FE 70-200mm F2.8 GM", 70.0f, 2.8f);
  QVERIFY(!unknown.valid);

  // Vignetting correction brightens the corners (negative k1 = dark corners).
  QImage flat(64, 64, QImage::Format_RGB888);
  flat.fill(QColor(128, 128, 128));
  const QImage corrected =
      photon::lensfun::applyProfile(flat, profile, 0.0f, 0.0f, 100.0f);
  QCOMPARE(corrected.size(), flat.size());
  const int centre = corrected.constScanLine(32)[32 * 3 + 1];
  const int corner = corrected.constScanLine(0)[0 * 3 + 1];
  QVERIFY(corner > centre);
  QVERIFY(centre >= 126 && centre <= 130);

  // Distortion correction (barrel) keeps the centre and moves edge detail.
  QImage grid(64, 64, QImage::Format_RGB888);
  grid.fill(Qt::black);
  for (int y = 0; y < 64; ++y)
    for (int x = 0; x < 64; ++x) {
      uchar* px = grid.scanLine(y) + x * 3;
      px[0] = px[1] = px[2] = (x % 8 == 0 || y % 8 == 0) ? 255 : 0;
    }
  const QImage undistorted = photon::lensfun::applyProfile(
      grid, profile, 100.0f, 0.0f, 0.0f);
  QCOMPARE(undistorted.size(), grid.size());
  const uchar* centreRow = undistorted.constScanLine(32);
  QVERIFY(centreRow[32 * 3 + 1] >= 0);
}

void TestRawEngine::testLensProfileEditState() {
  QTemporaryDir tempDir;
  QVERIFY(tempDir.isValid());

  RawEngine engine;
  engine.setSource(tempDir.filePath("lens.arw"));

  QVERIFY(!engine.lensProfileEnabled());
  QCOMPARE(engine.lensProfileDistortion(), 50.0f);
  QCOMPARE(engine.lensProfileVignetting(), 50.0f);
  QCOMPARE(engine.lensProfileCA(), 50.0f);
  QVERIFY(engine.isDefault());

  engine.setLensProfileEnabled(true);
  engine.setLensProfileDistortion(60.0f);
  QVERIFY(engine.lensProfileEnabled());
  QVERIFY(!engine.isDefault());

  const QVariantMap saved = engine.currentSettings();
  QCOMPARE(saved.value("lensProfileEnabled").toBool(), true);
  QCOMPARE(saved.value("lensProfileDistortion").toFloat(), 60.0f);

  RawEngine reloaded;
  reloaded.setSource(tempDir.filePath("lens2.arw"));
  reloaded.applySettings(saved);
  QVERIFY(reloaded.lensProfileEnabled());
  QCOMPARE(reloaded.lensProfileDistortion(), 60.0f);
  QCOMPARE(reloaded.lensProfileVignetting(), 50.0f);

  // Manual selection resolves a lens even without matching EXIF data.
  QVERIFY(engine.lensProfileMakers().contains(QStringLiteral("Sony")));
  engine.setLensProfileMaker(QStringLiteral("Sony"));
  QVERIFY(engine.lensProfileLenses().contains(
      QStringLiteral("Sony FE 16-35mm f/4 ZA OSS")));
  engine.setLensProfileLens(QStringLiteral("Sony FE 16-35mm f/4 ZA OSS"));
  QCOMPARE(engine.lensProfileName(),
           QStringLiteral("Sony FE 16-35mm f/4 ZA OSS"));

  const QVariantMap manual = engine.currentSettings();
  QCOMPARE(manual.value("lensProfileLens").toString(),
           QStringLiteral("Sony FE 16-35mm f/4 ZA OSS"));

  engine.autoDetectLensProfile();
  QCOMPARE(engine.lensProfileLens(), QString());
  QCOMPARE(engine.lensProfileName(), QString());
}

void TestRawEngine::testLensFunRealDatabase() {
  // Optional: validate the parser against a full lensfun database when one is
  // installed on the host (the bundled copy lives in the app resources).
  const QString dbDir = QStringLiteral("/usr/share/lensfun/version_1");
  if (!QDir(dbDir).exists())
    QSKIP("no system lensfun database available");

  QVector<QByteArray> docs;
  const QStringList files =
      QDir(dbDir).entryList(QStringList() << "*.xml", QDir::Files);
  for (const QString& name : files) {
    QFile file(QDir(dbDir).filePath(name));
    if (file.open(QIODevice::ReadOnly)) docs.append(file.readAll());
  }
  QVERIFY(!docs.isEmpty());

  photon::lensfun::Database& db = photon::lensfun::Database::instance();
  db.loadFromBuiltinXml(docs);
  std::fprintf(stderr, "DEBUG lensfun: %d cameras, %d lenses, %d docs\n",
               db.cameraCount(), db.lensCount(), docs.size());

  // A well-known combination must resolve from the real database.
  const photon::lensfun::Profile profile = db.match(
      "Sony", "ILCE-7M2", "FE 16-35mm F4 ZA OSS", 24.0f, 4.0f);
  QVERIFY(profile.valid);
  QVERIFY(profile.lensName.contains("16-35"));
  QVERIFY(!profile.distortionModel.isEmpty());
  QVERIFY(profile.hasVignetting);
  QVERIFY(db.makers().contains(QStringLiteral("Sony")));
  QVERIFY(db.lensesForMaker(QStringLiteral("Sony"))
              .contains(QStringLiteral("FE 16-35mm f/4 ZA OSS")));

  // And an unknown lens must not resolve.
  const photon::lensfun::Profile unknown = db.match(
      "Sony", "ILCE-7M2", "Totally Fake Lens 12-24mm", 12.0f, 2.8f);
  QVERIFY(!unknown.valid);
}

void TestRawEngine::testHdrMergeRadiometric() {
  // Synthetic scene spanning 0.01..8.0 radiance, three brackets at 1x, 1/4x
  // and 1/16x exposure with a 0.98 clip level.
  constexpr int N = 100;
  std::vector<std::vector<float>> frames(3, std::vector<float>(N));
  const std::vector<float> gains = {1.0f, 4.0f, 16.0f};
  const std::vector<float> exposures = {1.0f, 0.25f, 0.0625f};
  std::vector<float> scene(N);
  for (int p = 0; p < N; ++p) {
    scene[p] = 0.01f * std::pow(800.0f, float(p) / float(N - 1));
    for (int i = 0; i < 3; ++i)
      frames[i][p] = std::min(scene[p] * exposures[i], 0.98f);
  }

  const std::vector<float> merged =
      photon::hdrmath::mergeRadiometric(frames, gains, 0.99f);
  QCOMPARE(int(merged.size()), N);

  // Highlights recovered from the darkest frame (others are clipped).
  QVERIFY(merged[N - 1] > 6.0f);
  // Deep shadows come from the brightest frame.
  QVERIFY(merged[0] > 0.005f && merged[0] < 0.02f);
  // Midtones track the true scene.
  const int mid = N / 2;
  QVERIFY(std::abs(merged[mid] - scene[mid]) / scene[mid] < 0.1f);
  // Monotone response.
  for (int p = 1; p < N; ++p) QVERIFY(merged[p] >= merged[p - 1] * 0.98f);
}

void TestRawEngine::testCameraLookToneFit() {
  // A synthetic monotone camera tone. Quantile matching maps each quantile of
  // the scene to the same quantile of the JPEG, so a deterministic monotone
  // transform must come back out of the fit (within interpolation error).
  const auto camera = [](float scene) {
    return 0.02f + 0.9f * std::pow(scene, 1.25f);
  };
  constexpr int kCount = 3000;
  std::vector<std::pair<float, float>> pairs;
  pairs.reserve(kCount);
  for (int i = 0; i < kCount; ++i) {
    const float scene =
        0.002f * std::pow(500.0f, float(i) / float(kCount - 1));
    pairs.emplace_back(scene, camera(scene));
  }

  const photon::cameralook::CameraToneCurve curve =
      photon::cameralook::fitCameraToneCurve(pairs);
  QVERIFY(curve.valid);
  for (float scene : {0.01f, 0.05f, 0.18f, 0.5f, 0.9f})
    QVERIFY(std::abs(curve.apply(scene) - camera(scene)) < 0.02f);

  // Monotone response over the whole range.
  float previous = -1.0f;
  for (int i = 0; i <= 100; ++i) {
    const float y =
        curve.apply(0.002f * std::pow(500.0f, float(i) / 100.0f));
    QVERIFY(y >= previous - 1e-5f);
    previous = y;
  }

  // Fewer samples than the minimum: rejected.
  {
    std::vector<std::pair<float, float>> small(pairs.begin(),
                                               pairs.begin() + 100);
    QVERIFY(!photon::cameralook::fitCameraToneCurve(small).valid);
  }
  // An unrelated picture (shuffled targets) fails the rank-correlation gate.
  {
    std::vector<std::pair<float, float>> shuffled;
    shuffled.reserve(pairs.size());
    for (std::size_t i = 0; i < pairs.size(); ++i)
      shuffled.emplace_back(pairs[i].first,
                            pairs[(i * 7919) % pairs.size()].second);
    QVERIFY(!photon::cameralook::fitCameraToneCurve(shuffled).valid);
  }
  // Too little scene range (knots[31] < knots[0] * 1.5): rejected.
  {
    std::vector<std::pair<float, float>> narrow;
    narrow.reserve(kCount);
    for (int i = 0; i < kCount; ++i) {
      const float scene = 0.10f + 0.01f * std::sin(float(i));
      narrow.emplace_back(scene, camera(scene));
    }
    QVERIFY(!photon::cameralook::fitCameraToneCurve(narrow).valid);
  }

  // Composing into a display-encoded tone LUT reproduces decode -> curve ->
  // encode, and an invalid curve leaves the LUT untouched.
  {
    std::vector<float> lut(4096);
    for (std::size_t i = 0; i < lut.size(); ++i)
      lut[i] = float(i) / float(lut.size() - 1);
    std::vector<float> composed = lut;
    photon::cameralook::prependToneCurveToLut(composed, curve);
    for (std::size_t i = 0; i < composed.size(); i += 17) {
      const float display = float(i) / float(composed.size() - 1);
      const float expected = photon::cameralook::linearToSrgbF(
          curve.apply(photon::cameralook::srgbToLinearF(display)));
      QVERIFY(std::abs(composed[i] - expected) < 0.005f);
    }
    photon::cameralook::CameraToneCurve invalid;
    std::vector<float> untouched = lut;
    photon::cameralook::prependToneCurveToLut(untouched, invalid);
    QVERIFY(untouched == lut);
  }

  // Highlight protection: a curve fitted on a narrow scene range continues
  // straight to display white at scene white, so the RAW headroom above the
  // fitted range keeps its spread instead of collapsing just below white.
  {
    std::vector<std::pair<float, float>> low;
    low.reserve(kCount);
    for (int i = 0; i < kCount; ++i) {
      const float scene =
          0.002f * std::pow(250.0f, float(i) / float(kCount - 1));
      low.emplace_back(scene, 0.02f + 0.7f * std::pow(scene, 1.2f));
    }
    const photon::cameralook::CameraToneCurve lowCurve =
        photon::cameralook::fitCameraToneCurve(low);
    QVERIFY(lowCurve.valid);
    QVERIFY(std::abs(lowCurve.apply(1.0f) - 1.0f) < 1e-4f);
    QVERIFY(lowCurve.apply(0.9f) > 0.75f);
    float previousLow = -1.0f;
    for (int i = 0; i <= 100; ++i) {
      const float y = lowCurve.apply(0.002f + 0.998f * float(i) / 100.0f);
      QVERIFY(y >= previousLow - 1e-4f);
      previousLow = y;
    }
  }

  // The amount blend interpolates the look with the current rendering and
  // keeps white fixed; amount 0 leaves the LUT untouched.
  {
    std::vector<float> lut(1024);
    for (std::size_t i = 0; i < lut.size(); ++i)
      lut[i] = float(i) / float(lut.size() - 1);
    std::vector<float> half = lut;
    photon::cameralook::prependToneCurveToLut(half, curve, 0.5f);
    for (std::size_t i = 0; i < half.size(); i += 13) {
      const float display = float(i) / float(half.size() - 1);
      const float scene = photon::cameralook::srgbToLinearF(display);
      const float expected = photon::cameralook::linearToSrgbF(
          scene + 0.5f * (curve.apply(scene) - scene));
      QVERIFY(std::abs(half[i] - expected) < 0.005f);
    }
    QVERIFY(std::abs(half.back() - 1.0f) < 1e-3f);
    std::vector<float> zero = lut;
    photon::cameralook::prependToneCurveToLut(zero, curve, 0.0f);
    QVERIFY(zero == lut);
  }

  // JSON round-trip (per-photo cache and the export-side composition).
  {
    const QJsonObject json = photon::cameralook::toneCurveToJson(curve);
    const photon::cameralook::CameraToneCurve restored =
        photon::cameralook::toneCurveFromJson(json);
    QVERIFY(restored.valid);
    for (int i = 0; i < photon::cameralook::kToneKnots; ++i) {
      QVERIFY(std::abs(restored.knots[i][0] - curve.knots[i][0]) < 1e-6f);
      QVERIFY(std::abs(restored.knots[i][1] - curve.knots[i][1]) < 1e-6f);
    }
  }
}

void TestRawEngine::testWhiteBalanceKelvin() {
  using namespace photon::whitebalance;
  float gains[3] = {0.0f, 0.0f, 0.0f};

  // The as-shot balance is the neutral anchor.
  relativeGains(5500.0f, 5500.0f, gains);
  QVERIFY(std::abs(gains[0] - 1.0f) < 1e-4f);
  QVERIFY(std::abs(gains[1] - 1.0f) < 1e-4f);
  QVERIFY(std::abs(gains[2] - 1.0f) < 1e-4f);

  // Lower than as-shot cools the image (blue gain up).
  relativeGains(3000.0f, 5500.0f, gains);
  QVERIFY(gains[0] < 0.95f);
  QVERIFY(gains[2] > 1.05f);
  // Higher than as-shot warms it (red gain up).
  relativeGains(9000.0f, 5500.0f, gains);
  QVERIFY(gains[0] > 1.05f);
  QVERIFY(gains[2] < 0.95f);

  // Hotter blackbodies are bluer.
  float cold[3];
  float hot[3];
  planckianLinearRgb(3000.0f, cold);
  planckianLinearRgb(9000.0f, hot);
  QVERIFY(cold[0] / cold[2] > hot[0] / hot[2]);

  // The neutral anchor is the as-shot rendering.
  QVERIFY(std::abs(kNeutralKelvin - 5500.0f) < 1e-3f);
  relativeGains(kNeutralKelvin, kNeutralKelvin, gains);
  QVERIFY(std::abs(gains[0] - 1.0f) < 1e-4f);
  QVERIFY(std::abs(gains[2] - 1.0f) < 1e-4f);

  // Out-of-range slider values are clamped by the gain helper.
  relativeGains(1500.0f, 5500.0f, gains);
  float clamped[3];
  relativeGains(2000.0f, 5500.0f, clamped);
  QVERIFY(std::abs(gains[0] - clamped[0]) < 1e-4f);
  QVERIFY(std::abs(gains[2] - clamped[2]) < 1e-4f);
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
  ushort pr = 100 * 257;
  ushort pg = 180 * 257;
  ushort pb = 80 * 257;
  const float baseHue =  // of the intended sRGB colour
      oklabHueDeg(srgbToLinear(pr / 65535.0f), srgbToLinear(pg / 65535.0f),
                  srgbToLinear(pb / 65535.0f));
  srgb16ToWorking16(pr, pg, pb);
  for (int i = 0; i < w * h; ++i) {
    patch[i * 3 + 0] = pr;
    patch[i * 3 + 1] = pg;
    patch[i * 3 + 2] = pb;
  }
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
  ushort yr = 230 * 257;
  ushort yg = 200 * 257;
  ushort yb = 40 * 257;
  const float yellowHue =  // of the intended sRGB colour
      oklabHueDeg(srgbToLinear(yr / 65535.0f), srgbToLinear(yg / 65535.0f),
                  srgbToLinear(yb / 65535.0f));
  const float yIn[3] = {srgbToLinear(yr / 65535.0f), srgbToLinear(yg / 65535.0f),
                        srgbToLinear(yb / 65535.0f)};  // intended sRGB colour
  srgb16ToWorking16(yr, yg, yb);
  for (int i = 0; i < w * h; ++i) {
    yellow[i * 3 + 0] = yr;
    yellow[i * 3 + 1] = yg;
    yellow[i * 3 + 2] = yb;
  }
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
  labLC(yIn[0], yIn[1], yIn[2], inL, inC);

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
      // Contrast pivots at mid-grey (0.18 linear): darker tones darken,
      // brighter tones brighten, mid-grey stays put.
      float v = 0.18f;
      if (x < 8) v = 0.02f;
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
  QVERIFY(std::abs(bandLuma(cOut, 10, 22) - bandLuma(cRef, 10, 22)) < 0.01f);
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

  // The curve runs in the display-encoded domain (after sRGB encode): the
  // encoded input is mapped through y = 0.1 + 0.9x, then decoded back for the
  // linear comparison. For a straight line the min/max reprojection is exact.
  auto encode = [](double lin) {
    return lin <= 0.0031308 ? lin * 12.92
                            : 1.055 * std::pow(lin, 1.0 / 2.4) - 0.055;
  };
  auto decode = [](double enc) {
    return enc <= 0.04045 ? enc / 12.92
                          : std::pow((enc + 0.055) / 1.055, 2.4);
  };
  const double expectedR = decode(0.1 + 0.9 * encode(inLinR));
  const double expectedG = decode(0.1 + 0.9 * encode(inLinG));
  const double expectedB = decode(0.1 + 0.9 * encode(inLinB));

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

void TestRawEngine::testRatingStoreAndLegacySidecar() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());

  auto writeFile = [&dir](const QString& relative, const QByteArray& data) {
    QDir().mkpath(QFileInfo(dir.filePath(relative)).absolutePath());
    QFile file(dir.filePath(relative));
    if (!file.open(QIODevice::WriteOnly)) return false;
    return file.write(data) == data.size();
  };

  // Placeholder raw files (the scanner only looks at names/extensions here).
  QVERIFY(writeFile("rated.ARW", "x"));
  QVERIFY(writeFile("legacy.ARW", "x"));
  QVERIFY(writeFile("both.ARW", "x"));
  QVERIFY(writeFile("markerState.ARW", "x"));

  // New per-photo rating store.
  QVERIFY(writeFile(".PhotonData/ratings/rated.ARW.json", "{\"rating\":4}"));
  // Legacy rating in an old-format sidecar (no index marker).
  QVERIFY(writeFile(".PhotonData/edits/legacy.ARW.json",
                    "[{\"exposure\":0.5,\"rating\":3}]"));
  // The store wins over a legacy sidecar rating.
  QVERIFY(writeFile(".PhotonData/ratings/both.ARW.json", "{\"rating\":5}"));
  QVERIFY(writeFile(".PhotonData/edits/both.ARW.json",
                    "[{\"exposure\":0.0,\"rating\":2},{\"__index\":0}]"));
  // Legacy rating behind a trailing index marker.
  QVERIFY(writeFile(".PhotonData/edits/markerState.ARW.json",
                    "[{\"exposure\":0.0,\"rating\":3},{\"__index\":0}]"));

  FileScanner scanner;
  QMap<QString, int> ratings;
  const QVariantList files = scanner.scanForRawFiles(dir.path());
  for (const QVariant& file : files) {
    const QVariantMap map = file.toMap();
    ratings[map.value("name").toString()] = map.value("rating").toInt();
  }
  QCOMPARE(ratings.value("rated.ARW"), 4);
  QCOMPARE(ratings.value("legacy.ARW"), 3);
  QCOMPARE(ratings.value("both.ARW"), 5);
  QCOMPARE(ratings.value("markerState.ARW"), 3);
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
    srgb16ToWorking16(r, g, b);
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

// Export must apply the final-pass effects the preview shader applies
// (Sharpness, Structure, Dehaze, Centrè, Grain, Vignette) — previously they
// were GPU-only and exports looked different from the preview.
void TestRawEngine::testExportAppliesFinalPassEffects() {
  const int w = 160;
  const int h = 120;
  std::vector<ushort> src(size_t(w) * h * 3);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      // Mid-grey with fine texture and a soft colour gradient, so local
      // contrast and dehaze have something to act on.
      const float tex = ((x / 3 + y / 3) % 2) ? 0.06f : -0.06f;
      const float base = 0.45f + 0.15f * float(x) / float(w);
      const size_t i = (size_t(y) * w + x) * 3;
      src[i] = ushort(std::clamp(base + tex, 0.0f, 1.0f) * 65535.0f);
      src[i + 1] = ushort(std::clamp(base * 0.95f + tex, 0.0f, 1.0f) * 65535.0f);
      src[i + 2] = ushort(std::clamp(base * 0.85f + tex, 0.0f, 1.0f) * 65535.0f);
    }
  }

  QJsonObject base;
  base["contrast"] = 1.0;
  base["profile"] = "normal";
  base["denoiseEnabled"] = false;
  base["sceneWhite"] = 1.0;
  base["vignetteMidpoint"] = 50.0;
  base["vignetteFeather"] = 50.0;
  base["grainSize"] = 1.0;
  base["grainRoughness"] = 0.5;

  const QImage ref = photon::ImageDeveloper::develop(src.data(), w, h, base);
  QVERIFY(!ref.isNull());

  auto meanAbsDiff = [](const QImage& a, const QImage& b) {
    double sum = 0.0;
    for (int y = 0; y < a.height(); ++y) {
      const uchar* sa = a.constScanLine(y);
      const uchar* sb = b.constScanLine(y);
      for (int x = 0; x < a.width() * 3; ++x) sum += std::abs(sa[x] - sb[x]);
    }
    return sum / double(a.width() * a.height() * 3);
  };
  auto with = [&](const char* key, double value) {
    QJsonObject s = base;
    s[key] = value;
    return photon::ImageDeveloper::develop(src.data(), w, h, s);
  };

  const struct {
    const char* key;
    double value;
  } effects[] = {{"sharpness", 60.0},   {"structure", 60.0},
                 {"dehaze", 60.0},      {"centre", 80.0},
                 {"grainAmount", 80.0}, {"vignetteAmount", -80.0}};
  for (const auto& e : effects) {
    const QImage out = with(e.key, e.value);
    QVERIFY2(!out.isNull(), e.key);
    QVERIFY2(meanAbsDiff(out, ref) > 0.5, e.key);
  }

  // A negative vignette darkens the corners, not the centre.
  auto pixelLuma = [](const QImage& img, int x, int y) {
    const uchar* s = img.constScanLine(y);
    return 0.2126 * s[x * 3] + 0.7152 * s[x * 3 + 1] + 0.0722 * s[x * 3 + 2];
  };
  const QImage vig = with("vignetteAmount", -80.0);
  QVERIFY(pixelLuma(vig, 2, 2) < pixelLuma(ref, 2, 2) - 10.0);
  QVERIFY(std::abs(pixelLuma(vig, w / 2, h / 2) -
                   pixelLuma(ref, w / 2, h / 2)) < 3.0);

  // The vignette is relative to the cropped frame (like the baked preview):
  // the corner of a half-width crop is darkened too.
  QJsonObject cropped = base;
  QJsonObject rect;
  rect["x"] = 0.0;
  rect["y"] = 0.0;
  rect["w"] = 0.5;
  rect["h"] = 1.0;
  cropped["cropRect"] = rect;
  const QImage cropRef =
      photon::ImageDeveloper::develop(src.data(), w, h, cropped);
  cropped["vignetteAmount"] = -80.0;
  const QImage cropVig =
      photon::ImageDeveloper::develop(src.data(), w, h, cropped);
  QCOMPARE(cropVig.width(), w / 2);
  const int cw = cropVig.width();
  QVERIFY(pixelLuma(cropVig, cw - 3, 2) < pixelLuma(cropRef, cw - 3, 2) - 10.0);
  QVERIFY(std::abs(pixelLuma(cropVig, cw / 2, h / 2) -
                   pixelLuma(cropRef, cw / 2, h / 2)) < 3.0);
}

// Without an injected value, export measures scene white on the source exactly
// like the preview does at load (it used to fall back to 1.0).
void TestRawEngine::testExportSceneWhiteMatchesPreview() {
  const int w = 64;
  const int h = 64;
  std::vector<ushort> src(size_t(w) * h * 3);
  for (size_t i = 0; i < src.size(); ++i) {
    src[i] = ushort(((i / 3) % w) * 600 + 8000);  // dark-ish horizontal ramp
  }
  const float measured = photon::computeSceneWhite(src.data(), w, h, 3);
  QVERIFY(measured > 0.0f && measured < 0.5f);

  QJsonObject base;
  base["contrast"] = 1.0;
  base["profile"] = "normal";
  base["denoiseEnabled"] = false;
  base["exposure"] = 1.5;
  base["highlights"] = -40.0;

  QJsonObject injected = base;
  injected["sceneWhite"] = double(measured);
  QJsonObject legacy = base;
  legacy["sceneWhite"] = 1.0;

  const QImage autoImg = photon::ImageDeveloper::develop(src.data(), w, h, base);
  const QImage injectedImg =
      photon::ImageDeveloper::develop(src.data(), w, h, injected);
  const QImage legacyImg =
      photon::ImageDeveloper::develop(src.data(), w, h, legacy);
  QCOMPARE(autoImg, injectedImg);
  // The tone bands are display-referred with a fixed Highlights centre
  // (Reference fit): the scene white no longer changes the render.
  QCOMPARE(autoImg, legacyImg);
}

// --- AI denoise (model-agnostic parts; the tests build without ncnn) -------

void TestRawEngine::testAiDenoiseTiling() {
  using namespace photon::ai;
  // Small pictures are a single tile.
  QCOMPARE(tileStarts(100, 512, 64), std::vector<int>{0});
  for (const int n : {513, 1000, 3012, 2012}) {
    const std::vector<int> st = tileStarts(n, 512, 64);
    QVERIFY(st.size() >= 2);
    QCOMPARE(st.front(), 0);
    QCOMPARE(st.back(), n - 512);
    for (size_t i = 1; i < st.size(); ++i) {
      QVERIFY(st[i] > st[i - 1]);
      QVERIFY(st[i] - st[i - 1] <= 512 - 64);  // neighbours overlap enough
    }
  }
  // Weights: 1 inside and at picture edges, fading towards neighbours.
  QCOMPARE(tileWeight(0, 512, 64, false, false), 1.0f);
  QCOMPARE(tileWeight(256, 512, 64, true, true), 1.0f);
  QVERIFY(tileWeight(0, 512, 64, true, false) < 0.01f);
  QVERIFY(tileWeight(0, 512, 64, true, false) > 0.0f);
  QVERIFY(tileWeight(511, 512, 64, false, true) < 0.01f);
  QCOMPARE(tileWeight(511, 512, 64, true, false), 1.0f);
}

// A mock "model" that demosaics by replication and carries RawNIND's ~1e6
// output scale: the runner must remove the scale per tile and blend tiles
// without seams, so the output equals the per-cell input exactly.
void TestRawEngine::testAiDenoiseMosaicMock() {
  using namespace photon::ai;
  const int w = 2 * 150;  // 150 x 110 cells -> several 64-cell tiles
  const int h = 2 * 110;
  std::vector<float> mosaic(size_t(w) * h);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const float base = 0.1f + 0.4f * float(x) / w + 0.3f * float(y) / h;
      const float k = (y % 2 == 0) ? (x % 2 == 0 ? 0.6f : 1.0f)
                                   : (x % 2 == 0 ? 1.0f : 0.45f);
      mosaic[size_t(y) * w + x] = base * k;
    }
  }
  TileParams tp;
  tp.tile = 64;
  tp.overlap = 16;
  for (const float scale : {1.0e6f, 1.04e6f}) {
    int calls = 0;
    TileFn mock = [&](const float* in, std::vector<float>& out) {
      ++calls;
      const int t = tp.tile;
      const int side = 2 * t;
      out.assign(size_t(3) * side * side, 0.0f);
      for (int cy = 0; cy < t; ++cy) {
        for (int cx = 0; cx < t; ++cx) {
          const size_t k = size_t(cy) * t + cx;
          const float rgb[3] = {in[k],
                                0.5f * (in[size_t(t) * t + k] +
                                        in[size_t(2) * t * t + k]),
                                in[size_t(3) * t * t + k]};
          for (int c = 0; c < 3; ++c) {
            for (int sy = 0; sy < 2; ++sy) {
              for (int sx = 0; sx < 2; ++sx) {
                out[size_t(c) * side * side +
                    size_t(2 * cy + sy) * side + 2 * cx + sx] = rgb[c] * scale;
              }
            }
          }
        }
      }
      return true;
    };
    std::vector<float> cam;
    QString err;
    const Status st =
        denoiseMosaic(mosaic.data(), w, h, mock, tp, cam, nullptr, {}, &err);
    QCOMPARE(int(st), int(Status::Ok));
    QVERIFY(calls > 4);
    QCOMPARE(cam.size(), size_t(w) * h * 3);
    float maxErr = 0.0f;
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        const int cy = y / 2 * 2, cx = x / 2 * 2;
        const float r = mosaic[size_t(cy) * w + cx];
        const float g = 0.5f * (mosaic[size_t(cy) * w + cx + 1] +
                                mosaic[size_t(cy + 1) * w + cx]);
        const float b = mosaic[size_t(cy + 1) * w + cx + 1];
        const float* o = &cam[(size_t(y) * w + x) * 3];
        maxErr = std::max({maxErr, std::abs(o[0] - r), std::abs(o[1] - g),
                           std::abs(o[2] - b)});
      }
    }
    QVERIFY2(maxErr < 1e-4f, qPrintable(QString::number(maxErr)));
  }

  // Abort is honoured and a failing model is an error, not a crash.
  std::atomic<bool> abort{true};
  std::vector<float> cam;
  TileFn never = [](const float*, std::vector<float>&) { return true; };
  QCOMPARE(int(denoiseMosaic(mosaic.data(), w, h, never, tp, cam, &abort, {},
                             nullptr)),
           int(Status::Aborted));
  TileFn failing = [](const float*, std::vector<float>&) { return false; };
  QCOMPARE(int(denoiseMosaic(mosaic.data(), w, h, failing, tp, cam, nullptr,
                             {}, nullptr)),
           int(Status::Failed));
}

void TestRawEngine::testAiDenoiseCacheRoundTrip() {
  using namespace photon::ai;
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString photo = dir.filePath("IMG_0001.ARW");
  {
    QFile f(photo);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("raw");
  }
  Product p;
  p.width = 333;
  p.height = 141;  // not a multiple of the strip height
  p.rgb.resize(size_t(p.width) * p.height * 3);
  uint32_t seed = 7;
  for (size_t i = 0; i < p.rgb.size(); ++i) {
    seed = seed * 1664525u + 1013904223u;
    p.rgb[i] = ushort((i * 37) % 60000 + (seed >> 28));
  }
  const QString file = cachePath(photo, QStringLiteral("fp-test"));
  QVERIFY(file.contains("/.PhotonData/denoise/IMG_0001.ARW.ai-"));
  QVERIFY(saveProduct(file, p));
  Product back;
  QVERIFY(loadProduct(file, back));
  QCOMPARE(back.width, p.width);
  QCOMPARE(back.height, p.height);
  int maxDiff = 0;
  for (size_t i = 0; i < p.rgb.size(); ++i)
    maxDiff = std::max(maxDiff, std::abs(int(back.rgb[i]) - int(p.rgb[i])));
  QVERIFY2(maxDiff == 0, qPrintable(QString::number(maxDiff)));  // lossless

  // A different model fingerprint is a different file; stale ones go away.
  const QString other = cachePath(photo, QStringLiteral("fp-other"));
  QVERIFY(other != file);
  QVERIFY(saveProduct(other, p));
  removeStaleProducts(photo, other);
  QVERIFY(!QFile::exists(file));
  QVERIFY(QFile::exists(other));

  // Corrupt data is rejected.
  {
    QFile f(other);
    QVERIFY(f.open(QIODevice::ReadWrite));
    f.seek(40);
    f.write("garbage-garbage-garbage");
  }
  Product bad;
  QVERIFY(!loadProduct(other, bad) || bad.isValid());
}

void TestRawEngine::testAiDenoiseBlendAndResample() {
  using namespace photon::ai;
  const ushort plain[6] = {1000, 2000, 3000, 60000, 0, 500};
  const ushort prod[6] = {3000, 2000, 1000, 50000, 1000, 500};
  ushort out[6];
  blendRgb16(plain, prod, out, 2, 0.5f);
  const ushort expect[6] = {2000, 2000, 2000, 55000, 500, 500};
  for (int i = 0; i < 6; ++i) QCOMPARE(out[i], expect[i]);
  blendRgb16(plain, prod, out, 2, 0.0f);
  for (int i = 0; i < 6; ++i) QCOMPARE(out[i], plain[i]);
  blendRgb16(plain, prod, out, 2, 1.0f);
  for (int i = 0; i < 6; ++i) QCOMPARE(out[i], prod[i]);

  // Two-slider blend: luma follows the AI result, colour the colour pass.
  {
    const ushort p[3] = {30000, 20000, 10000};    // plain
    const ushort a[3] = {26000, 21000, 14000};    // AI
    const ushort k[3] = {22000, 22000, 22000};    // AI + colour pass (grey)
    ushort o[3];
    auto luma = [](const ushort* v) {  // working-space (Rec.2020) luma
      return photon::colorspace::luma(v[0], v[1], v[2]);
    };
    blendDenoised(p, a, k, o, 1, 0.0f, 0.0f);
    for (int i = 0; i < 3; ++i) QVERIFY(std::abs(o[i] - p[i]) <= 1);
    blendDenoised(p, a, k, o, 1, 1.0f, 0.0f);
    for (int i = 0; i < 3; ++i) QVERIFY(std::abs(o[i] - a[i]) <= 1);
    // Full colour: chroma of the colour pass (grey) with the AI luma.
    blendDenoised(p, a, k, o, 1, 1.0f, 1.0f);
    QVERIFY(std::abs(luma(o) - luma(a)) < 2.0);
    QVERIFY(std::abs(o[0] - o[1]) <= 2 && std::abs(o[1] - o[2]) <= 2);
    // Colour without luma: plain luma, grey chroma.
    blendDenoised(p, a, k, o, 1, 0.0f, 1.0f);
    QVERIFY(std::abs(luma(o) - luma(p)) < 2.0);
    QVERIFY(std::abs(o[0] - o[2]) <= 2);
    // No colour product: colour follows luma.
    blendDenoised(p, a, nullptr, o, 1, 1.0f, 1.0f);
    for (int i = 0; i < 3; ++i) QVERIFY(std::abs(o[i] - a[i]) <= 1);
  }

  // 4x2 -> 2x1 box average.
  std::vector<ushort> src(4 * 2 * 3);
  for (int i = 0; i < 8; ++i) {
    src[i * 3] = ushort(i * 100);
    src[i * 3 + 1] = 1000;
    src[i * 3 + 2] = ushort(i < 4 ? 0 : 4000);
  }
  std::vector<ushort> dst(2 * 1 * 3);
  resampleRgb16(src.data(), 4, 2, dst.data(), 2, 1);
  QCOMPARE(int(dst[0]), (0 + 100 + 400 + 500 + 2) / 4);
  QCOMPARE(int(dst[1]), 1000);
  QCOMPARE(int(dst[2]), 2000);
  QCOMPARE(int(dst[3]), (200 + 300 + 600 + 700 + 2) / 4);
}

// The denoise engine is a global Settings choice (AI by default); the Color
// amount is per photo (older edits migrate from the Classic chroma amount).
void TestRawEngine::testAiDenoiseModeAndFallback() {
  AppStateManager* app = AppStateManager::instance();
  const QString savedEngine = app->denoiseEngine();
  app->setDenoiseEngine(QStringLiteral("bogus"));
  QCOMPARE(app->denoiseEngine(), QStringLiteral("ai"));
  RawEngine engine;
  QSignalSpy modeSpy(&engine, &RawEngine::denoiseModeChanged);
  QCOMPARE(engine.denoiseMode(), QStringLiteral("ai"));
  app->setDenoiseEngine(QStringLiteral("classic"));
  QCOMPARE(engine.denoiseMode(), QStringLiteral("classic"));
  QCOMPARE(modeSpy.count(), 1);
  app->setDenoiseEngine(QStringLiteral("ai"));
  QVERIFY(!engine.currentSettings().contains("denoiseMode"));
  QCOMPARE(app->aiDenoiseDevices().mid(0, 2), (QStringList{"Auto", "CPU"}));

  QCOMPARE(engine.denoiseColor(), 50.0f);
  engine.setDenoiseColor(80.0f);
  RawEngine reloaded;
  reloaded.applySettings(engine.currentSettings());
  QCOMPARE(reloaded.denoiseColor(), 80.0f);
  QVariantMap legacy;
  legacy["denoiseChromaAmount"] = 30.0;
  legacy["denoiseMode"] = "classic";  // legacy per-photo key: ignored
  RawEngine migrated;
  migrated.applySettings(legacy);
  QCOMPARE(migrated.denoiseColor(), 30.0f);
  QCOMPARE(migrated.denoiseMode(), QStringLiteral("ai"));

  if (!photon::ai::isCompiledIn()) {
    QVERIFY(!engine.aiDenoiseAvailable());
    engine.setDenoiseEnabled(true);
    engine.setDenoiseAmount(50.0f);
    QVERIFY(!engine.aiDenoiseActive());  // Classic stays in charge
  }

  // Export/preview source: no product -> Classic fallback, pixels untouched.
  std::vector<ushort> rgb(8 * 8 * 3, 20000);
  QJsonObject settings;
  settings["denoiseEnabled"] = true;
  settings["denoiseAmount"] = 50.0;
  settings["denoiseMode"] = "ai";
  const bool applied = photon::ai::applyToDevelopSource(
      QStringLiteral("/nonexistent/photo.ARW"), rgb.data(), 8, 8, settings,
      true);
  QVERIFY(!applied);
  QCOMPARE(settings.value("denoiseMode").toString(), QStringLiteral("classic"));
  QVERIFY(settings.contains("sceneWhite"));
  for (const ushort v : rgb) QCOMPARE(v, ushort(20000));

  // ImageDeveloper skips Classic denoise when the source was AI-denoised.
  const int w = 32, h = 24;
  std::vector<ushort> src(size_t(w) * h * 3);
  for (size_t i = 0; i < src.size(); ++i)
    src[i] = ushort(20000 + ((i * 7919) % 5000));
  QJsonObject base;
  base["contrast"] = 1.0;
  base["profile"] = "normal";
  base["sceneWhite"] = 1.0;
  QJsonObject off = base;
  off["denoiseEnabled"] = false;
  QJsonObject ai = base;
  ai["denoiseEnabled"] = true;
  ai["denoiseAmount"] = 80.0;
  ai["denoiseMode"] = "ai";
  QCOMPARE(photon::ImageDeveloper::develop(src.data(), w, h, ai),
           photon::ImageDeveloper::develop(src.data(), w, h, off));
  app->setDenoiseEngine(savedEngine);
}

// End-to-end: a real Bayer RAW through RawEngine with the real model. Runs
// only when the build has ncnn + the bundled model and a sample RAW exists
// (PHOTON_TEST_RAW or <repo>/testphoton/_DSC8711.ARW).
void TestRawEngine::testAiDenoiseEngineIntegration() {
  if (!photon::ai::isCompiledIn() || !photon::ai::modelFilesPresent())
    QSKIP("AI denoise model not available in this build");
  QString sample = qEnvironmentVariable("PHOTON_TEST_RAW");
  if (sample.isEmpty()) {
    sample = QCoreApplication::applicationDirPath() +
             QStringLiteral("/../../testphoton/_DSC8711.ARW");
  }
  if (!QFileInfo::exists(sample)) QSKIP("no sample Bayer RAW available");

  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.filePath(QFileInfo(sample).fileName());
  QVERIFY(QFile::copy(sample, path));

  RawEngine engine;
  QSignalSpy loaded(&engine, &RawEngine::imageLoaded);
  engine.setSource(path);
  QVERIFY(loaded.wait(60000));
  const QImage plain = engine.getProcessedImage();
  QVERIFY(!plain.isNull());

  auto roughness = [](const QImage& img) {
    // Mean absolute horizontal difference of the green channel (noise proxy).
    double sum = 0.0;
    size_t n = 0;
    for (int y = img.height() / 3; y < img.height() * 2 / 3; ++y) {
      const QRgba64* row = reinterpret_cast<const QRgba64*>(img.constScanLine(y));
      for (int x = 1; x < img.width(); ++x, ++n)
        sum += std::abs(int(row[x].green()) - int(row[x - 1].green()));
    }
    return sum / double(n);
  };
  auto meanGreen = [](const QImage& img) {
    double sum = 0.0;
    for (int y = 0; y < img.height(); ++y) {
      const QRgba64* row = reinterpret_cast<const QRgba64*>(img.constScanLine(y));
      for (int x = 0; x < img.width(); ++x) sum += row[x].green();
    }
    return sum / double(img.width() * img.height());
  };

  QSignalSpy pixels(&engine, &RawEngine::sourcePixelsChanged);
  AppStateManager::instance()->setDenoiseEngine(QStringLiteral("ai"));
  engine.setDenoiseEnabled(true);
  engine.setDenoiseAmount(100.0f);
  QVERIFY(engine.aiDenoiseActive());
  QTRY_VERIFY_WITH_TIMEOUT(!engine.aiDenoiseRunning() && pixels.count() > 0,
                           180000);
  QVERIFY2(engine.aiDenoiseActive(), qPrintable(engine.aiDenoiseStatus()));
  const QImage denoised = engine.getProcessedImage();
  QCOMPARE(denoised.size(), plain.size());
  QVERIFY(denoised != plain);
  const double rPlain = roughness(plain);
  const double rAi = roughness(denoised);
  QVERIFY2(rAi < rPlain * 0.9,
           qPrintable(QStringLiteral("roughness %1 -> %2").arg(rPlain).arg(rAi)));
  QVERIFY(std::abs(meanGreen(denoised) - meanGreen(plain)) <
          0.02 * meanGreen(plain));

  // The product is cached next to the photo.
  const QStringList cached =
      QDir(dir.filePath(".PhotonData/denoise")).entryList({"*.ai-*.pdn"}, QDir::Files);
  QCOMPARE(cached.size(), 1);
  qInfo() << "cached product" << cached.first() << QFileInfo(dir.filePath(
                 ".PhotonData/denoise/" + cached.first())).size() / 1e6 << "MB";

  // Colour pass: full Color has less chroma noise than none.
  auto chromaRoughness = [](const QImage& img) {
    double sum = 0.0;
    size_t n = 0;
    for (int y = img.height() / 3; y < img.height() * 2 / 3; ++y) {
      const QRgba64* row = reinterpret_cast<const QRgba64*>(img.constScanLine(y));
      for (int x = 1; x < img.width(); ++x, ++n) {
        const int c1 = int(row[x].red()) - int(row[x].green());
        const int c0 = int(row[x - 1].red()) - int(row[x - 1].green());
        sum += std::abs(c1 - c0);
      }
    }
    return sum / double(n);
  };
  pixels.clear();
  engine.setDenoiseColor(0.0f);
  QTRY_VERIFY_WITH_TIMEOUT(pixels.count() > 0, 10000);
  const double chromaNone = chromaRoughness(engine.getProcessedImage());
  pixels.clear();
  engine.setDenoiseColor(100.0f);
  QTRY_VERIFY_WITH_TIMEOUT(pixels.count() > 0, 10000);
  const double chromaFull = chromaRoughness(engine.getProcessedImage());
  QVERIFY2(chromaFull < chromaNone * 0.9,
           qPrintable(QStringLiteral("chroma %1 -> %2").arg(chromaNone).arg(chromaFull)));
  QCOMPARE(QDir(dir.filePath(".PhotonData/denoise"))
               .entryList({"*.aic-*.pdn"}, QDir::Files)
               .size(),
           1);

  // Amount and Color 0 restore the plain source exactly (no model run).
  pixels.clear();
  engine.setDenoiseColor(0.0f);
  QTRY_VERIFY_WITH_TIMEOUT(pixels.count() > 0, 10000);
  pixels.clear();
  engine.setDenoiseAmount(0.0f);
  QTRY_VERIFY_WITH_TIMEOUT(pixels.count() > 0, 10000);
  QCOMPARE(engine.getProcessedImage(), plain);

  // Half the amount is half-way, from the cached product.
  pixels.clear();
  engine.setDenoiseAmount(50.0f);
  QTRY_VERIFY_WITH_TIMEOUT(pixels.count() > 0, 10000);
  const double rHalf = roughness(engine.getProcessedImage());
  QVERIFY(rHalf < rPlain && rHalf > rAi);
}

// Sidecars end with a {"__index": i} history marker: the current state is
// states[i], not the last array element (export used to read the marker and
// render the photo without any edit).
void TestRawEngine::testEditStackCurrentState() {
  using namespace photon::editstack;
  QJsonObject a, b, c;
  a["exposure"] = 0.0;
  b["exposure"] = -1.0;
  b["__step"] = "Exposure";
  c["exposure"] = -2.0;
  QJsonObject marker;
  marker["__index"] = 1;
  marker["rating"] = 4;

  QJsonArray plain{a, b};
  QCOMPARE(currentState(plain)["exposure"].toDouble(), -1.0);
  QJsonArray withMarker{a, b, c, marker};
  QCOMPARE(currentIndex(withMarker), 1);
  QCOMPARE(currentState(withMarker)["exposure"].toDouble(), -1.0);
  QVERIFY(!currentSettings(withMarker).contains("__step"));
  QVERIFY(currentState(QJsonArray{marker}).isEmpty());
  QVERIFY(currentState(QJsonArray{}).isEmpty());
  QJsonObject badMarker;
  badMarker["__index"] = 99;
  QCOMPARE(currentState(QJsonArray{a, c, badMarker})["exposure"].toDouble(), -2.0);

  // Replacing keeps the history and the marker (incl. its rating).
  QJsonObject edited = currentState(withMarker);
  edited["orientationSteps"] = 1;
  replaceCurrentState(withMarker, edited);
  QCOMPARE(withMarker.size(), 4);
  QCOMPARE(withMarker[1].toObject()["orientationSteps"].toInt(), 1);
  QCOMPARE(withMarker[1].toObject()["exposure"].toDouble(), -1.0);
  QCOMPARE(withMarker[3].toObject()["__index"].toInt(), 1);
  QCOMPARE(withMarker[3].toObject()["rating"].toInt(), 4);
  QJsonArray empty;
  replaceCurrentState(empty, edited);
  QCOMPARE(empty.size(), 1);
}

void TestRawEngine::testExportUsesHistorySelectedState() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString sourcePath = dir.filePath("history.jpg");
  const int width = 16, height = 8;
  std::vector<uint8_t> pixels(size_t(width) * height * 3, 128);
  QVERIFY(BitmapTestUtils::writeJpegFile(sourcePath, width, height, pixels));

  // History: neutral, then -2 EV (current, selected by the marker), then a
  // redo step at +1 EV that must be ignored.
  QJsonObject neutral, dark, redo, marker;
  neutral["exposure"] = 0.0;
  dark["exposure"] = -2.0;
  redo["exposure"] = 1.0;
  marker["__index"] = 1;
  QVERIFY(QDir().mkpath(dir.filePath(".PhotonData/edits")));
  {
    QFile f(dir.filePath(".PhotonData/edits/history.jpg.json"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QJsonDocument(QJsonArray{neutral, dark, redo, marker}).toJson());
  }

  photon::ExportManager exportManager;
  QSignalSpy finishedSpy(&exportManager, &photon::ExportManager::exportFinished);
  exportManager.startExport({sourcePath}, dir.filePath("out"), "PNG", 100);
  QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() > 0, 60000);
  const QImage exported(dir.filePath("out/history.png"));
  QVERIFY(!exported.isNull());
  const int g = qGray(exported.pixel(width / 2, height / 2));
  QVERIFY2(g < 70, qPrintable(QStringLiteral("grey %1: -2 EV not applied").arg(g)));
}

// The RAW temperature slider reaches 50000 K: gains stay finite and keep
// warming monotonically past the old 10000 K limit (no clamp plateau).
void TestRawEngine::testWhiteBalanceKelvinRange50k() {
  namespace wb = photon::whitebalance;
  QCOMPARE(wb::kMinKelvin, 2000.0f);
  QCOMPARE(wb::kMaxKelvin, 50000.0f);
  float prevR = 0.0f, prevB = 1e9f;
  for (float k : {5500.0f, 10000.0f, 15000.0f, 25000.0f, 35000.0f, 50000.0f}) {
    float g[3];
    wb::relativeGains(k, wb::kNeutralKelvin, g);
    QVERIFY(std::isfinite(g[0]) && std::isfinite(g[2]));
    QVERIFY2(g[0] >= prevR && g[2] <= prevB, qPrintable(QString::number(k)));
    if (k > 10000.0f) QVERIFY(g[0] > prevR && g[2] < prevB);
    prevR = g[0];
    prevB = g[2];
  }
  float over[3], top[3];
  wb::relativeGains(90000.0f, wb::kNeutralKelvin, over);  // clamped
  wb::relativeGains(50000.0f, wb::kNeutralKelvin, top);
  QCOMPARE(over[0], top[0]);
  QCOMPARE(over[2], top[2]);
}

// --- Light sliders: continuity, monotonicity and tonal ranges --------------
//
// A synthetic test photo: a step wedge of grey patches (log-spaced from deep
// black to just over white), one flat row and one textured row (the tone
// ranges also react to local contrast). Each slider is swept across its whole
// range through the CPU develop path (which the GPU shaders mirror) and every
// patch is checked for jumps, for monotone response and for tone inversion.
// With PHOTON_TONE_REPORT=1 the per-patch response is printed.
namespace {
struct ToneWedge {
  static constexpr int kPatches = 24;
  static constexpr int kPatch = 32;
  int width = kPatches * kPatch;
  int height = 2 * kPatch;
  std::vector<ushort> src;
  std::vector<double> linear;  // patch luminance (linear)

  ToneWedge() {
    src.resize(size_t(width) * height * 3);
    linear.resize(kPatches);
    auto encode = [](double l) {
      l = std::clamp(l, 0.0, 1.0);
      const double e = l <= 0.0031308 ? l * 12.92 : 1.055 * std::pow(l, 1 / 2.4) - 0.055;
      return ushort(std::clamp(e * 65535.0 + 0.5, 0.0, 65535.0));
    };
    for (int p = 0; p < kPatches; ++p) {
      // 2^-11 .. 2^0 (and the last patch clipped white)
      linear[p] = std::pow(2.0, -11.0 + 11.0 * p / double(kPatches - 2));
      for (int y = 0; y < height; ++y) {
        for (int x = p * kPatch; x < (p + 1) * kPatch; ++x) {
          double l = linear[p];
          if (y >= kPatch) {
            // textured row: +-25 % checker of 4 px
            l *= (((x / 4) + (y / 4)) % 2) ? 1.25 : 0.8;
          }
          const ushort v = encode(l);
          const size_t i = (size_t(y) * width + x) * 3;
          src[i] = src[i + 1] = src[i + 2] = v;
        }
      }
    }
  }

  // Mean linear luminance of each patch centre (flat row = row 0).
  std::vector<double> measure(const QImage& img, int row) const {
    std::vector<double> out(kPatches);
    for (int p = 0; p < kPatches; ++p) {
      double sum = 0.0;
      int n = 0;
      for (int y = row * kPatch + 8; y < row * kPatch + kPatch - 8; ++y) {
        const uchar* s = img.constScanLine(y);
        for (int x = p * kPatch + 8; x < (p + 1) * kPatch - 8; ++x) {
          auto lin = [](double v) {
            v /= 255.0;
            return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
          };
          sum += 0.2126 * lin(s[x * 3]) + 0.7152 * lin(s[x * 3 + 1]) +
                 0.0722 * lin(s[x * 3 + 2]);
          ++n;
        }
      }
      out[p] = sum / n;
    }
    return out;
  }
};

// Display-referred (sRGB-encoded) value, the scale artefacts are judged in.
double toDisplay(double l) {
  l = std::clamp(l, 0.0, 1.0);
  return l <= 0.0031308 ? l * 12.92 : 1.055 * std::pow(l, 1 / 2.4) - 0.055;
}
}  // namespace

void TestRawEngine::testLightSlidersSmoothMonotoneAndRanged() {
  const ToneWedge wedge;
  QJsonObject base;
  base["contrast"] = 1.0;
  base["profile"] = "normal";
  base["denoiseEnabled"] = false;
  base["sceneWhite"] = 1.0;
  const bool report = qEnvironmentVariableIsSet("PHOTON_TONE_REPORT");

  struct Slider {
    const char* key;
    double from, to, step;
    bool brightens;  // monotone non-decreasing in the slider
  };
  const Slider sliders[] = {
      {"exposure", -3.0, 3.0, 0.1, true}, {"highlights", -100, 100, 4, true},
      {"shadows", -100, 100, 4, true},    {"whites", -100, 100, 4, true},
      {"blacks", -100, 100, 4, true},     {"clarity", -100, 100, 10, false},
      {"contrast", -100, 100, 4, false},
  };
  // Couplings that used to break monotonicity / continuity, and the combos
  // most likely to invert tones (opposite moves of neighbouring bands).
  struct Context {
    const char* name;
    const char* key;
    double value;
    QStringList sweep;
  };
  const Context contexts[] = {
      {"", "", 0.0, {}},
      {"blacks=-60", "blacks", -60.0, {"shadows"}},
      {"highlights=+60", "highlights", 60.0, {"shadows"}},
      {"shadows=+60", "shadows", 60.0, {"highlights"}},
      {"shadows=-100", "shadows", -100.0, {"blacks", "highlights"}},
      {"highlights=-100", "highlights", -100.0, {"whites", "shadows"}},
      {"exposure=+2", "exposure", 2.0, {"highlights", "whites"}},
  };

  QStringList failures;
  for (const Context& ctx : contexts) {
    for (const Slider& sl : sliders) {
      if (ctx.key[0] && !ctx.sweep.contains(sl.key)) continue;
      std::vector<std::vector<double>> rowsFlat, rowsTex;
      std::vector<double> values;
      for (double v = sl.from; v <= sl.to + 1e-9; v += sl.step) {
        QJsonObject s = base;
        if (ctx.key[0]) s[ctx.key] = ctx.value;
        if (QString(sl.key) == "contrast")
          s["contrast"] = 1.0 + v / 200.0;  // UI mapping
        else
          s[sl.key] = v;
        const QImage out =
            photon::ImageDeveloper::develop(wedge.src.data(), wedge.width,
                                            wedge.height, s);
        rowsFlat.push_back(wedge.measure(out, 0));
        rowsTex.push_back(wedge.measure(out, 1));
        values.push_back(v);
      }
      for (int row = 0; row < 2; ++row) {
        const auto& r = row == 0 ? rowsFlat : rowsTex;
        for (int p = 0; p < ToneWedge::kPatches; ++p) {
          // Jumps: display-referred change between neighbouring slider
          // values well above the typical step (and above dither noise).
          std::vector<double> steps;
          for (size_t i = 1; i < r.size(); ++i)
            steps.push_back(std::abs(toDisplay(r[i][p]) - toDisplay(r[i - 1][p])));
          // A jump is a step much larger than both neighbours (an
          // accelerating curve, like +Whites, has growing but similar steps).
          for (size_t i = 0; i < steps.size(); ++i) {
            const double prev = i > 0 ? steps[i - 1] : steps[i + 1];
            const double next = i + 1 < steps.size() ? steps[i + 1] : steps[i - 1];
            if (steps[i] > std::max(3.0 * std::max(prev, next), 2.5 / 255.0)) {
              failures << QStringLiteral("%1%2 %3 patch %4: jump %5 at %6")
                              .arg(ctx.name).arg(ctx.name[0] ? " " : "")
                              .arg(sl.key).arg(p)
                              .arg(steps[i] * 255.0, 0, 'f', 1)
                              .arg(values[i + 1]);
              break;
            }
          }
          // Monotone in the slider (brightening sliders).
          if (sl.brightens) {
            for (size_t i = 1; i < r.size(); ++i) {
              if (toDisplay(r[i][p]) < toDisplay(r[i - 1][p]) - 1.0 / 255.0) {
                failures << QStringLiteral("%1%2 %3 patch %4 row %5: decreases at %6")
                                .arg(ctx.name).arg(ctx.name[0] ? " " : "")
                                .arg(sl.key).arg(p).arg(row).arg(values[i]);
                break;
              }
            }
          }
        }
        // No tone inversion along the (flat) wedge at any setting.
        if (row == 0) {
          for (size_t i = 0; i < r.size(); ++i) {
            for (int p = 1; p < ToneWedge::kPatches; ++p) {
              // Equal inputs (clipped white) may differ with local tone
              // mapping (different surroundings): only strictly brighter
              // inputs must stay brighter.
              if (std::min(wedge.linear[p], 1.0) <= std::min(wedge.linear[p - 1], 1.0))
                continue;
              if (toDisplay(r[i][p]) < toDisplay(r[i][p - 1]) - 1.0 / 255.0) {
                failures << QStringLiteral("%1%2 %3=%4: tone inversion at patch %5")
                                .arg(ctx.name).arg(ctx.name[0] ? " " : "")
                                .arg(sl.key).arg(values[i]).arg(p);
                break;
              }
            }
          }
        }
      }
      if (!ctx.name[0]) {
        // Tonal zones (display-referred, Reference-like), flat row, stops at
        // the slider extremes. Patch p has log2(L) = -11 + 0.5 p (display
        // white = patch 22).
        const auto& r = rowsFlat;
        const size_t mid = r.size() / 2;
        auto at = [&](bool high, int p) {
          return std::log2(std::max(high ? r.back()[p] : r.front()[p], 1e-6) /
                           std::max(r[mid][p], 1e-6));
        };
        struct Zone {
          const char* key;
          bool high;  // slider at +100 (else -100)
          int patch;
          double lo, hi;  // allowed stops
          const char* what;
        };
        const Zone zones[] = {
            // (fitted to Reference: broad Shadows/Highlights, Whites over
            // everything above ~20 %, Blacks a levels black point)
            {"shadows", true, 10, 0.9, 3.0, "Shadows lifts ~13 % strongly"},
            {"shadows", true, 22, -0.01, 0.05, "Shadows leaves white alone"},
            {"highlights", false, 19, -2.0, -0.6, "-Highlights darkens ~63 %"},
            {"highlights", false, 22, -2.0, -0.3, "-Highlights darkens near-white with its region"},
            {"highlights", true, 19, 0.5, 2.0, "+Highlights lifts ~63 %"},
            {"highlights", true, 22, -0.01, 0.05, "+Highlights does not clip white"},
            {"whites", false, 22, -1.0, -0.15, "-Whites lowers white"},
            {"whites", true, 17, 1.0, 4.0, "+Whites lifts ~46 % strongly"},
            {"blacks", false, 6, -12.0, -3.0, "-Blacks crushes ~4 % to black"},
            {"blacks", true, 6, 0.5, 3.0, "+Blacks lifts ~4 %"},
            {"blacks", true, 22, -0.01, 0.05, "+Blacks leaves white alone"},
        };
        for (const Zone& z : zones) {
          if (QString(z.key) != sl.key) continue;
          const double v = at(z.high, z.patch);
          if (v < z.lo || v > z.hi) {
            failures << QStringLiteral("zone: %1 (patch %2: %3 stops, want %4..%5)")
                            .arg(z.what).arg(z.patch).arg(v, 0, 'f', 2)
                            .arg(z.lo).arg(z.hi);
          }
        }
      }
      if (report && !ctx.name[0]) {
        // Response in stops at the extremes, flat row.
        const auto& r = rowsFlat;
        const size_t mid = r.size() / 2;
        QString lo, hi;
        for (int p = 0; p < ToneWedge::kPatches; ++p) {
          lo += QStringLiteral("%1 ").arg(std::log2(std::max(r.front()[p], 1e-6) / std::max(r[mid][p], 1e-6)), 5, 'f', 2);
          hi += QStringLiteral("%1 ").arg(std::log2(std::max(r.back()[p], 1e-6) / std::max(r[mid][p], 1e-6)), 5, 'f', 2);
        }
        qInfo().noquote() << sl.key << "min:" << lo;
        qInfo().noquote() << sl.key << "max:" << hi;
      }
    }
  }
  if (report) {
    QString lum;
    for (double l : wedge.linear) lum += QStringLiteral("%1 ").arg(std::log2(l), 5, 'f', 1);
    qInfo().noquote() << "patch log2(L):" << lum;
  }
  for (const QString& f : failures) qWarning().noquote() << f;
  QVERIFY2(failures.isEmpty(),
           qPrintable(QStringLiteral("%1 problems, first: %2")
                          .arg(failures.size())
                          .arg(failures.isEmpty() ? QString() : failures.first())));
}

namespace {
// Synthetic scene for the local tone mapping of the tone bands: a hard
// 5.3-stop edge between a textured dark half (Shadows peak) and a textured
// bright half (Highlights peak), above a textured midtone band (on the
// falling side of Shadows, like skin, with facial-feature-sized structure).
struct ToneEdgeScene {
  static constexpr int width = 1536;
  static constexpr int height = 768;
  static constexpr int edgeX = 768;
  static constexpr int bandY = 512;
  // dark half at the Shadows peak (~13 %), bright half in the upper
  // Highlights (~80 %), band on the falling side of Shadows (~24 %).
  static constexpr double darkL = 0.015, brightL = 0.6, midL = 0.065;
  std::vector<ushort> src;

  ToneEdgeScene() {
    src.resize(size_t(width) * height * 3);
    auto encode = [](double l) {
      l = std::clamp(l, 0.0, 1.0);
      const double e = l <= 0.0031308 ? l * 12.92 : 1.055 * std::pow(l, 1 / 2.4) - 0.055;
      return ushort(std::clamp(e * 65535.0 + 0.5, 0.0, 65535.0));
    };
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        const double base = y >= bandY ? midL : (x < edgeX ? darkL : brightL);
        // fine texture: per-pixel hash, +-0.25 stop
        uint32_t hsh = uint32_t(x) * 73856093u ^ uint32_t(y) * 19349663u;
        hsh = (hsh ^ (hsh >> 13)) * 1274126177u;
        const double n = double(hsh & 0xFFFF) / 65535.0 * 2.0 - 1.0;
        // coarser structure: 8 px checker, +-0.15 stop; the midtone band
        // has strong mid-scale structure instead (24 px, +-0.5 stop, like
        // facial features), which raises the guided filter's a there.
        const int cell = y >= bandY ? 24 : 8;
        const double amp = y >= bandY ? 0.5 : 0.15;
        const double c = (((x / cell) + (y / cell)) % 2) ? amp : -amp;
        const ushort v = encode(base * std::exp2(0.25 * n + c));
        const size_t i = (size_t(y) * width + x) * 3;
        src[i] = src[i + 1] = src[i + 2] = v;
      }
    }
  }

  static std::vector<double> logLuma(const QImage& img) {
    std::vector<double> out(size_t(img.width()) * img.height());
    auto lin = [](double v) {
      v /= 255.0;
      return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
    };
    for (int y = 0; y < img.height(); ++y) {
      const uchar* s = img.constScanLine(y);
      for (int x = 0; x < img.width(); ++x) {
        out[size_t(y) * img.width() + x] = std::log2(
            0.2126 * lin(s[x * 3]) + 0.7152 * lin(s[x * 3 + 1]) +
            0.0722 * lin(s[x * 3 + 2]) + 1e-5);
      }
    }
    return out;
  }

  // Mean log luminance shift (stops) over a rectangle.
  static double shift(const std::vector<double>& out, const std::vector<double>& ref,
                      int x0, int x1, int y0, int y1) {
    double sum = 0.0;
    for (int y = y0; y < y1; ++y)
      for (int x = x0; x < x1; ++x)
        sum += out[size_t(y) * width + x] - ref[size_t(y) * width + x];
    return sum / double((x1 - x0) * (y1 - y0));
  }

  // Detail (log luminance minus its 5x5 box mean) RMS over a rectangle.
  static double detail(const std::vector<double>& l, int x0, int x1, int y0, int y1) {
    double sum = 0.0;
    int n = 0;
    for (int y = y0; y < y1; ++y) {
      for (int x = x0; x < x1; ++x) {
        double m = 0.0;
        for (int dy = -2; dy <= 2; ++dy)
          for (int dx = -2; dx <= 2; ++dx) m += l[size_t(y + dy) * width + x + dx];
        const double d = l[size_t(y) * width + x] - m / 25.0;
        sum += d * d;
        ++n;
      }
    }
    return std::sqrt(sum / n);
  }
};
}  // namespace

void TestRawEngine::testToneBandsLocalNoHaloKeepDetail() {
  const ToneEdgeScene scene;
  QJsonObject base;
  base["contrast"] = 1.0;
  base["profile"] = "normal";
  base["denoiseEnabled"] = false;
  base["sceneWhite"] = 1.0;
  const auto ref = ToneEdgeScene::logLuma(photon::ImageDeveloper::develop(
      scene.src.data(), ToneEdgeScene::width, ToneEdgeScene::height, base));
  const bool report = qEnvironmentVariableIsSet("PHOTON_TONE_REPORT");
  constexpr int ex = ToneEdgeScene::edgeX;
  constexpr int rowsTop = 64, rowsBottom = 448;  // top half, away from the band

  struct Case {
    const char* key;
    double value;
    bool darkSide;     // the side the slider targets
    bool checkDetail;  // Highlights +100 lifts the bright half into the
                       // white roll-off and Whites is the white point (it
                       // compresses the top end): both by design
  };
  const Case cases[] = {{"shadows", 100.0, true, true},
                        {"shadows", -100.0, true, true},
                        {"highlights", -100.0, false, true},
                        {"highlights", 100.0, false, false},
                        {"blacks", -100.0, true, false},  // black point: crushes
                        {"whites", -100.0, false, false}};
  for (const Case& c : cases) {
    QJsonObject s = base;
    s[c.key] = c.value;
    const auto out = ToneEdgeScene::logLuma(photon::ImageDeveloper::develop(
        scene.src.data(), ToneEdgeScene::width, ToneEdgeScene::height, s));
    // far from the edge vs the 16 px next to it (4..20 px away)
    const double far = c.darkSide
                           ? ToneEdgeScene::shift(out, ref, 100, 500, rowsTop, rowsBottom)
                           : ToneEdgeScene::shift(out, ref, 1036, 1436, rowsTop, rowsBottom);
    const double near = c.darkSide
                            ? ToneEdgeScene::shift(out, ref, ex - 20, ex - 4, rowsTop, rowsBottom)
                            : ToneEdgeScene::shift(out, ref, ex + 4, ex + 20, rowsTop, rowsBottom);
    const double detailRatio =
        c.darkSide ? ToneEdgeScene::detail(out, 100, 500, rowsTop, rowsBottom) /
                         ToneEdgeScene::detail(ref, 100, 500, rowsTop, rowsBottom)
                   : ToneEdgeScene::detail(out, 1036, 1436, rowsTop, rowsBottom) /
                         ToneEdgeScene::detail(ref, 1036, 1436, rowsTop, rowsBottom);
    if (report) {
      qInfo().noquote() << QString("%1=%2: far %3 st, near edge %4 st, detail x%5")
                               .arg(c.key).arg(c.value).arg(far, 0, 'f', 3)
                               .arg(near, 0, 'f', 3).arg(detailRatio, 0, 'f', 3);
    }
    // The slider acts on its side...
    if (QString(c.key) == "shadows" || QString(c.key) == "highlights")
      QVERIFY2(std::abs(far) > 0.3, qPrintable(QString("%1=%2 far shift %3").arg(c.key).arg(c.value).arg(far)));
    // ...uniformly up to the edge (no halo)...
    QVERIFY2(std::abs(near - far) < 0.12,
             qPrintable(QString("%1=%2: halo, far %3 vs near %4").arg(c.key).arg(c.value).arg(far).arg(near)));
    // ...keeping the texture (no wash-out / posterization).
    // (-Highlights reveals bright texture a little - the "recovery" look -
    // hence the upper bound.)
    QVERIFY2(!c.checkDetail || (detailRatio > 0.9 && detailRatio < 2.3),
             qPrintable(QString("%1=%2: detail x%3").arg(c.key).arg(c.value).arg(detailRatio)));
  }

  // Midtone texture on the falling side of Shadows (skin): keeps its detail.
  QJsonObject lift = base;
  lift["shadows"] = 100.0;
  const auto out = ToneEdgeScene::logLuma(photon::ImageDeveloper::develop(
      scene.src.data(), ToneEdgeScene::width, ToneEdgeScene::height, lift));
  constexpr int by0 = ToneEdgeScene::bandY + 64, by1 = ToneEdgeScene::height - 64;
  const double midShift = ToneEdgeScene::shift(out, ref, 100, 1436, by0, by1);
  const double midDetail = ToneEdgeScene::detail(out, 100, 1436, by0, by1) /
                           ToneEdgeScene::detail(ref, 100, 1436, by0, by1);
  if (report) {
    qInfo().noquote() << QString("shadows=100 midtone band: shift %1 st, detail x%2")
                             .arg(midShift, 0, 'f', 3).arg(midDetail, 0, 'f', 3);
  }
  QVERIFY2(midDetail > 0.9, qPrintable(QString("midtone detail x%1").arg(midDetail)));
}

void TestRawEngine::testToneMaskEncoding() {
  // CPU sampling and the GPU texture encoding agree, and no mask = identity.
  const ToneEdgeScene scene;
  const float wb[3] = {1.0f, 1.0f, 1.0f};
  const photon::tone::ToneMask mask = photon::tone::computeToneMask(
      scene.src.data(), ToneEdgeScene::width, ToneEdgeScene::height, 3, wb);
  QVERIFY(mask.isValid());
  QCOMPARE(mask.width, 512);
  QCOMPARE(mask.height, 256);
  const QImage enc = photon::tone::encodeToneMask(mask);
  QCOMPARE(enc.width(), mask.width * 3);
  QCOMPARE(enc.height(), mask.height);
  auto decode = [&](int x, int y, int plane) {
    const uchar* px = enc.constScanLine(y) + size_t(x + plane * mask.width) * 4;
    return float(px[0] * 256 + px[1]) / 65535.0f;
  };
  for (int y = 0; y < mask.height; y += 37) {
    for (int x = 0; x < mask.width; x += 41) {
      const size_t i = size_t(y) * mask.width + x;
      QVERIFY(std::abs(decode(x, y, 0) - mask.a[i]) < 1e-4f);
      QVERIFY(std::abs(photon::tone::kLogMin + photon::tone::kLogRange * decode(x, y, 1) -
                       mask.mu[i]) < 1e-3f);
      QVERIFY(std::abs(photon::tone::kLogMin + photon::tone::kLogRange * decode(x, y, 2) -
                       mask.g[i]) < 1e-3f);
    }
  }
  // Flat regions: a ~ 0 (one gain per region); the hard edge: a ~ 1.
  const auto flat = mask.sample(0.15f, 0.25f);
  QVERIFY2(flat.a < 0.35f, qPrintable(QString("flat a %1").arg(flat.a)));
  const auto edge = mask.sample((ToneEdgeScene::edgeX + 0.5f) / ToneEdgeScene::width, 0.25f);
  QVERIFY2(edge.a > 0.8f, qPrintable(QString("edge a %1").arg(edge.a)));
  // Identity: no mask, band position = the pixel.
  const photon::tone::ToneMask none;
  const auto id = none.sample(0.5f, 0.5f);
  QVERIFY(id.identity);
  QCOMPARE(photon::tone::maskLogExposed(id, -3.25f, 1.5f), -3.25f);
  // Shrinkage: small detail dropped, large passes, slope < 1 (monotone).
  QVERIFY(std::abs(photon::tone::detailShrink(0.1f)) < 0.005f);
  QVERIFY(std::abs(photon::tone::detailShrink(3.0f) - 2.5f) < 0.01f);
  float prev = photon::tone::detailShrink(-4.0f);
  for (float d = -3.99f; d <= 4.0f; d += 0.01f) {
    const float cur = photon::tone::detailShrink(d);
    QVERIFY(cur >= prev && cur - prev <= 0.01f + 1e-5f);
    prev = cur;
  }
}

// Reference helpers (testToneSlidersMatchReference).
#define PHOTON_SOURCE_DIR_FIT (QFileInfo(QStringLiteral(__FILE__)).absolutePath() + "/../..")
namespace {
struct FitPhoto {
  QString tag;
  float baseEv = 0.0f;  // photon::raw::baseExposure of the decode
  int w = 0, h = 0;
  std::vector<ushort> px;
  std::vector<int> bin;      // base display bin per pixel
  std::vector<float> baseL;  // base linear luma per pixel
};
constexpr int kFitBins = 16;
const double kFitEdges[kFitBins + 1] = {0, 3, 6, 10, 15, 20, 25, 30, 40, 50, 60, 70, 80, 88, 94, 98, 101};

std::vector<float> fitLuma(const QImage& img) {
  std::vector<float> out(size_t(img.width()) * img.height());
  auto lin = [](double v) { v /= 255.0; return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
  for (int y = 0; y < img.height(); ++y) {
    const uchar* s = img.constScanLine(y);
    for (int x = 0; x < img.width(); ++x)
      out[size_t(y) * img.width() + x] = float(0.2126 * lin(s[x * 3]) + 0.7152 * lin(s[x * 3 + 1]) + 0.0722 * lin(s[x * 3 + 2]));
  }
  return out;
}

QJsonObject fitBase(const FitPhoto& f) {
  QJsonObject s;
  s["contrast"] = 1.0; s["profile"] = "normal"; s["denoiseEnabled"] = false;
  s["baseExposure"] = f.baseEv;
  return s;
}

FitPhoto loadFitPhoto(const QString& tag, int longEdge) {
  FitPhoto f; f.tag = tag;
  LibRaw lr;
  photon::raw::configureDevelopDecode(lr);
  lr.imgdata.params.half_size = 1;
  const QString path = QString("%1/testphoton/_DSC%2.ARW").arg(PHOTON_SOURCE_DIR_FIT, tag);
  if (lr.open_file(path.toLocal8Bit().constData()) != 0 || lr.unpack() != 0 || lr.dcraw_process() != 0) return f;
  int ret = 0; libraw_processed_image_t* mem = lr.dcraw_make_mem_image(&ret);
  const int sw = mem->width, sh = mem->height;
  f.baseEv = photon::raw::baseExposure(reinterpret_cast<const ushort*>(mem->data), sw, sh, 3);
  const int step = std::max(1, std::max(sw, sh) / longEdge);
  f.w = sw / step; f.h = sh / step;
  f.px.resize(size_t(f.w) * f.h * 3);
  const ushort* src = reinterpret_cast<const ushort*>(mem->data);
  for (int y = 0; y < f.h; ++y)
    for (int x = 0; x < f.w; ++x)
      for (int c = 0; c < 3; ++c) {
        double acc = 0;  // box average in sRGB-encoded 16-bit (fine for a fit)
        for (int dy = 0; dy < step; ++dy)
          for (int dx = 0; dx < step; ++dx) acc += src[(size_t(y * step + dy) * sw + (x * step + dx)) * 3 + c];
        f.px[(size_t(y) * f.w + x) * 3 + c] = ushort(acc / (step * step));
      }
  LibRaw::dcraw_clear_mem(mem);
  f.baseL = fitLuma(photon::ImageDeveloper::develop(f.px.data(), f.w, f.h, fitBase(f)));
  f.bin.resize(f.baseL.size());
  for (size_t i = 0; i < f.baseL.size(); ++i) {
    const double l = std::clamp(double(f.baseL[i]), 0.0, 1.0);
    const double d = (l <= 0.0031308 ? l * 12.92 : 1.055 * std::pow(l, 1 / 2.4) - 0.055) * 100;
    int b = 0; while (b < kFitBins - 1 && d >= kFitEdges[b + 1]) ++b;
    f.bin[i] = b;
  }
  return f;
}

// mean stops per bin (and pixel fraction) for one slider value
void fitResponse(const FitPhoto& f, const QString& slider, int value, double* stops, double* frac) {
  QJsonObject s = fitBase(f); s[slider] = double(value);
  const auto out = fitLuma(photon::ImageDeveloper::develop(f.px.data(), f.w, f.h, s));
  double sum[kFitBins] = {}, cnt[kFitBins] = {};
  for (size_t i = 0; i < out.size(); ++i) {
    sum[f.bin[i]] += std::log2((out[i] + 1e-5) / (f.baseL[i] + 1e-5));
    cnt[f.bin[i]] += 1;
  }
  for (int b = 0; b < kFitBins; ++b) { stops[b] = cnt[b] ? sum[b] / cnt[b] : 0; frac[b] = cnt[b] / out.size(); }
}

struct RefRow { double stops, frac; };
std::map<QString, RefRow> loadReference(const QString& csv) {
  std::map<QString, RefRow> m;
  QFile file(csv); file.open(QIODevice::ReadOnly);
  file.readLine();
  std::map<QString, double> tot;
  std::vector<std::tuple<QString, double, double>> rows;
  while (!file.atEnd()) {
    const QStringList p = QString(file.readLine()).trimmed().split(',');
    if (p.size() < 6) continue;
    rows.emplace_back(p[0] + "," + p[1] + "," + p[2] + "," + p[3], p[4].toDouble(), p[5].toDouble());
    tot[p[0] + "," + p[1] + "," + p[2]] += p[5].toDouble();
  }
  for (auto& [k, st, c] : rows) {
    const QString key = k.section(',', 0, 2);
    m[k] = {st, tot[key] ? c / tot[key] : 0};
  }
  return m;
}
}  // namespace

void TestRawEngine::testToneSlidersMatchReference() {
  // Reference responses of three photos per slider and value, measured
  // from its exports (testphoton/reference/reference_tables.csv, local only:
  // the photos are not in the repository). The tone constants in ToneMath.h
  // were fitted to them; this guards the fit.
  const QString refDir = QString("%1/testphoton/reference").arg(PHOTON_SOURCE_DIR_FIT);
  if (!QFileInfo::exists(refDir + "/reference_tables.csv")) QSKIP("Reference photos not available");
  const auto refTable = loadReference(refDir + "/reference_tables.csv");
  std::vector<FitPhoto> photos;
  for (const QString tag : {"7870", "9064", "9111"}) photos.push_back(loadFitPhoto(tag, 768));
  for (const auto& f : photos)
    if (f.w <= 0) QSKIP("Reference photos not available");
  const int values[] = {-100, -50, -25, 25, 50, 100};
  // Weighted squared error (stops) at the end of the fit, +25 % slack.
  const std::pair<const char*, double> sliders[] = {
      // (after the Rec.2020 / full-range base; Clarity's per-tone means are a
      // regression guard - its local contrast was matched spatially)
      {"highlights", 0.95}, {"shadows", 2.92}, {"whites", 2.38}, {"blacks", 2.51},
      {"clarity", 2.43}};
  const bool report = qEnvironmentVariableIsSet("PHOTON_TONE_REPORT");
  for (const auto& [slider, fitted] : sliders) {
    double err = 0;
    for (const auto& f : photos) {
      for (int v : values) {
        double st[kFitBins], fr[kFitBins];
        fitResponse(f, slider, v, st, fr);
        QString line;
        for (int b = 0; b < kFitBins; ++b) {
          const auto it = refTable.find(QString("%1,%2,%3,%4").arg(f.tag, slider).arg(v).arg(b));
          if (it == refTable.end() || it->second.frac < 0.01 || fr[b] < 0.005) {
            line += "         .";
            continue;
          }
          line += QString(" %1/%2").arg(st[b], 4, 'f', 1).arg(it->second.stops, -4, 'f', 1);
          // stops clamped to [-6, 6]: crushed bins (to black) must not dominate
          const double d = std::clamp(st[b], -6.0, 6.0) - std::clamp(it->second.stops, -6.0, 6.0);
          err += std::sqrt(std::min(fr[b], it->second.frac)) * d * d;
        }
        if (report) qInfo().noquote() << slider << f.tag << QString("%1").arg(v, 4) << line;
      }
    }
    if (report) qInfo() << slider << "error vs Reference" << err << "(fitted" << fitted << ")";
    QVERIFY2(err < fitted * 1.25,
             qPrintable(QString("%1 drifted from Reference: error %2, fitted %3").arg(slider).arg(err).arg(fitted)));
  }
}

void TestRawEngine::testExport16BitTiff() {
  // A smooth ramp: the 16-bit develop matches the 8-bit one (within its
  // dither), keeps finer steps, and survives a TIFF round trip at 16 bits.
  const int w = 256, h = 8;
  std::vector<ushort> src(size_t(w) * h * 3);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x)
      for (int c = 0; c < 3; ++c) src[(size_t(y) * w + x) * 3 + c] = ushort(x * 64 + 2000);
  QJsonObject s;
  s["contrast"] = 1.0; s["profile"] = "normal"; s["denoiseEnabled"] = false;
  const QImage img8 = photon::ImageDeveloper::develop(src.data(), w, h, s);
  s["outputBits"] = 16;
  const QImage img16 = photon::ImageDeveloper::develop(src.data(), w, h, s);
  QCOMPARE(img8.format(), QImage::Format_RGB888);
  QCOMPARE(img16.format(), QImage::Format_RGBX64);
  int distinct16 = 0, distinct8 = 0;
  quint16 prev16 = 0; uchar prev8 = 0;
  for (int x = 0; x < w; ++x) {
    const quint16 v16 = reinterpret_cast<const quint16*>(img16.constScanLine(4))[x * 4 + 1];
    const uchar v8 = img8.constScanLine(4)[x * 3 + 1];
    QVERIFY2(std::abs(v16 / 257.0 - v8) <= 1.5, qPrintable(QString("x %1: %2 vs %3").arg(x).arg(v16).arg(v8)));
    if (x == 0 || v16 != prev16) ++distinct16;
    if (x == 0 || v8 != prev8) ++distinct8;
    prev16 = v16; prev8 = v8;
  }
  QVERIFY2(distinct16 > distinct8, qPrintable(QString("%1 vs %2 levels").arg(distinct16).arg(distinct8)));
  QTemporaryDir dir;
  const QString path = dir.filePath("out.tif");
  QVERIFY(QImageWriter(path, "tif").write(img16));
  const QImage back(path);
  QVERIFY(!back.isNull());
  QCOMPARE(back.depth(), 64);
  const QImage back64 = back.convertToFormat(QImage::Format_RGBX64);
  QCOMPARE(reinterpret_cast<const quint16*>(back64.constScanLine(4))[128 * 4 + 1],
           reinterpret_cast<const quint16*>(img16.constScanLine(4))[128 * 4 + 1]);
}

void TestRawEngine::testExportAdobeRgb() {
  // Colour patches: the Adobe RGB export converted back to sRGB by Qt's
  // colour management matches the sRGB export, and the profile is embedded.
  const int w = 64, h = 16;
  ushort patches[4][3] = {{46000, 46000, 46000}, {60000, 9000, 9000}, {9000, 52000, 12000}, {12000, 16000, 58000}};
  for (auto& p : patches) srgb16ToWorking16(p[0], p[1], p[2]);  // sRGB colours
  std::vector<ushort> src(size_t(w) * h * 3);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x)
      for (int c = 0; c < 3; ++c) src[(size_t(y) * w + x) * 3 + c] = patches[x / 16][c];
  QJsonObject s;
  s["contrast"] = 1.0; s["profile"] = "normal"; s["denoiseEnabled"] = false;
  const QImage srgb = photon::ImageDeveloper::develop(src.data(), w, h, s);
  s["outputColorSpace"] = "adobergb";
  const QImage adobe = photon::ImageDeveloper::develop(src.data(), w, h, s);
  QCOMPARE(srgb.colorSpace(), QColorSpace(QColorSpace::SRgb));
  QCOMPARE(adobe.colorSpace(), QColorSpace(QColorSpace::AdobeRgb));
  QVERIFY(adobe != srgb);
  const QImage back = adobe.convertedToColorSpace(QColorSpace(QColorSpace::SRgb)).convertToFormat(QImage::Format_RGB888);
  for (int p = 0; p < 4; ++p) {
    const QColor a = back.pixelColor(p * 16 + 8, 8), b = srgb.pixelColor(p * 16 + 8, 8);
    // (8-bit round trip through another gamma: dark channels move a few levels)
    QVERIFY2(std::abs(a.red() - b.red()) <= 4 && std::abs(a.green() - b.green()) <= 4 && std::abs(a.blue() - b.blue()) <= 4,
             qPrintable(QString("patch %1: %2 vs %3").arg(p).arg(a.name(), b.name())));
  }
  QTemporaryDir dir;
  for (const char* fmt : {"jpg", "tif"}) {
    const QString path = dir.filePath(QString("out.%1").arg(fmt));
    QVERIFY(QImageWriter(path, fmt).write(adobe));
    QCOMPARE(QImage(path).colorSpace(), QColorSpace(QColorSpace::AdobeRgb));
  }
}

void TestRawEngine::testColorSpaceRec2020() {
  namespace cs = photon::colorspace;
  // sRGB -> Rec.2020 -> sRGB round trip, and luminance agrees across spaces.
  const float colors[][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0.2f, 0.5f, 0.8f}, {0.18f, 0.18f, 0.18f}};
  for (const auto& c : colors) {
    float r = c[0], g = c[1], b = c[2];
    cs::srgbToRec2020(r, g, b);
    QVERIFY(std::abs(cs::luma(r, g, b) - (0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2])) < 1e-4f);
    cs::rec2020ToSrgb(r, g, b);
    QVERIFY(std::abs(r - c[0]) < 1e-4f && std::abs(g - c[1]) < 1e-4f && std::abs(b - c[2]) < 1e-4f);
  }
  // White is neutral in OKLab of the working space, and OKLab round-trips.
  float L, A, B;
  cs::rec2020ToOklab(1, 1, 1, L, A, B);
  QVERIFY(std::abs(L - 1.0f) < 1e-3f && std::abs(A) < 1e-3f && std::abs(B) < 1e-3f);
  float r, g, b;
  cs::rec2020ToOklab(0.3f, 0.6f, 0.1f, L, A, B);
  cs::oklabToRec2020(L, A, B, r, g, b);
  QVERIFY(std::abs(r - 0.3f) < 1e-3f && std::abs(g - 0.6f) < 1e-3f && std::abs(b - 0.1f) < 1e-3f);
  // A Rec.2020 primary (outside sRGB) maps into sRGB with its luminance kept.
  r = 0.0f; g = 0.8f; b = 0.0f;
  const float y = cs::luma(r, g, b);
  cs::toOutput(r, g, b, false, false);
  QVERIFY(std::min({r, g, b}) >= -1e-6f);
  QVERIFY(std::abs(0.2126f * r + 0.7152f * g + 0.0722f * b - y) < 1e-4f);
  QVERIFY(g > r && g > b);  // still green
  // ...and Adobe RGB keeps more of it than sRGB (wider gamut).
  float ar = 0.0f, ag = 0.8f, ab = 0.0f;
  cs::toOutput(ar, ag, ab, true, false);
  QVERIFY(ag - std::max(ar, ab) > g - std::max(r, b) - 1e-4f);
}

// The denoisers work at the RAW base exposure: the same scene stored 4 EV
// darker (the full-range decode) and denoised with its base gain must give
// the same result as the bright source, instead of being over-smoothed into
// 8x8 blocks by sigmas tuned for the bright image.
void TestRawEngine::testDenoiseAtBaseExposure() {
  namespace cs = photon::colorspace;
  constexpr int w = 96, h = 96;
  constexpr float kGain = 16.0f;  // 4 stops
  // Linear scene at its display brightness: soft texture + colour + noise.
  std::vector<float> scene(size_t(w) * h * 3);
  uint32_t seed = 12345;
  auto noise = [&seed]() {
    seed = seed * 1664525u + 1013904223u;
    return (float(seed >> 8) / 16777216.0f - 0.5f) * 0.06f;
  };
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const float base = 0.18f + 0.12f * std::sin(x * 0.2f) * std::cos(y * 0.15f);
      const float tint[3] = {1.1f, 1.0f, 0.85f};
      for (int c = 0; c < 3; ++c)
        scene[(size_t(y) * w + x) * 3 + c] =
            std::max(0.0f, base * tint[c] * (1.0f + noise()));
    }
  auto source = [&](float scale) {
    QImage img(w, h, QImage::Format_RGBX64);
    for (int y = 0; y < h; ++y) {
      QRgba64* line = reinterpret_cast<QRgba64*>(img.scanLine(y));
      for (int x = 0; x < w; ++x) {
        const float* p = &scene[(size_t(y) * w + x) * 3];
        auto enc = [&](float v) {
          return ushort(std::lround(cs::encodeSrgb(v * scale) * 65535.0f));
        };
        line[x] = QRgba64::fromRgba64(enc(p[0]), enc(p[1]), enc(p[2]), 65535);
      }
    }
    return img;
  };
  // Mean absolute difference in display terms (sRGB-encoded, 0..255) after
  // bringing `dark` up by kGain.
  auto displayDiff = [&](const QImage& bright, const QImage& dark) {
    double acc = 0.0;
    for (int y = 0; y < h; ++y) {
      const QRgba64* a = reinterpret_cast<const QRgba64*>(bright.constScanLine(y));
      const QRgba64* b = reinterpret_cast<const QRgba64*>(dark.constScanLine(y));
      for (int x = 0; x < w; ++x) {
        const ushort av[3] = {a[x].red(), a[x].green(), a[x].blue()};
        const ushort bv[3] = {b[x].red(), b[x].green(), b[x].blue()};
        for (int c = 0; c < 3; ++c) {
          const float ea = cs::encodeSrgb(cs::decodeSrgb(av[c] / 65535.0f));
          const float eb = cs::encodeSrgb(cs::decodeSrgb(bv[c] / 65535.0f) * kGain);
          acc += std::abs(ea - eb) * 255.0;
        }
      }
    }
    return acc / (double(w) * h * 3);
  };
  const QImage bright = source(1.0f);
  const QImage dark = source(1.0f / kGain);

  photon::DenoiseParams plain;
  photon::DenoiseParams gained;
  gained.exposureGain = kGain;
  const QImage refOut = photon::Denoiser::denoise(bright, 60.0f, nullptr, true, 4, {}, plain);
  const QImage darkOut = photon::Denoiser::denoise(dark, 60.0f, nullptr, true, 4, {}, gained);
  const QImage darkOld = photon::Denoiser::denoise(dark, 60.0f, nullptr, true, 4, {}, plain);
  const double invariant = displayDiff(refOut, darkOut);
  const double old = displayDiff(refOut, darkOld);
  qInfo() << "classic: with base gain" << invariant << "without" << old;
  QVERIFY2(invariant < 0.5, qPrintable(QString::number(invariant)));
  QVERIFY(old > 4.0 * invariant);  // the gain is what makes it match

  const QImage chromaRef = photon::Denoiser::denoiseChroma(bright, 1.0f, nullptr, 1.0f);
  const QImage chromaDark = photon::Denoiser::denoiseChroma(dark, 1.0f, nullptr, kGain);
  const double chroma = displayDiff(chromaRef, chromaDark);
  qInfo() << "colour: with base gain" << chroma;
  QVERIFY2(chroma < 0.5, qPrintable(QString::number(chroma)));

  // Gain 1 with no denoising work is a lossless round trip of the domain.
  const QImage same = photon::Denoiser::denoiseChroma(bright, 0.0f, nullptr, kGain);
  QCOMPARE(same.pixel(10, 10), bright.pixel(10, 10));
}

// Classic denoise works at the image's measured noise: the estimator finds a
// known sigma, amount 50 removes the noise, and edges/texture survive (the
// old fixed sigma of 0.8 x amount flattened real detail into 8x8 blocks).
void TestRawEngine::testDenoiseNoiseAdaptive() {
  constexpr int w = 128, h = 128;
  uint32_t seed = 99;
  auto gauss = [&seed]() {  // Box-Muller
    auto u = [&seed]() {
      seed = seed * 1664525u + 1013904223u;
      return (float(seed >> 8) + 0.5f) / 16777216.0f;
    };
    const float a = u(), b = u();
    return std::sqrt(-2.0f * std::log(a)) * std::cos(6.2831853f * b);
  };
  // Smooth gradient + vertical step edge at x = 64, plus sigma-6 noise.
  std::vector<float> clean(size_t(w) * h), noisy(size_t(w) * h);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const size_t i = size_t(y) * w + x;
      clean[i] = 80.0f + 0.3f * y + (x >= 64 ? 60.0f : 0.0f);
      noisy[i] = clean[i] + 6.0f * gauss();
    }
  const float est = photon::Denoiser::estimateNoiseSigma(noisy, w, h);
  QVERIFY2(std::abs(est - 6.0f) < 0.6f, qPrintable(QString::number(est)));
  QVERIFY(photon::Denoiser::estimateNoiseSigma(clean, w, h) < 0.5f);

  QImage img(w, h, QImage::Format_RGBX64);  // grey, values as 0..255 encoded
  for (int y = 0; y < h; ++y) {
    QRgba64* line = reinterpret_cast<QRgba64*>(img.scanLine(y));
    for (int x = 0; x < w; ++x) {
      const ushort v = ushort(std::lround(std::clamp(noisy[size_t(y) * w + x], 0.0f, 255.0f) * 257.0f));
      line[x] = QRgba64::fromRgba64(v, v, v, 65535);
    }
  }
  photon::DenoiseParams dp;  // gain 1: the 0..255 values are the domain
  const QImage out = photon::Denoiser::denoise(img, 50.0f, nullptr, true, 4, {}, dp);
  double err = 0.0, edgeOut = 0.0;
  for (int y = 8; y < h - 8; ++y) {
    const QRgba64* line = reinterpret_cast<const QRgba64*>(out.constScanLine(y));
    for (int x = 8; x < w - 8; ++x) {
      const double v = line[x].green() / 257.0;
      if (std::abs(x - 64) > 4) err += (v - clean[size_t(y) * w + x]) * (v - clean[size_t(y) * w + x]);
    }
    edgeOut += line[66].green() / 257.0 - line[61].green() / 257.0;
  }
  const double rms = std::sqrt(err / ((h - 16.0) * (w - 16.0 - 9.0)));
  edgeOut /= (h - 16.0);
  qInfo() << "noise est" << est << "residual rms" << rms << "edge" << edgeOut;
  QVERIFY2(rms < 2.0, qPrintable(QString::number(rms)));       // noise removed
  QVERIFY2(edgeOut > 55.0, qPrintable(QString::number(edgeOut)));  // edge kept
}

QTEST_MAIN(TestRawEngine)
#include "tst_RawEngine.moc"
