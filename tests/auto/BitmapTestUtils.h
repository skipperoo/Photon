#pragma once

#include <jpeglib.h>
#include <lcms2.h>
#include <tiffio.h>

#include <QByteArray>
#include <QString>
#include <csetjmp>
#include <cstdio>
#include <cstring>
#include <vector>

namespace BitmapTestUtils {

inline QByteArray makeExifTiffBlob(quint16 orientation, const char* make,
                                   int iso, int fNumberNumerator,
                                   int fNumberDenominator) {
  QByteArray blob;
  auto appendU16 = [&](quint16 value) {
    blob.append(char(value & 0xFF));
    blob.append(char((value >> 8) & 0xFF));
  };
  auto appendU32 = [&](quint32 value) {
    for (int i = 0; i < 4; ++i) {
      blob.append(char((value >> (8 * i)) & 0xFF));
    }
  };

  const quint32 ifd0Offset = 8;
  const quint32 ifd0EntryCount = 3;
  const quint32 ifd0Size = 2 + ifd0EntryCount * 12 + 4;
  const quint32 makeDataOffset = ifd0Offset + ifd0Size;
  const quint32 makeLength = quint32(std::strlen(make)) + 1;
  const quint32 exifIfdOffset = makeDataOffset + makeLength;
  const quint32 exifEntryCount = 2;
  const quint32 exifIfdSize = 2 + exifEntryCount * 12 + 4;
  const quint32 fNumberDataOffset = exifIfdOffset + exifIfdSize;

  blob.append("II", 2);
  appendU16(42);
  appendU32(ifd0Offset);

  appendU16(ifd0EntryCount);
  appendU16(0x010F);
  appendU16(2);
  appendU32(makeLength);
  appendU32(makeDataOffset);
  appendU16(0x0112);
  appendU16(3);
  appendU32(1);
  appendU16(orientation);
  appendU16(0);
  appendU16(0x8769);
  appendU16(4);
  appendU32(1);
  appendU32(exifIfdOffset);
  appendU32(0);
  blob.append(make, int(makeLength) - 1);
  blob.append('\0');

  appendU16(exifEntryCount);
  appendU16(0x8827);
  appendU16(3);
  appendU32(1);
  appendU16(quint16(iso));
  appendU16(0);
  appendU16(0x829D);
  appendU16(5);
  appendU32(1);
  appendU32(fNumberDataOffset);
  appendU32(0);
  appendU32(quint32(fNumberNumerator));
  appendU32(quint32(fNumberDenominator));
  return blob;
}

struct JpegWriteErrorManager {
  jpeg_error_mgr pub;
  jmp_buf jump;
};

inline void jpegWriteErrorExit(j_common_ptr cinfo) {
  longjmp(reinterpret_cast<JpegWriteErrorManager*>(cinfo->err)->jump, 1);
}

inline bool writeJpegFile(const QString& path, int width, int height,
                          const std::vector<uint8_t>& rgb,
                          const QByteArray& app1 = {}) {
  FILE* file = std::fopen(QFile::encodeName(path).constData(), "wb");
  if (!file) return false;

  jpeg_compress_struct cinfo;
  JpegWriteErrorManager errorManager;
  std::memset(&cinfo, 0, sizeof(cinfo));
  cinfo.err = jpeg_std_error(&errorManager.pub);
  errorManager.pub.error_exit = jpegWriteErrorExit;
  if (setjmp(errorManager.jump)) {
    jpeg_destroy_compress(&cinfo);
    std::fclose(file);
    return false;
  }

  jpeg_create_compress(&cinfo);
  jpeg_stdio_dest(&cinfo, file);
  cinfo.image_width = width;
  cinfo.image_height = height;
  cinfo.input_components = 3;
  cinfo.in_color_space = JCS_RGB;
  jpeg_set_defaults(&cinfo);
  jpeg_set_quality(&cinfo, 100, TRUE);
  jpeg_start_compress(&cinfo, TRUE);

  if (!app1.isEmpty()) {
    jpeg_write_marker(&cinfo, JPEG_APP0 + 1,
                      reinterpret_cast<const JOCTET*>(app1.constData()),
                      app1.size());
  }

  while (cinfo.next_scanline < cinfo.image_height) {
    JSAMPROW row = const_cast<JSAMPROW>(
        rgb.data() + size_t(cinfo.next_scanline) * size_t(width) * 3);
    jpeg_write_scanlines(&cinfo, &row, 1);
  }

  jpeg_finish_compress(&cinfo);
  jpeg_destroy_compress(&cinfo);
  std::fclose(file);
  return true;
}

inline bool writeTiff16File(const QString& path, int width, int height,
                            const std::vector<uint16_t>& rgb,
                            uint16_t orientation = ORIENTATION_TOPLEFT,
                            const QByteArray& icc = {},
                            const char* make = nullptr,
                            const char* model = nullptr) {
  TIFF* tif = TIFFOpen(QFile::encodeName(path).constData(), "w");
  if (!tif) return false;

  TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, quint32(width));
  TIFFSetField(tif, TIFFTAG_IMAGELENGTH, quint32(height));
  TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 3);
  TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 16);
  TIFFSetField(tif, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT);
  TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
  TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
  TIFFSetField(tif, TIFFTAG_ORIENTATION, orientation);
  TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, quint32(height));
  if (!icc.isEmpty()) {
    TIFFSetField(tif, TIFFTAG_ICCPROFILE, quint32(icc.size()), icc.constData());
  }
  if (make) TIFFSetField(tif, TIFFTAG_MAKE, make);
  if (model) TIFFSetField(tif, TIFFTAG_MODEL, model);

  for (int y = 0; y < height; ++y) {
    const uint16_t* row = rgb.data() + size_t(y) * size_t(width) * 3;
    if (TIFFWriteScanline(tif, const_cast<uint16_t*>(row), quint32(y)) < 0) {
      TIFFClose(tif);
      return false;
    }
  }
  TIFFClose(tif);
  return true;
}

inline QByteArray makeLinearSrgbProfile() {
  cmsCIExyY whitePoint = {0.3127, 0.3290, 1.0};
  cmsCIExyYTRIPLE primaries = {
      {0.64, 0.33, 1.0}, {0.30, 0.60, 1.0}, {0.15, 0.06, 1.0}};
  cmsToneCurve* gamma = cmsBuildGamma(nullptr, 1.0);
  cmsToneCurve* curves[3] = {gamma, gamma, gamma};
  cmsHPROFILE profile = cmsCreateRGBProfile(&whitePoint, &primaries, curves);

  QByteArray icc;
  cmsUInt32Number size = 0;
  if (profile && cmsSaveProfileToMem(profile, nullptr, &size) && size > 0) {
    icc.resize(int(size));
    cmsSaveProfileToMem(profile, icc.data(), &size);
  }
  if (profile) cmsCloseProfile(profile);
  cmsFreeToneCurve(gamma);
  return icc;
}

}  // namespace BitmapTestUtils
