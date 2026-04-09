#include <cmath>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QtTest>

#include "RawEngine.h"

class TestRawEngine : public QObject {
  Q_OBJECT

 private slots:
  void testLoadInvalidFile();
  void testLoadValidFile();
  void testProperties();
  void testPhoton001MultipassUsesFloatTargets();
  void testSwitchingSourceResetsExposureAndContrast();
  void testApplyGeometryTransformsStraightenKeepsFullFrame();
  void testApplyGeometryTransformsCropRectOnRotatedFrame();
  void testApplyGeometryTransformsCropPreservesAspectAndFocus();
};

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

  verifySourceUsesFloatTarget("photon001GaussianSmallPass");
  verifySourceUsesFloatTarget("photon001GaussianBigPass");
  verifySourceUsesFloatTarget("photon001LogPass");
  verifySourceUsesFloatTarget("photon001MinMaxMeanPass");
  verifySourceUsesFloatTarget("photon001MomentsPass");
  verifySourceUsesFloatTarget("photon001ReductionPass");
  verifySourceUsesFloatTarget("photon001DeltaPass");
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
