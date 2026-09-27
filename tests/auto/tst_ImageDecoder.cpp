#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "BitmapTestUtils.h"
#include "ImageDecoder.h"

using namespace photon;
using namespace BitmapTestUtils;

class TestImageDecoder : public QObject {
  Q_OBJECT

 private slots:
  void testExtensionClassification();
  void testTiff16BitExactPixels();
  void testTiffOrientation();
  void testTiffMetadata();
  void testTiffIccConversion();
  void testJpegRoundTrip();
  void testJpegExifMarker();
};

void TestImageDecoder::testExtensionClassification() {
  QVERIFY(ImageDecoder::isBitmap("photo.JPG"));
  QVERIFY(ImageDecoder::isBitmap("photo.jpeg"));
  QVERIFY(ImageDecoder::isBitmap("photo.TIF"));
  QVERIFY(ImageDecoder::isBitmap("photo.tiff"));
  QVERIFY(!ImageDecoder::isBitmap("photo.nef"));
  QVERIFY(ImageDecoder::isRaw("photo.NEF"));
  QVERIFY(ImageDecoder::isRaw("photo.cr2"));
  QVERIFY(!ImageDecoder::isRaw("photo.jpg"));
}

void TestImageDecoder::testTiff16BitExactPixels() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.filePath("pixels.tif");

  const int width = 3;
  const int height = 2;
  std::vector<uint16_t> pixels(size_t(width) * size_t(height) * 3);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const size_t base = (size_t(y) * size_t(width) + size_t(x)) * 3;
      pixels[base + 0] = uint16_t(1000 * x + 1);
      pixels[base + 1] = uint16_t(2000 * y + 2);
      pixels[base + 2] = uint16_t(30000 + x + y);
    }
  }
  QVERIFY(writeTiff16File(path, width, height, pixels));

  const BitmapImage image = ImageDecoder::decode(path);
  QCOMPARE(image.width, width);
  QCOMPARE(image.height, height);
  QCOMPARE(image.orientation, 1);
  QCOMPARE(image.pixels, pixels);
}

void TestImageDecoder::testTiffOrientation() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.filePath("rotated.tif");

  const int width = 2;
  const int height = 3;
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
  QVERIFY(writeTiff16File(path, width, height, pixels, ORIENTATION_RIGHTTOP));

  const BitmapImage image = ImageDecoder::decode(path);
  QCOMPARE(image.orientation, 6);
  QCOMPARE(image.width, height);
  QCOMPARE(image.height, width);

  // Orientation 6 rotates 90 degrees clockwise:
  // source (x, y) -> destination (height - 1 - y, x)
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const uint16_t expected = uint16_t(1000 * (y * width + x) + 1);
      const int dx = height - 1 - y;
      const int dy = x;
      const size_t destinationBase =
          (size_t(dy) * size_t(image.width) + size_t(dx)) * 3;
      QCOMPARE(image.pixels[destinationBase + 0], expected);
      QCOMPARE(image.pixels[destinationBase + 1], expected);
      QCOMPARE(image.pixels[destinationBase + 2], expected);
    }
  }
}

void TestImageDecoder::testTiffMetadata() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.filePath("metadata.tif");

  const int width = 2;
  const int height = 2;
  std::vector<uint16_t> pixels(size_t(width) * size_t(height) * 3, 4000);
  QVERIFY(writeTiff16File(path, width, height, pixels, ORIENTATION_TOPLEFT, {},
                          "PhotonTest", "ModelX"));

  const QVariantMap metadata = ImageDecoder::readMetadata(path);
  QCOMPARE(metadata.value("make").toString(), QString("PhotonTest"));
  QCOMPARE(metadata.value("model").toString(), QString("ModelX"));
  QCOMPARE(metadata.value("lensModel").toString(), QString("Unknown Lens"));
  QCOMPARE(metadata.value("artist").toString(), QString("-"));

  const BitmapImage image = ImageDecoder::decode(path);
  QCOMPARE(image.metadata.value("make").toString(), QString("PhotonTest"));
  QCOMPARE(image.metadata.value("width").toInt(), width);
  QCOMPARE(image.metadata.value("height").toInt(), height);
}

void TestImageDecoder::testTiffIccConversion() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());

  const QByteArray icc = makeLinearSrgbProfile();
  QVERIFY(!icc.isEmpty());

  const int width = 1;
  const int height = 1;
  std::vector<uint16_t> pixels = {0x8000, 0x8000, 0x8000};

  const QString withIcc = dir.filePath("with_icc.tif");
  QVERIFY(writeTiff16File(withIcc, width, height, pixels, ORIENTATION_TOPLEFT,
                          icc));

  const QString withoutIcc = dir.filePath("without_icc.tif");
  QVERIFY(writeTiff16File(withoutIcc, width, height, pixels));

  const BitmapImage converted = ImageDecoder::decode(withIcc);
  QCOMPARE(converted.width, 1);
  // Linear 0.5 (gamma 1.0 profile) encodes to ~0.735 in sRGB (~48190).
  QVERIFY(converted.pixels[0] > 46000);
  QVERIFY(converted.pixels[0] < 50000);

  const BitmapImage untouched = ImageDecoder::decode(withoutIcc);
  QCOMPARE(untouched.pixels[0], uint16_t(0x8000));
}

void TestImageDecoder::testJpegRoundTrip() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.filePath("solid.jpg");

  const int width = 4;
  const int height = 4;
  std::vector<uint8_t> pixels(size_t(width) * size_t(height) * 3);
  for (size_t i = 0; i < size_t(width) * size_t(height); ++i) {
    pixels[i * 3 + 0] = 200;
    pixels[i * 3 + 1] = 100;
    pixels[i * 3 + 2] = 50;
  }
  QVERIFY(writeJpegFile(path, width, height, pixels));

  const BitmapImage image = ImageDecoder::decode(path);
  QCOMPARE(image.width, width);
  QCOMPARE(image.height, height);
  QCOMPARE(image.orientation, 1);
  for (size_t i = 0; i < size_t(width) * size_t(height); ++i) {
    QVERIFY(std::abs(int(image.pixels[i * 3 + 0]) - 200 * 257) < 2570);
    QVERIFY(std::abs(int(image.pixels[i * 3 + 1]) - 100 * 257) < 2570);
    QVERIFY(std::abs(int(image.pixels[i * 3 + 2]) - 50 * 257) < 2570);
  }
}

void TestImageDecoder::testJpegExifMarker() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.filePath("exif.jpg");

  const int width = 4;
  const int height = 2;
  std::vector<uint8_t> pixels(size_t(width) * size_t(height) * 3);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const size_t base = (size_t(y) * size_t(width) + size_t(x)) * 3;
      pixels[base + 0] = uint8_t(50 * x + 10);
      pixels[base + 1] = uint8_t(50 * y + 20);
      pixels[base + 2] = 30;
    }
  }

  QByteArray app1("Exif\0\0", 6);
  app1.append(makeExifTiffBlob(6, "TestCam", 200, 28, 10));
  QVERIFY(writeJpegFile(path, width, height, pixels, app1));

  const BitmapImage image = ImageDecoder::decode(path);
  QCOMPARE(image.orientation, 6);
  QCOMPARE(image.width, height);
  QCOMPARE(image.height, width);
  QCOMPARE(image.metadata.value("make").toString(), QString("TestCam"));
  QCOMPARE(image.metadata.value("iso").toInt(), 200);
  QCOMPARE(image.metadata.value("aperture").toString(), QString("f/2.8"));
}

QTEST_MAIN(TestImageDecoder)
#include "tst_ImageDecoder.moc"
