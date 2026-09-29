#include "ImageDecoder.h"

#include <jpeglib.h>
#include <lcms2.h>
#include <tiffio.h>

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <algorithm>
#include <array>
#include <cmath>
#include <csetjmp>
#include <cstdio>
#include <cstring>
#include <functional>

#include "CrashReporter.h"
#include "LogManager.h"

namespace photon {

namespace {

const char* kLogPrefix = "[ ImageDecoder.cpp ] - ";

void logError(const QString& message) {
  LogManager::instance()->log(QString(kLogPrefix) + message, PHOTON_ERROR);
}

void logDebug(const QString& message) {
  LogManager::instance()->log(QString(kLogPrefix) + message, PHOTON_DEBUG);
}

constexpr quint16 kExifTagImageWidth = 0x0100;
constexpr quint16 kExifTagImageHeight = 0x0101;
constexpr quint16 kExifTagMake = 0x010F;
constexpr quint16 kExifTagModel = 0x0110;
constexpr quint16 kExifTagOrientation = 0x0112;
constexpr quint16 kExifTagArtist = 0x013B;
constexpr quint16 kExifTagDateTime = 0x0132;
constexpr quint16 kExifTagExifIfd = 0x8769;
constexpr quint16 kExifTagIccProfile = 0x8773;

constexpr quint16 kExifTagExposureTime = 0x829A;
constexpr quint16 kExifTagFNumber = 0x829D;
constexpr quint16 kExifTagIso = 0x8827;
constexpr quint16 kExifTagDateTimeOriginal = 0x9003;
constexpr quint16 kExifTagFocalLength = 0x920A;
constexpr quint16 kExifTagLensModel = 0xA434;

// Maps an EXIF/TIFF orientation tag to the value understood by the renderer
// (1, 3, 6 or 8), mirroring the LibRaw flip mapping used for RAW files.
int mapOrientation(int rawOrientation) {
  switch (rawOrientation) {
    case 3:
      return 3;
    case 6:
      return 6;
    case 8:
    case 5:
      return 8;
    default:
      return 1;
  }
}

// ---------------------------------------------------------------------------
// Minimal TIFF/EXIF IFD reader
// ---------------------------------------------------------------------------

struct TiffEntry {
  quint16 tag = 0;
  quint16 type = 0;
  quint32 count = 0;
  std::array<uint8_t, 4> valueField{};
};

size_t tiffTypeSize(quint16 type) {
  switch (type) {
    case 1:  // BYTE
    case 2:  // ASCII
    case 6:  // SBYTE
    case 7:  // UNDEFINED
      return 1;
    case 3:  // SHORT
    case 8:  // SSHORT
      return 2;
    case 4:   // LONG
    case 9:   // SLONG
    case 11:  // FLOAT
      return 4;
    case 5:   // RATIONAL
    case 10:  // SRATIONAL
    case 12:  // DOUBLE
      return 8;
    default:
      return 0;
  }
}

class TiffReader {
 public:
  using ReadFn = std::function<bool(qint64 pos, void* dst, qint64 len)>;

  TiffReader(ReadFn read, qint64 totalSize)
      : m_read(std::move(read)), m_totalSize(totalSize) {}

  bool init(qint64 tiffBase) {
    m_base = tiffBase;
    uint8_t header[8] = {};
    if (!m_read(m_base, header, sizeof(header))) return false;
    if (header[0] == 'I' && header[1] == 'I') {
      m_littleEndian = true;
    } else if (header[0] == 'M' && header[1] == 'M') {
      m_littleEndian = false;
    } else {
      return false;
    }
    if (read16(header + 2) != 42) return false;
    m_ifd0Offset = qint64(read32(header + 4));
    m_valid = true;
    return true;
  }

  bool isValid() const { return m_valid; }
  qint64 ifd0Offset() const { return m_ifd0Offset; }

  template <typename Callback>
  bool forEachEntry(qint64 ifdOffset, Callback&& callback) {
    if (!m_valid || ifdOffset <= 0) return false;
    uint8_t countBytes[2] = {};
    if (!m_read(m_base + ifdOffset, countBytes, sizeof(countBytes)))
      return false;
    const quint32 entryCount = read16(countBytes);
    if (entryCount > 4096) return false;

    for (quint32 i = 0; i < entryCount; ++i) {
      uint8_t entryBytes[12] = {};
      if (!m_read(m_base + ifdOffset + 2 + qint64(i) * 12, entryBytes,
                  sizeof(entryBytes))) {
        return false;
      }
      TiffEntry entry;
      entry.tag = read16(entryBytes);
      entry.type = read16(entryBytes + 2);
      entry.count = read32(entryBytes + 4);
      std::copy(entryBytes + 8, entryBytes + 12, entry.valueField.begin());
      if (!callback(entry)) return false;
    }
    return true;
  }

  bool entryBytes(const TiffEntry& entry, QByteArray& out) {
    const size_t typeSize = tiffTypeSize(entry.type);
    if (typeSize == 0) return false;
    const qint64 total = qint64(typeSize) * qint64(entry.count);
    if (total < 0 || total > 16 * 1024 * 1024) return false;
    out.resize(total);
    if (total <= 4) {
      std::memcpy(out.data(), entry.valueField.data(), size_t(total));
      return true;
    }
    const qint64 offset = read32(entry.valueField.data());
    if (offset <= 0 || offset + total > m_totalSize) return false;
    return m_read(m_base + offset, out.data(), total);
  }

  QString asciiValue(const TiffEntry& entry) {
    QByteArray bytes;
    if (!entryBytes(entry, bytes)) return QString();
    while (!bytes.isEmpty() && bytes.endsWith('\0')) bytes.chop(1);
    return QString::fromUtf8(bytes).trimmed();
  }

  quint32 unsignedValue(const TiffEntry& entry) {
    QByteArray bytes;
    if (!entryBytes(entry, bytes)) return 0;
    if (entry.type == 1 || entry.type == 6 || entry.type == 7) {
      return bytes.isEmpty() ? 0 : quint8(bytes.at(0));
    }
    if ((entry.type == 3 || entry.type == 8) && bytes.size() >= 2) {
      return read16(reinterpret_cast<const uint8_t*>(bytes.constData()));
    }
    if ((entry.type == 4 || entry.type == 9) && bytes.size() >= 4) {
      return read32(reinterpret_cast<const uint8_t*>(bytes.constData()));
    }
    return 0;
  }

  double rationalValue(const TiffEntry& entry) {
    QByteArray bytes;
    if (!entryBytes(entry, bytes)) return 0.0;
    if ((entry.type == 5 || entry.type == 10) && bytes.size() >= 8) {
      const uint8_t* data = reinterpret_cast<const uint8_t*>(bytes.constData());
      if (entry.type == 10) {
        const qint32 numerator = qint32(read32(data));
        const qint32 denominator = qint32(read32(data + 4));
        return denominator != 0 ? double(numerator) / double(denominator) : 0.0;
      }
      const quint32 numerator = read32(data);
      const quint32 denominator = read32(data + 4);
      return denominator != 0 ? double(numerator) / double(denominator) : 0.0;
    }
    return double(unsignedValue(entry));
  }

  quint16 read16(const uint8_t* data) const {
    if (m_littleEndian) {
      return quint16(data[0]) | (quint16(data[1]) << 8);
    }
    return (quint16(data[0]) << 8) | quint16(data[1]);
  }

  quint32 read32(const uint8_t* data) const {
    if (m_littleEndian) {
      return quint32(data[0]) | (quint32(data[1]) << 8) |
             (quint32(data[2]) << 16) | (quint32(data[3]) << 24);
    }
    return (quint32(data[0]) << 24) | (quint32(data[1]) << 16) |
           (quint32(data[2]) << 8) | quint32(data[3]);
  }

 private:
  ReadFn m_read;
  qint64 m_totalSize = 0;
  qint64 m_base = 0;
  qint64 m_ifd0Offset = 0;
  bool m_littleEndian = true;
  bool m_valid = false;
};

struct ParsedMetadata {
  QVariantMap map;
  int orientation = 1;
  QByteArray icc;
  int width = 0;
  int height = 0;
  bool valid = false;
};

QString formatExposureTime(double shutter) {
  if (shutter <= 0.0) return "-";
  if (shutter < 1.0) return QString("1/%1 s").arg(qRound(1.0 / shutter));
  return QString("%1 s").arg(shutter, 0, 'f', 1);
}

QString formatAperture(double aperture) {
  if (aperture <= 0.0) return "-";
  return QString("f/%1").arg(aperture, 0, 'f', 1);
}

QString formatFocalLength(double focal) {
  if (focal <= 0.0) return "-";
  return QString("%1mm").arg(focal, 0, 'f', 1);
}

QString formatTimestamp(const QString& raw) {
  if (raw.isEmpty()) return "-";
  QDateTime dateTime = QDateTime::fromString(raw, "yyyy:MM:dd HH:mm:ss");
  if (!dateTime.isValid()) {
    dateTime = QDateTime::fromString(raw, "yyyy-MM-dd HH:mm:ss");
  }
  if (!dateTime.isValid()) return raw;
  return dateTime.toString("yyyy-MM-dd HH:mm:ss");
}

QVariantMap defaultMetadataMap() {
  QVariantMap map;
  map["make"] = QString();
  map["model"] = QString();
  map["iso"] = 0;
  map["exposureTime"] = QString("-");
  map["aperture"] = QString("-");
  map["focalLength"] = QString("-");
  map["lensModel"] = QString("Unknown Lens");
  map["artist"] = QString("-");
  map["timestamp"] = QString("-");
  return map;
}

ParsedMetadata parseMetadataFromReader(TiffReader& reader) {
  ParsedMetadata parsed;
  if (!reader.isValid()) return parsed;

  QString make;
  QString model;
  QString artist;
  QString lens;
  QString dateTime;
  double exposureTime = 0.0;
  double aperture = 0.0;
  double focalLength = 0.0;
  int iso = 0;

  qint64 exifIfdOffset = -1;
  QByteArray icc;

  reader.forEachEntry(reader.ifd0Offset(), [&](const TiffEntry& entry) {
    switch (entry.tag) {
      case kExifTagImageWidth:
        parsed.width = int(reader.unsignedValue(entry));
        break;
      case kExifTagImageHeight:
        parsed.height = int(reader.unsignedValue(entry));
        break;
      case kExifTagMake:
        make = reader.asciiValue(entry);
        break;
      case kExifTagModel:
        model = reader.asciiValue(entry);
        break;
      case kExifTagOrientation:
        parsed.orientation = int(reader.unsignedValue(entry));
        break;
      case kExifTagArtist:
        artist = reader.asciiValue(entry);
        break;
      case kExifTagDateTime:
        dateTime = reader.asciiValue(entry);
        break;
      case kExifTagExifIfd:
        exifIfdOffset = qint64(reader.unsignedValue(entry));
        break;
      case kExifTagIccProfile:
        reader.entryBytes(entry, icc);
        break;
      default:
        break;
    }
    return true;
  });

  if (parsed.orientation < 1 || parsed.orientation > 8) parsed.orientation = 1;

  if (exifIfdOffset > 0) {
    reader.forEachEntry(exifIfdOffset, [&](const TiffEntry& entry) {
      switch (entry.tag) {
        case kExifTagExposureTime:
          exposureTime = reader.rationalValue(entry);
          break;
        case kExifTagFNumber:
          aperture = reader.rationalValue(entry);
          break;
        case kExifTagIso:
          iso = int(reader.unsignedValue(entry));
          break;
        case kExifTagDateTimeOriginal:
          dateTime = reader.asciiValue(entry);
          break;
        case kExifTagFocalLength:
          focalLength = reader.rationalValue(entry);
          break;
        case kExifTagLensModel:
          lens = reader.asciiValue(entry);
          break;
        default:
          break;
      }
      return true;
    });
  }

  QVariantMap map;
  map["make"] = make;
  map["model"] = model;
  map["iso"] = iso;
  map["exposureTime"] = formatExposureTime(exposureTime);
  map["aperture"] = formatAperture(aperture);
  map["focalLength"] = formatFocalLength(focalLength);
  map["lensModel"] = lens.isEmpty() ? QString("Unknown Lens") : lens;
  map["artist"] = artist.isEmpty() ? QString("-") : artist;
  map["timestamp"] = formatTimestamp(dateTime);
  if (parsed.width > 0) map["width"] = parsed.width;
  if (parsed.height > 0) map["height"] = parsed.height;

  parsed.map = map;
  parsed.icc = icc;
  parsed.valid = true;
  return parsed;
}

// Extracts the EXIF TIFF block and the ICC profile from a JPEG marker stream.
struct JpegExtras {
  QByteArray exifTiffBlob;
  QByteArray icc;
};

JpegExtras scanJpegMarkers(const QString& path) {
  JpegExtras extras;
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return extras;

  const QByteArray soi = file.read(2);
  if (soi.size() != 2 || quint8(soi[0]) != 0xFF || quint8(soi[1]) != 0xD8) {
    return extras;
  }

  constexpr int kMaxIccChunks = 32;
  QByteArray iccChunks[kMaxIccChunks];
  int iccChunkCount = -1;
  int iccChunksSeen = 0;

  while (!file.atEnd()) {
    int prefix = 0;
    while (true) {
      const QByteArray prefixByte = file.read(1);
      if (prefixByte.isEmpty()) return extras;
      prefix = quint8(prefixByte[0]);
      if (prefix == 0xFF) break;
    }

    int marker = 0;
    while (true) {
      const QByteArray markerByte = file.read(1);
      if (markerByte.isEmpty()) return extras;
      marker = quint8(markerByte[0]);
      if (marker != 0xFF) break;
    }

    // Standalone markers without a payload.
    if (marker == 0xD8 || (marker >= 0xD0 && marker <= 0xD7) ||
        marker == 0x01) {
      continue;
    }
    if (marker == 0xD9 || marker == 0xDA) break;  // EOI or SOS

    const QByteArray lengthBytes = file.read(2);
    if (lengthBytes.size() != 2) break;
    const int segmentLength =
        (quint8(lengthBytes[0]) << 8) | quint8(lengthBytes[1]);
    if (segmentLength < 2) break;
    const int payloadLength = segmentLength - 2;

    if (marker == 0xE1 && extras.exifTiffBlob.isEmpty()) {
      const QByteArray payload = file.read(payloadLength);
      if (payload.size() == payloadLength &&
          payload.startsWith(QByteArray("Exif\0\0", 6))) {
        extras.exifTiffBlob = payload.mid(6);
      }
    } else if (marker == 0xE2) {
      const QByteArray payload = file.read(payloadLength);
      if (payload.size() == payloadLength &&
          payload.startsWith(QByteArray("ICC_PROFILE\0", 12)) &&
          payload.size() > 14) {
        const int sequence = quint8(payload[12]);
        const int total = quint8(payload[13]);
        if (total > 0 && total <= kMaxIccChunks && sequence >= 1 &&
            sequence <= total) {
          if (iccChunkCount < 0) {
            iccChunkCount = total;
          } else if (iccChunkCount != total) {
            iccChunkCount = -2;
          }
          if (iccChunks[sequence - 1].isEmpty()) ++iccChunksSeen;
          iccChunks[sequence - 1] = payload.mid(14);
        }
      }
    } else {
      file.seek(file.pos() + payloadLength);
    }
  }

  if (iccChunkCount > 0 && iccChunksSeen == iccChunkCount) {
    for (int i = 0; i < iccChunkCount; ++i) extras.icc.append(iccChunks[i]);
  }
  return extras;
}

ParsedMetadata parseJpegMetadata(const QString& path) {
  const JpegExtras extras = scanJpegMarkers(path);
  ParsedMetadata parsed;
  if (!extras.exifTiffBlob.isEmpty()) {
    const QByteArray* blob = &extras.exifTiffBlob;
    TiffReader reader(
        [blob](qint64 pos, void* dst, qint64 len) {
          if (pos < 0 || len < 0 || pos + len > blob->size()) return false;
          std::memcpy(dst, blob->constData() + pos, size_t(len));
          return true;
        },
        extras.exifTiffBlob.size());
    if (reader.init(0)) parsed = parseMetadataFromReader(reader);
  }
  if (!extras.icc.isEmpty()) parsed.icc = extras.icc;
  return parsed;
}

ParsedMetadata parseTiffMetadata(const QString& path) {
  ParsedMetadata parsed;
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return parsed;
  TiffReader reader(
      [&file](qint64 pos, void* dst, qint64 len) {
        if (pos < 0 || len < 0 || !file.seek(pos)) return false;
        return file.read(static_cast<char*>(dst), len) == len;
      },
      file.size());
  if (!reader.init(0)) return parsed;
  return parseMetadataFromReader(reader);
}

// ---------------------------------------------------------------------------
// Pixel helpers
// ---------------------------------------------------------------------------

void applyOrientationForTag(BitmapImage& image, int rawOrientation) {
  if (rawOrientation <= 1 || rawOrientation > 8) return;
  const int width = image.width;
  const int height = image.height;
  if (width <= 0 || height <= 0) return;

  const bool swapDimensions = rawOrientation >= 5;
  const int destinationWidth = swapDimensions ? height : width;
  const int destinationHeight = swapDimensions ? width : height;

  std::vector<uint16_t> destination(size_t(destinationWidth) *
                                    size_t(destinationHeight) * 3);
  for (int y = 0; y < height; ++y) {
    const uint16_t* sourceRow =
        image.pixels.data() + size_t(y) * size_t(width) * 3;
    for (int x = 0; x < width; ++x) {
      int dx = x;
      int dy = y;
      switch (rawOrientation) {
        case 2:
          dx = width - 1 - x;
          break;
        case 3:
          dx = width - 1 - x;
          dy = height - 1 - y;
          break;
        case 4:
          dy = height - 1 - y;
          break;
        case 5:
          dx = y;
          dy = x;
          break;
        case 6:
          dx = height - 1 - y;
          dy = x;
          break;
        case 7:
          dx = height - 1 - y;
          dy = width - 1 - x;
          break;
        case 8:
          dx = y;
          dy = width - 1 - x;
          break;
        default:
          break;
      }
      uint16_t* destinationPixel =
          destination.data() +
          (size_t(dy) * size_t(destinationWidth) + size_t(dx)) * 3;
      const uint16_t* sourcePixel = sourceRow + size_t(x) * 3;
      destinationPixel[0] = sourcePixel[0];
      destinationPixel[1] = sourcePixel[1];
      destinationPixel[2] = sourcePixel[2];
    }
  }

  image.pixels = std::move(destination);
  image.width = destinationWidth;
  image.height = destinationHeight;
}

void applyIccProfile(BitmapImage& image, const QByteArray& icc) {
  if (icc.isEmpty() || image.pixels.empty() || image.width <= 0 ||
      image.height <= 0) {
    return;
  }

  cmsHPROFILE inputProfile =
      cmsOpenProfileFromMem(icc.constData(), cmsUInt32Number(icc.size()));
  if (!inputProfile) {
    logDebug("Ignoring unreadable ICC profile");
    return;
  }
  if (cmsGetColorSpace(inputProfile) != cmsSigRgbData) {
    logDebug("Ignoring non-RGB ICC profile");
    cmsCloseProfile(inputProfile);
    return;
  }

  cmsHPROFILE srgbProfile = cmsCreate_sRGBProfile();
  if (!srgbProfile) {
    cmsCloseProfile(inputProfile);
    return;
  }

  cmsHTRANSFORM transform =
      cmsCreateTransform(inputProfile, TYPE_RGB_16, srgbProfile, TYPE_RGB_16,
                         INTENT_PERCEPTUAL, 0);
  if (transform) {
    cmsDoTransform(transform, image.pixels.data(), image.pixels.data(),
                   cmsUInt32Number(size_t(image.width) * size_t(image.height)));
    cmsDeleteTransform(transform);
  }

  cmsCloseProfile(srgbProfile);
  cmsCloseProfile(inputProfile);
}

QImage toQImage(const BitmapImage& image) {
  if (image.pixels.empty() || image.width <= 0 || image.height <= 0) {
    return QImage();
  }
  QImage result(image.width, image.height, QImage::Format_RGBX64);
  QRgba64* destination = reinterpret_cast<QRgba64*>(result.bits());
  const uint16_t* source = image.pixels.data();
  for (size_t i = 0; i < size_t(image.width) * size_t(image.height); ++i) {
    destination[i] = QRgba64::fromRgba64(source[i * 3 + 0], source[i * 3 + 1],
                                         source[i * 3 + 2], 65535);
  }
  return result;
}

void downscaleRgb16(BitmapImage& image, int maxEdge) {
  if (maxEdge <= 0 || image.pixels.empty() || image.width <= 0 ||
      image.height <= 0) {
    return;
  }
  if (std::max(image.width, image.height) <= maxEdge) return;

  const QImage source = toQImage(image);
  const QImage scaled = source.scaled(maxEdge, maxEdge, Qt::KeepAspectRatio,
                                      Qt::SmoothTransformation);
  if (scaled.isNull()) return;

  BitmapImage result;
  result.width = scaled.width();
  result.height = scaled.height();
  result.orientation = image.orientation;
  result.metadata = image.metadata;
  result.pixels.resize(size_t(result.width) * size_t(result.height) * 3);
  const QRgba64* sourcePixels =
      reinterpret_cast<const QRgba64*>(scaled.constBits());
  for (size_t i = 0; i < size_t(result.width) * size_t(result.height); ++i) {
    result.pixels[i * 3 + 0] = sourcePixels[i].red();
    result.pixels[i * 3 + 1] = sourcePixels[i].green();
    result.pixels[i * 3 + 2] = sourcePixels[i].blue();
  }
  image = std::move(result);
}

// ---------------------------------------------------------------------------
// JPEG decoding
// ---------------------------------------------------------------------------

struct JpegErrorManager {
  jpeg_error_mgr pub;
  jmp_buf jump;
};

void jpegErrorExit(j_common_ptr cinfo) {
  auto* errorManager = reinterpret_cast<JpegErrorManager*>(cinfo->err);
  longjmp(errorManager->jump, 1);
}

void jpegOutputMessage(j_common_ptr cinfo) {
  char buffer[JMSG_LENGTH_MAX] = {};
  (*cinfo->err->format_message)(cinfo, buffer);
  logDebug(QString("libjpeg: %1").arg(QString::fromLatin1(buffer)));
}

BitmapImage decodeJpegPixels(const QString& path, int maxEdge) {
  BitmapImage image;
  const QByteArray nativePath = QFile::encodeName(path);
  FILE* file = std::fopen(nativePath.constData(), "rb");
  if (!file) {
    logError(QString("Cannot open JPEG file: %1").arg(path));
    return image;
  }

  jpeg_decompress_struct cinfo;
  JpegErrorManager errorManager;
  std::memset(&cinfo, 0, sizeof(cinfo));
  cinfo.err = jpeg_std_error(&errorManager.pub);
  errorManager.pub.error_exit = jpegErrorExit;
  errorManager.pub.output_message = jpegOutputMessage;

  if (setjmp(errorManager.jump)) {
    logError(QString("Failed to decode JPEG: %1").arg(path));
    jpeg_destroy_decompress(&cinfo);
    std::fclose(file);
    return BitmapImage();
  }

  jpeg_create_decompress(&cinfo);
  jpeg_stdio_src(&cinfo, file);
  jpeg_read_header(&cinfo, TRUE);

  if (maxEdge > 0) {
    const int minDimension =
        std::min(int(cinfo.image_width), int(cinfo.image_height));
    int denominator = 1;
    while (denominator < 8 && minDimension / (denominator * 2) >= maxEdge) {
      denominator *= 2;
    }
    if (denominator > 1) {
      cinfo.scale_num = 1;
      cinfo.scale_denom = denominator;
    }
  }

  if (cinfo.jpeg_color_space == JCS_CMYK ||
      cinfo.jpeg_color_space == JCS_YCCK) {
    cinfo.out_color_space = JCS_CMYK;
  } else {
    cinfo.out_color_space = JCS_RGB;
  }

  jpeg_start_decompress(&cinfo);

  const int width = int(cinfo.output_width);
  const int height = int(cinfo.output_height);
  const int components = int(cinfo.output_components);
  if (width <= 0 || height <= 0 || (components != 3 && components != 4)) {
    logError(QString("Unsupported JPEG layout in %1").arg(path));
    jpeg_destroy_decompress(&cinfo);
    std::fclose(file);
    return BitmapImage();
  }

  const int rowStride = width * components;
  std::vector<uint8_t> rowBuffer(static_cast<size_t>(rowStride));
  image.pixels.resize(size_t(width) * size_t(height) * 3);
  image.width = width;
  image.height = height;

  while (cinfo.output_scanline < cinfo.output_height) {
    JSAMPROW rowPointer = rowBuffer.data();
    jpeg_read_scanlines(&cinfo, &rowPointer, 1);
    const int y = int(cinfo.output_scanline) - 1;
    uint16_t* destination = image.pixels.data() + size_t(y) * size_t(width) * 3;

    if (components == 3) {
      for (int x = 0; x < width; ++x) {
        const uint8_t* pixel = rowBuffer.data() + size_t(x) * 3;
        destination[size_t(x) * 3 + 0] = uint16_t(pixel[0]) * 257;
        destination[size_t(x) * 3 + 1] = uint16_t(pixel[1]) * 257;
        destination[size_t(x) * 3 + 2] = uint16_t(pixel[2]) * 257;
      }
    } else {
      for (int x = 0; x < width; ++x) {
        const uint8_t* pixel = rowBuffer.data() + size_t(x) * 4;
        const int c = pixel[0];
        const int m = pixel[1];
        const int yellow = pixel[2];
        const int k = pixel[3];
        destination[size_t(x) * 3 + 0] =
            uint16_t((255 - c) * (255 - k) / 255) * 257;
        destination[size_t(x) * 3 + 1] =
            uint16_t((255 - m) * (255 - k) / 255) * 257;
        destination[size_t(x) * 3 + 2] =
            uint16_t((255 - yellow) * (255 - k) / 255) * 257;
      }
    }
  }

  jpeg_finish_decompress(&cinfo);
  jpeg_destroy_decompress(&cinfo);
  std::fclose(file);
  return image;
}

BitmapImage decodeJpeg(const QString& path, int maxEdge) {
  CrashReporter::setBreadcrumb(QString("jpeg pixels %1").arg(path));
  BitmapImage image = decodeJpegPixels(path, maxEdge);
  if (image.pixels.empty()) return image;

  downscaleRgb16(image, maxEdge);

  CrashReporter::setBreadcrumb(QString("jpeg metadata %1").arg(path));
  const ParsedMetadata parsed = parseJpegMetadata(path);
  image.orientation = mapOrientation(parsed.orientation);
  applyIccProfile(image, parsed.icc);
  applyOrientationForTag(image, parsed.orientation);

  QVariantMap metadata =
      parsed.map.isEmpty() ? defaultMetadataMap() : parsed.map;
  metadata["width"] = image.width;
  metadata["height"] = image.height;
  image.metadata = metadata;
  return image;
}

// ---------------------------------------------------------------------------
// TIFF decoding
// ---------------------------------------------------------------------------

BitmapImage decodeTiffRgbaFallback(TIFF* tif, uint32_t width, uint32_t height) {
  BitmapImage image;
  std::vector<uint32_t> raster(size_t(width) * size_t(height));
  if (!TIFFReadRGBAImageOriented(tif, width, height, raster.data(),
                                 ORIENTATION_TOPLEFT, 0)) {
    logError("TIFFReadRGBAImage failed");
    return image;
  }

  image.width = int(width);
  image.height = int(height);
  image.pixels.resize(size_t(width) * size_t(height) * 3);
  for (size_t i = 0; i < raster.size(); ++i) {
    const uint32_t pixel = raster[i];
    image.pixels[i * 3 + 0] = uint16_t(TIFFGetR(pixel)) * 257;
    image.pixels[i * 3 + 1] = uint16_t(TIFFGetG(pixel)) * 257;
    image.pixels[i * 3 + 2] = uint16_t(TIFFGetB(pixel)) * 257;
  }
  return image;
}

BitmapImage decodeTiff(const QString& path) {
  CrashReporter::setBreadcrumb(QString("tiff pixels %1").arg(path));
  BitmapImage image;
  const QByteArray nativePath = QFile::encodeName(path);
  TIFF* tif = TIFFOpen(nativePath.constData(), "r");
  if (!tif) {
    logError(QString("Cannot open TIFF file: %1").arg(path));
    return image;
  }

  uint32_t width = 0;
  uint32_t height = 0;
  TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width);
  TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height);
  if (width == 0 || height == 0) {
    logError(QString("Invalid TIFF dimensions in %1").arg(path));
    TIFFClose(tif);
    return image;
  }

  uint16_t bitsPerSample = 8;
  uint16_t samplesPerPixel = 1;
  uint16_t photometric = PHOTOMETRIC_MINISBLACK;
  uint16_t planarConfig = PLANARCONFIG_CONTIG;
  uint16_t sampleFormat = SAMPLEFORMAT_UINT;
  uint16_t rawOrientation = ORIENTATION_TOPLEFT;
  TIFFGetFieldDefaulted(tif, TIFFTAG_BITSPERSAMPLE, &bitsPerSample);
  TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLESPERPIXEL, &samplesPerPixel);
  TIFFGetFieldDefaulted(tif, TIFFTAG_PHOTOMETRIC, &photometric);
  TIFFGetFieldDefaulted(tif, TIFFTAG_PLANARCONFIG, &planarConfig);
  TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLEFORMAT, &sampleFormat);
  TIFFGetFieldDefaulted(tif, TIFFTAG_ORIENTATION, &rawOrientation);

  const bool isGray = photometric == PHOTOMETRIC_MINISBLACK ||
                      photometric == PHOTOMETRIC_MINISWHITE;
  const bool isRgb = photometric == PHOTOMETRIC_RGB;
  const bool supportedDepth = (bitsPerSample == 8 || bitsPerSample == 16) &&
                              sampleFormat == SAMPLEFORMAT_UINT;
  const bool supportedLayout =
      !TIFFIsTiled(tif) && supportedDepth && (isGray || isRgb) &&
      (isGray ? samplesPerPixel >= 1 : samplesPerPixel >= 3);

  if (!supportedLayout) {
    logDebug(QString("TIFF fallback decoder used for %1 (bps=%2 spp=%3 "
                     "photo=%4 tiled=%5)")
                 .arg(path)
                 .arg(bitsPerSample)
                 .arg(samplesPerPixel)
                 .arg(photometric)
                 .arg(TIFFIsTiled(tif)));
    BitmapImage fallback = decodeTiffRgbaFallback(tif, width, height);
    TIFFClose(tif);
    if (fallback.pixels.empty()) return fallback;

    const ParsedMetadata parsed = parseTiffMetadata(path);
    fallback.orientation = mapOrientation(parsed.orientation);
    applyIccProfile(fallback, parsed.icc);

    QVariantMap metadata =
        parsed.map.isEmpty() ? defaultMetadataMap() : parsed.map;
    metadata["width"] = fallback.width;
    metadata["height"] = fallback.height;
    fallback.metadata = metadata;
    return fallback;
  }

  const int bytesPerSample = bitsPerSample / 8;
  const int channels = isRgb ? 3 : 1;
  image.width = int(width);
  image.height = int(height);
  image.pixels.resize(size_t(width) * size_t(height) * 3);

  const tsize_t scanlineSize = TIFFScanlineSize(tif);
  std::vector<uint8_t> scanline(size_t(std::max<tsize_t>(scanlineSize, 1)));

  auto storePixel = [&](int x, int y, uint16_t r, uint16_t g, uint16_t b) {
    uint16_t* destination =
        image.pixels.data() + (size_t(y) * size_t(width) + size_t(x)) * 3;
    destination[0] = r;
    destination[1] = g;
    destination[2] = b;
  };

  auto scaleSample = [&](const uint8_t* sample) -> uint16_t {
    if (bitsPerSample == 16) {
      uint16_t value = 0;
      std::memcpy(&value, sample, sizeof(value));
      return value;
    }
    return uint16_t(*sample) * 257;
  };

  if (planarConfig == PLANARCONFIG_CONTIG) {
    for (uint32_t y = 0; y < height; ++y) {
      if (TIFFReadScanline(tif, scanline.data(), y) < 0) {
        logError(QString("Failed to read TIFF scanline in %1").arg(path));
        TIFFClose(tif);
        return BitmapImage();
      }
      for (uint32_t x = 0; x < width; ++x) {
        const uint8_t* pixel =
            scanline.data() + size_t(x) * samplesPerPixel * bytesPerSample;
        if (isRgb) {
          storePixel(int(x), int(y), scaleSample(pixel),
                     scaleSample(pixel + bytesPerSample),
                     scaleSample(pixel + size_t(2) * bytesPerSample));
        } else {
          uint16_t value = scaleSample(pixel);
          if (photometric == PHOTOMETRIC_MINISWHITE) {
            value = uint16_t(65535 - value);
          }
          storePixel(int(x), int(y), value, value, value);
        }
      }
    }
  } else {
    // PLANARCONFIG_SEPARATE
    std::vector<uint16_t> planeRows(size_t(width) * size_t(channels));
    for (uint32_t y = 0; y < height; ++y) {
      for (int channel = 0; channel < channels; ++channel) {
        if (TIFFReadScanline(tif, scanline.data(), y, uint16_t(channel)) < 0) {
          logError(QString("Failed to read TIFF plane in %1").arg(path));
          TIFFClose(tif);
          return BitmapImage();
        }
        for (uint32_t x = 0; x < width; ++x) {
          planeRows[size_t(x) * channels + channel] =
              scaleSample(scanline.data() + size_t(x) * bytesPerSample);
        }
      }
      for (uint32_t x = 0; x < width; ++x) {
        if (isRgb) {
          storePixel(int(x), int(y), planeRows[size_t(x) * channels + 0],
                     planeRows[size_t(x) * channels + 1],
                     planeRows[size_t(x) * channels + 2]);
        } else {
          uint16_t value = planeRows[size_t(x)];
          if (photometric == PHOTOMETRIC_MINISWHITE) {
            value = uint16_t(65535 - value);
          }
          storePixel(int(x), int(y), value, value, value);
        }
      }
    }
  }

  TIFFClose(tif);

  CrashReporter::setBreadcrumb(QString("tiff metadata %1").arg(path));
  const ParsedMetadata parsed = parseTiffMetadata(path);
  image.orientation = mapOrientation(parsed.orientation);
  applyIccProfile(image, parsed.icc);
  applyOrientationForTag(image, parsed.orientation);

  QVariantMap metadata =
      parsed.map.isEmpty() ? defaultMetadataMap() : parsed.map;
  metadata["width"] = image.width;
  metadata["height"] = image.height;
  image.metadata = metadata;
  return image;
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

const QStringList& ImageDecoder::rawExtensions() {
  static const QStringList extensions = {"arw", "cr2", "cr3", "nef", "dng",
                                         "orf", "raf", "rw2", "pef", "srw",
                                         "x3f", "iiq", "nrw", "kdc", "dcr"};
  return extensions;
}

const QStringList& ImageDecoder::bitmapExtensions() {
  static const QStringList extensions = {"jpg", "jpeg", "tif", "tiff"};
  return extensions;
}

bool ImageDecoder::isRaw(const QString& path) {
  return rawExtensions().contains(QFileInfo(path).suffix().toLower());
}

bool ImageDecoder::isBitmap(const QString& path) {
  return bitmapExtensions().contains(QFileInfo(path).suffix().toLower());
}

BitmapImage ImageDecoder::decode(const QString& path) {
  CrashReporter::setBreadcrumb(QString("decode %1").arg(path));
  const QString extension = QFileInfo(path).suffix().toLower();
  if (extension == "jpg" || extension == "jpeg") {
    return decodeJpeg(path, 0);
  }
  if (extension == "tif" || extension == "tiff") {
    return decodeTiff(path);
  }
  logError(QString("Unsupported image file: %1").arg(path));
  return BitmapImage();
}

BitmapImage ImageDecoder::decodeScaled(const QString& path, int maxEdge) {
  CrashReporter::setBreadcrumb(QString("decode scaled %1").arg(path));
  const QString extension = QFileInfo(path).suffix().toLower();
  if (extension == "jpg" || extension == "jpeg") {
    return decodeJpeg(path, maxEdge);
  }
  if (extension == "tif" || extension == "tiff") {
    BitmapImage image = decodeTiff(path);
    downscaleRgb16(image, maxEdge);
    return image;
  }
  logError(QString("Unsupported image file: %1").arg(path));
  return BitmapImage();
}

QImage ImageDecoder::extractThumbnail(const QString& path, int maxEdge) {
  const BitmapImage image = decodeScaled(path, maxEdge);
  return toQImage(image);
}

QVariantMap ImageDecoder::readMetadata(const QString& path) {
  const QString extension = QFileInfo(path).suffix().toLower();
  QVariantMap map;
  if (extension == "jpg" || extension == "jpeg") {
    map = parseJpegMetadata(path).map;
  } else if (extension == "tif" || extension == "tiff") {
    map = parseTiffMetadata(path).map;
  }
  return map.isEmpty() ? defaultMetadataMap() : map;
}

}  // namespace photon
