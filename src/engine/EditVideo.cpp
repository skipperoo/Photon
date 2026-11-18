#include "EditVideo.h"

#include <QFontDatabase>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRegion>
#include <algorithm>
#include <cmath>
#include <mutex>

namespace photon::editvideo {

// --- Edit groups --------------------------------------------------------------

const std::vector<EditGroup>& editGroups() {
  static const std::vector<EditGroup> groups = {
      {QStringLiteral("Profile"),
       {QStringLiteral("profile"), QStringLiteral("tonemappingEnabled")},
       {}},
      {QStringLiteral("Camera Look"),
       {QStringLiteral("cameraLookAmount"), QStringLiteral("cameraLook"),
        QStringLiteral("cameraLookAgx")},
       {}},
      // White balance lives in Light; the exporter's Kelvin gains go with it.
      {QStringLiteral("Light"),
       {QStringLiteral("temperature"), QStringLiteral("temperatureKelvin"),
        QStringLiteral("tint"), QStringLiteral("wbGainR"),
        QStringLiteral("wbGainG"), QStringLiteral("wbGainB"),
        QStringLiteral("exposure"), QStringLiteral("contrast"),
        QStringLiteral("highlights"), QStringLiteral("shadows"),
        QStringLiteral("whites"), QStringLiteral("blacks"),
        QStringLiteral("adaptation")},
       {}},
      {QStringLiteral("Presence"),
       {QStringLiteral("vibrance"), QStringLiteral("saturation")},
       {}},
      {QStringLiteral("Tone Curve"), {}, {QStringLiteral("toneCurve")}},
      {QStringLiteral("HSL"), {}, {QStringLiteral("hsl")}},
      {QStringLiteral("Black & White Mix"), {}, {QStringLiteral("bwMix")}},
      {QStringLiteral("Color Grading"), {}, {QStringLiteral("cg")}},
      {QStringLiteral("Detail"),
       {QStringLiteral("sharpness"), QStringLiteral("sharpenMask"),
        QStringLiteral("maskFeather"), QStringLiteral("focusDetect"),
        QStringLiteral("denoiseEnabled"), QStringLiteral("denoiseAmount"),
        QStringLiteral("denoiseColor"), QStringLiteral("denoiseChromaAmount"),
        QStringLiteral("denoiseChromaBm3d"),
        QStringLiteral("denoiseChromaRadius"),
        QStringLiteral("denoiseGroupSize"),
        QStringLiteral("denoiseSearchWindow")},
       {}},
      {QStringLiteral("Creative"),
       {},
       {QStringLiteral("grain"), QStringLiteral("vignette")}},
      {QStringLiteral("Effects"),
       {QStringLiteral("clarity"), QStringLiteral("dehaze"),
        QStringLiteral("structure"), QStringLiteral("centre")},
       {}},
      {QStringLiteral("Calibration"), {}, {QStringLiteral("calib")}},
  };
  return groups;
}

int groupOfKey(const QString& key) {
  const auto& groups = editGroups();
  for (size_t g = 0; g < groups.size(); ++g) {
    if (groups[g].keys.contains(key)) return int(g);
    for (const QString& prefix : groups[g].keyPrefixes) {
      if (key.startsWith(prefix)) return int(g);
    }
  }
  return -1;
}

const QStringList& baselineKeys() {
  static const QStringList keys = {
      // Geometry: applied first, so every frame shares the final framing.
      QStringLiteral("cropRect"), QStringLiteral("cropAspectRatio"),
      QStringLiteral("orientationSteps"), QStringLiteral("straightenAngle"),
      QStringLiteral("flipHorizontal"), QStringLiteral("flipVertical"),
      // Lens corrections: optical fixes, applied with the geometry.
      QStringLiteral("lensProfileEnabled"), QStringLiteral("lensProfileMaker"),
      QStringLiteral("lensProfileLens"), QStringLiteral("lensProfileDistortion"),
      QStringLiteral("lensProfileVignetting"), QStringLiteral("lensProfileCA"),
      // Metadata and render keys injected by the exporter.
      QStringLiteral("processVersion"), QStringLiteral("exifMake"),
      QStringLiteral("exifModel"), QStringLiteral("exifLens"),
      QStringLiteral("exifFocal"), QStringLiteral("exifAperture"),
      QStringLiteral("baseExposure"), QStringLiteral("sceneWhite"),
      QStringLiteral("outputBits"), QStringLiteral("outputColorSpace"),
      QStringLiteral("denoiseSecondPass"), QStringLiteral("denoiseMode")};
  return keys;
}

GroupMask allGroups() {
  return (GroupMask(1) << editGroups().size()) - 1;
}

QJsonObject stepSettings(const QJsonObject& full, GroupMask groups) {
  QJsonObject out;
  for (auto it = full.begin(); it != full.end(); ++it) {
    const int g = groupOfKey(it.key());
    if (g < 0 || (groups & groupBit(g))) out.insert(it.key(), it.value());
  }
  return out;
}

bool editHasGroup(const QJsonObject& full, int group) {
  for (auto it = full.begin(); it != full.end(); ++it) {
    if (groupOfKey(it.key()) == group) return true;
  }
  return false;
}

std::vector<StepPlan> planSteps(const std::vector<int>& shownGroups, bool tweaks) {
  std::vector<int> shown = shownGroups;
  std::sort(shown.begin(), shown.end());
  shown.erase(std::unique(shown.begin(), shown.end()), shown.end());
  std::vector<StepPlan> plan = {{QString(), 0}};
  GroupMask mask = 0;
  for (const int g : shown) {
    if (g < 0 || g >= int(editGroups().size())) continue;
    mask |= groupBit(g);
    plan.push_back({editGroups()[size_t(g)].name, mask});
  }
  if (tweaks) plan.push_back({QString::fromLatin1(tweaksName()), allGroups()});
  return plan;
}

// --- Output canvas ------------------------------------------------------------

Aspect aspectFromString(const QString& s) {
  if (s == QLatin1String("4:5")) return Aspect::Portrait4x5;
  if (s == QLatin1String("5:4")) return Aspect::Landscape5x4;
  if (s == QLatin1String("16:9")) return Aspect::Landscape16x9;
  if (s == QLatin1String("9:16")) return Aspect::Portrait9x16;
  return Aspect::Original;
}

QSize canvasSize(Aspect aspect, QSize photo, int shortSide) {
  auto even = [](double v) { return std::max(2, int(std::lround(v / 2.0)) * 2); };
  const double s = shortSide;
  switch (aspect) {
    case Aspect::Portrait4x5: return {even(s), even(s * 5.0 / 4.0)};
    case Aspect::Landscape5x4: return {even(s * 5.0 / 4.0), even(s)};
    case Aspect::Landscape16x9: return {even(s * 16.0 / 9.0), even(s)};
    case Aspect::Portrait9x16: return {even(s), even(s * 16.0 / 9.0)};
    case Aspect::Original:
    default: {
      if (photo.width() <= 0 || photo.height() <= 0) return {even(s), even(s)};
      const double ratio = double(photo.width()) / photo.height();
      return ratio >= 1.0 ? QSize(even(s * ratio), even(s))
                          : QSize(even(s), even(s / ratio));
    }
  }
}

// --- Pill options -----------------------------------------------------------------

PillStyle pillStyleFromString(const QString& s) {
  if (s == QLatin1String("liquid")) return PillStyle::LiquidGlass;
  if (s == QLatin1String("white")) return PillStyle::White;
  if (s == QLatin1String("black")) return PillStyle::Black;
  return PillStyle::FrostedGlass;
}

PillShape pillShapeFromString(const QString& s) {
  return s == QLatin1String("capsule") ? PillShape::Capsule : PillShape::RoundedRect;
}

PillPosition pillPositionFromString(const QString& s) {
  if (s == QLatin1String("top")) return PillPosition::Top;
  if (s == QLatin1String("bottom")) return PillPosition::Bottom;
  return PillPosition::Center;
}

// --- Helpers -------------------------------------------------------------------

namespace {

double clamp01(double t) { return std::clamp(t, 0.0, 1.0); }
// Ease in-out (cubic): calm start and end, like the platform animations.
double ease(double t) {
  t = clamp01(t);
  return t < 0.5 ? 4.0 * t * t * t : 1.0 - std::pow(-2.0 * t + 2.0, 3.0) / 2.0;
}

QString pillFontFamily() {
  static QString family;
  static std::once_flag once;
  std::call_once(once, [] {
    const int id = QFontDatabase::addApplicationFont(
        QStringLiteral(":/Main/assets/fonts/InstrumentSans-SemiBold.ttf"));
    const QStringList families =
        id >= 0 ? QFontDatabase::applicationFontFamilies(id) : QStringList();
    family = families.isEmpty() ? QStringLiteral("sans-serif") : families.first();
  });
  return family;
}

void boxPass(const QRgb* src, QRgb* dst, int n, int stride, int r) {
  // One running-sum box filter along a line of n pixels (edge clamped).
  int sum[4] = {0, 0, 0, 0};
  auto at = [&](int i) { return src[size_t(std::clamp(i, 0, n - 1)) * stride]; };
  for (int i = -r; i <= r; ++i) {
    const QRgb p = at(i);
    sum[0] += qRed(p); sum[1] += qGreen(p); sum[2] += qBlue(p); sum[3] += qAlpha(p);
  }
  const int w = 2 * r + 1;
  for (int i = 0; i < n; ++i) {
    dst[size_t(i) * stride] =
        qRgba(sum[0] / w, sum[1] / w, sum[2] / w, sum[3] / w);
    const QRgb add = at(i + r + 1), sub = at(i - r);
    sum[0] += qRed(add) - qRed(sub);
    sum[1] += qGreen(add) - qGreen(sub);
    sum[2] += qBlue(add) - qBlue(sub);
    sum[3] += qAlpha(add) - qAlpha(sub);
  }
}

// Signed distance to a rounded box centred at the origin (negative inside).
double roundedBoxSdf(double x, double y, double halfW, double halfH, double r) {
  const double qx = std::abs(x) - (halfW - r);
  const double qy = std::abs(y) - (halfH - r);
  const double ox = std::max(qx, 0.0), oy = std::max(qy, 0.0);
  return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0) - r;
}

// Bilinear sample of an RGB32/ARGB32 image (edge clamped).
QRgb sampleBilinear(const QImage& img, double x, double y) {
  x = std::clamp(x, 0.0, double(img.width() - 1));
  y = std::clamp(y, 0.0, double(img.height() - 1));
  const int x0 = int(x), y0 = int(y);
  const int x1 = std::min(x0 + 1, img.width() - 1);
  const int y1 = std::min(y0 + 1, img.height() - 1);
  const double fx = x - x0, fy = y - y0;
  const QRgb* r0 = reinterpret_cast<const QRgb*>(img.constScanLine(y0));
  const QRgb* r1 = reinterpret_cast<const QRgb*>(img.constScanLine(y1));
  auto mix = [&](int (*ch)(QRgb)) {
    const double top = ch(r0[x0]) * (1 - fx) + ch(r0[x1]) * fx;
    const double bottom = ch(r1[x0]) * (1 - fx) + ch(r1[x1]) * fx;
    return int(std::lround(top * (1 - fy) + bottom * fy));
  };
  return qRgb(mix(qRed), mix(qGreen), mix(qBlue));
}

}  // namespace

QImage boxBlur(const QImage& image, int radius) {
  QImage a = image.convertToFormat(QImage::Format_ARGB32);
  if (radius <= 0 || a.isNull()) return a;
  QImage b(a.size(), QImage::Format_ARGB32);
  const int w = a.width(), h = a.height();
  for (int pass = 0; pass < 3; ++pass) {
    for (int y = 0; y < h; ++y)
      boxPass(reinterpret_cast<const QRgb*>(a.constScanLine(y)),
              reinterpret_cast<QRgb*>(b.scanLine(y)), w, 1, radius);
    const int stride = int(b.bytesPerLine() / 4);
    for (int x = 0; x < w; ++x)
      boxPass(reinterpret_cast<const QRgb*>(b.constBits()) + x,
              reinterpret_cast<QRgb*>(a.bits()) + x, h, stride, radius);
  }
  return a;
}

// --- Compositor ----------------------------------------------------------------

Composer::Composer(std::vector<Step> steps, QSize canvas, Timing timing,
                   PillOptions pill, bool showLine)
    : m_steps(std::move(steps)),
      m_canvas(canvas),
      m_timing(timing),
      m_pill(pill),
      m_showLine(showLine) {
  m_fontFamily = pillFontFamily();
  if (m_steps.empty() || m_canvas.isEmpty()) return;

  // The photo fits the canvas (contain); the free space shows a blurred,
  // slightly dimmed extension of the same frame (as the iPhone does).
  const QSize photo = m_steps.front().image.size();
  const QSize fitted = photo.scaled(m_canvas, Qt::KeepAspectRatio);
  m_photoRect = QRect(QPoint((m_canvas.width() - fitted.width()) / 2,
                             (m_canvas.height() - fitted.height()) / 2),
                      fitted);
  const bool needsExtension = fitted != m_canvas;
  for (const Step& step : m_steps) {
    QImage layer(m_canvas, QImage::Format_RGB32);
    layer.fill(Qt::black);
    QPainter p(&layer);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    if (needsExtension) {
      const QSize cover = photo.scaled(m_canvas, Qt::KeepAspectRatioByExpanding);
      // Blur at 1/8 scale (a wide, smooth blur for little work).
      const QSize small(std::max(8, cover.width() / 8), std::max(8, cover.height() / 8));
      QImage bg = boxBlur(step.image.scaled(small, Qt::IgnoreAspectRatio,
                                            Qt::SmoothTransformation),
                          std::max(2, std::min(small.width(), small.height()) / 16));
      bg = bg.scaled(cover, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
      p.drawImage(QPoint((m_canvas.width() - cover.width()) / 2,
                         (m_canvas.height() - cover.height()) / 2),
                  bg);
      p.fillRect(layer.rect(), QColor(0, 0, 0, 28));  // ~11 % dimmer
    }
    p.drawImage(m_photoRect, step.image.scaled(fitted, Qt::IgnoreAspectRatio,
                                               Qt::SmoothTransformation));
    p.end();
    m_layers.push_back(std::move(layer));
  }

  const Timing& t = m_timing;
  const double perStep = t.fade + t.pillHold + t.pillOut + t.stepRest;
  const int groups = int(m_steps.size()) - 1;
  double total = t.intro + groups * perStep;
  if (groups > 0) {
    // The first sweep, plus two return sweeps of half its length.
    total += t.outroHold + t.toOriginal + t.originalHold + t.sweep +
             t.sweepHold + t.sweep / 2.0 + t.leftHold + t.sweep / 2.0;
  }
  total += t.endHold;
  // Nearest frame: durations are meant in whole frames, and summing them
  // in floating point must not add one.
  m_frameCount = std::max(1, int(std::lround(total * t.fps)));
}

QImage Composer::blend(int a, int b, double t) const {
  if (t <= 0.0) return m_layers[size_t(a)].copy();
  if (t >= 1.0) return m_layers[size_t(b)].copy();
  QImage out = m_layers[size_t(a)].copy();
  QPainter p(&out);
  p.setOpacity(t);
  p.drawImage(0, 0, m_layers[size_t(b)]);
  return out;
}

QImage Composer::split(double position, double lineOpacity) const {
  // The line travels across the photo: after on its left, before on its
  // right. The blurred surroundings (other aspect ratios) crossfade with the
  // sweep, so they always match the side being revealed.
  const int last = int(m_layers.size()) - 1;
  const QRect photo = m_photoRect;
  position = clamp01(position);
  const int x = photo.left() + int(std::lround(position * photo.width()));
  QImage out = m_layers.front().copy();
  QPainter p(&out);
  p.setRenderHint(QPainter::Antialiasing);
  if (photo != QRect(QPoint(0, 0), m_canvas) && position > 0.0) {
    p.save();
    p.setClipRegion(QRegion(QRect(QPoint(0, 0), m_canvas)).subtracted(QRegion(photo)));
    p.setOpacity(position);
    p.drawImage(0, 0, m_layers[size_t(last)]);
    p.restore();
  }
  const QRect revealed(photo.left(), photo.top(), x - photo.left(), photo.height());
  p.drawImage(revealed, m_layers[size_t(last)], revealed);
  if (m_showLine && lineOpacity > 0.0) {
    const double w = std::max(2.0, std::min(m_canvas.width(), m_canvas.height()) / 360.0);
    // At the edges the line stays fully on the photo.
    const double cx =
        std::clamp(double(x), photo.left() + w / 2.0, photo.left() + photo.width() - w / 2.0);
    p.setOpacity(lineOpacity);
    p.setClipRect(photo);
    // Soft shadow, then the line.
    for (int i = 4; i >= 1; --i) {
      p.fillRect(QRectF(cx - w / 2 - i * w, photo.top(), w + 2 * i * w, photo.height()),
                 QColor(0, 0, 0, 12));
    }
    p.fillRect(QRectF(cx - w / 2, photo.top(), w, photo.height()),
               QColor(255, 255, 255, 235));
  }
  return out;
}

QRectF Composer::pillRect(const QString& text) const {
  const double shortSide = std::min(m_canvas.width(), m_canvas.height());
  QFont font(m_fontFamily);
  font.setPixelSize(std::max(12, int(std::lround(shortSide * 0.042))));
  font.setWeight(QFont::DemiBold);
  font.setLetterSpacing(QFont::PercentageSpacing, 101.0);
  const QFontMetricsF fm(font);
  const double fontPx = font.pixelSize();
  const double h = fontPx * 2.3;
  const double w = fm.horizontalAdvance(text) + fontPx * 2.2;
  // Positioned on the photo (not the blurred extension), with a margin.
  const double margin = shortSide * 0.07;
  double cy = m_canvas.height() / 2.0;
  if (m_pill.position == PillPosition::Top) cy = m_photoRect.top() + margin + h / 2.0;
  if (m_pill.position == PillPosition::Bottom)
    cy = m_photoRect.bottom() + 1 - margin - h / 2.0;
  return QRectF(m_canvas.width() / 2.0 - w / 2.0, cy - h / 2.0, w, h);
}

void Composer::drawPill(QImage& frame, const QString& text, double opacity) const {
  if (opacity <= 0.0 || text.isEmpty()) return;
  const double shortSide = std::min(m_canvas.width(), m_canvas.height());
  QFont font(m_fontFamily);
  font.setPixelSize(std::max(12, int(std::lround(shortSide * 0.042))));
  font.setWeight(QFont::DemiBold);
  font.setLetterSpacing(QFont::PercentageSpacing, 101.0);
  // Appears with a slight grow (0.94 -> 1), like the system overlays.
  const QRectF full = pillRect(text);
  const double scale = 0.94 + 0.06 * opacity;
  const QRectF pill(full.center().x() - full.width() * scale / 2.0,
                    full.center().y() - full.height() * scale / 2.0,
                    full.width() * scale, full.height() * scale);
  const double h = pill.height();
  const double radius = m_pill.shape == PillShape::Capsule ? h / 2.0 : h * 0.28;
  QPainterPath shape;
  shape.addRoundedRect(pill, radius, radius);

  QPainter p(&frame);
  p.setRenderHint(QPainter::Antialiasing);
  p.setRenderHint(QPainter::TextAntialiasing);
  p.setRenderHint(QPainter::SmoothPixmapTransform);
  p.setOpacity(opacity);

  // Soft shadow under the pill (lighter for clear glass).
  const int shadowAlpha = m_pill.style == PillStyle::LiquidGlass ? 6 : 10;
  for (int i = 6; i >= 1; --i) {
    QPainterPath s;
    const double grow = i * h * 0.035;
    s.addRoundedRect(pill.adjusted(-grow, -grow + h * 0.06, grow, grow + h * 0.06),
                     radius + grow, radius + grow);
    p.fillPath(s, QColor(0, 0, 0, shadowAlpha));
  }

  QColor textColor(255, 255, 255);
  switch (m_pill.style) {
    case PillStyle::FrostedGlass: {
      // The frame behind the pill, blurred, under a translucent dark tint and
      // a faint top sheen (no border).
      const int margin = int(h);
      const QRect area = pill.toAlignedRect()
                             .adjusted(-margin, -margin, margin, margin)
                             .intersected(frame.rect());
      const QSize small(std::max(4, area.width() / 4), std::max(4, area.height() / 4));
      QImage backdrop = boxBlur(frame.copy(area).scaled(small, Qt::IgnoreAspectRatio,
                                                        Qt::SmoothTransformation),
                                std::max(2, int(h / 10.0)));
      backdrop = backdrop.scaled(area.size(), Qt::IgnoreAspectRatio,
                                 Qt::SmoothTransformation);
      p.save();
      p.setClipPath(shape);
      p.drawImage(area.topLeft(), backdrop);
      p.fillPath(shape, QColor(16, 16, 18, 102));
      QLinearGradient sheen(pill.topLeft(), pill.bottomLeft());
      sheen.setColorAt(0.0, QColor(255, 255, 255, 26));
      sheen.setColorAt(0.5, QColor(255, 255, 255, 0));
      p.fillPath(shape, sheen);
      p.restore();
      break;
    }
    case PillStyle::LiquidGlass: {
      // Clear glass that bends the scene near its rim like a thick lens
      // (sampled outwards along the edge normal), a slight lift, a specular
      // highlight on the upper rim and a soft inner shade below.
      const double rim = h * 0.34;
      const double bend = h * 0.15;
      const int margin = int(std::ceil(bend)) + 2;
      const QRect area = pill.toAlignedRect()
                             .adjusted(-margin, -margin, margin, margin)
                             .intersected(frame.rect());
      const QImage scene =
          boxBlur(frame.copy(area), 1).convertToFormat(QImage::Format_RGB32);
      QImage glass(area.size(), QImage::Format_ARGB32_Premultiplied);
      glass.fill(Qt::transparent);
      const double cx = pill.center().x() - area.left();
      const double cy = pill.center().y() - area.top();
      const double halfW = pill.width() / 2.0, halfH = pill.height() / 2.0;
      const double lx = -0.45, ly = -0.89;  // light from the upper left
      for (int y = 0; y < glass.height(); ++y) {
        QRgb* out = reinterpret_cast<QRgb*>(glass.scanLine(y));
        for (int x = 0; x < glass.width(); ++x) {
          const double px = x + 0.5 - cx, py = y + 0.5 - cy;
          const double d = roundedBoxSdf(px, py, halfW, halfH, radius);
          if (d > 1.0) continue;
          // Outward normal from the distance field's gradient.
          double nx = roundedBoxSdf(px + 0.5, py, halfW, halfH, radius) -
                      roundedBoxSdf(px - 0.5, py, halfW, halfH, radius);
          double ny = roundedBoxSdf(px, py + 0.5, halfW, halfH, radius) -
                      roundedBoxSdf(px, py - 0.5, halfW, halfH, radius);
          const double nl = std::sqrt(nx * nx + ny * ny);
          if (nl > 1e-9) {
            nx /= nl;
            ny /= nl;
          }
          const double edge = clamp01(1.0 + d / rim);  // 1 at the rim, 0 inside
          const double shift = bend * edge * edge;
          const QRgb c = sampleBilinear(scene, x + nx * shift, y + ny * shift);
          double r = qRed(c) * 1.05 + 6, g = qGreen(c) * 1.05 + 6,
                 b = qBlue(c) * 1.05 + 6;
          const double facing = nx * lx + ny * ly;  // > 0 towards the light
          // Specular on the lit side, a thin light rim all around (the glass
          // edge catching light; it defines the shape on any scene) and a
          // soft shade on the far side.
          const double spec = std::pow(edge, 3.0) * std::max(0.0, facing);
          const double ring = std::pow(edge, 10.0) * (0.22 + 0.18 * std::max(0.0, facing));
          const double shade = std::pow(edge, 2.0) * std::max(0.0, -facing) * 0.16;
          const double light = std::min(1.0, spec * 0.5 + ring);
          r = r * (1.0 - shade) * (1.0 - light) + 255.0 * light;
          g = g * (1.0 - shade) * (1.0 - light) + 255.0 * light;
          b = b * (1.0 - shade) * (1.0 - light) + 255.0 * light;
          const double a = clamp01(0.5 - d);  // anti-aliased edge
          out[x] = qRgba(int(std::clamp(r, 0.0, 255.0) * a),
                         int(std::clamp(g, 0.0, 255.0) * a),
                         int(std::clamp(b, 0.0, 255.0) * a), int(255 * a));
        }
      }
      p.drawImage(area.topLeft(), glass);
      // A hint of tint keeps white text readable on bright scenes.
      p.fillPath(shape, QColor(0, 0, 0, 22));
      break;
    }
    case PillStyle::White:
      p.fillPath(shape, QColor(255, 255, 255, 238));
      textColor = QColor(28, 28, 30);
      break;
    case PillStyle::Black:
      p.fillPath(shape, QColor(10, 10, 12, 228));
      break;
  }

  p.setFont(font);
  if (m_pill.style == PillStyle::LiquidGlass) {
    // Soft shadow under the text: legible over any scene through clear glass.
    p.setPen(QColor(0, 0, 0, 70));
    p.drawText(pill.translated(0, std::max(1.0, h / 60.0)), Qt::AlignCenter, text);
  }
  p.setPen(textColor);
  p.drawText(pill, Qt::AlignCenter, text);
}

QImage Composer::frame(int index) const {
  if (m_layers.empty()) return QImage();
  const Timing& tm = m_timing;
  double t = double(std::clamp(index, 0, m_frameCount - 1)) / tm.fps;
  const int last = int(m_layers.size()) - 1;

  if (t < tm.intro || last == 0) return m_layers.front().copy();
  t -= tm.intro;

  const double perStep = tm.fade + tm.pillHold + tm.pillOut + tm.stepRest;
  for (int step = 1; step <= last; ++step) {
    if (t < perStep) {
      QImage out = blend(step - 1, step, ease(t / tm.fade));
      const double in = ease(t / tm.pillIn);
      const double outT = t - (tm.fade + tm.pillHold);
      const double fadeOut = outT > 0.0 ? 1.0 - ease(outT / tm.pillOut) : 1.0;
      drawPill(out, m_steps[size_t(step)].name, std::min(in, fadeOut));
      return out;
    }
    t -= perStep;
  }

  // Closing sequence: back to the original, slow slider to the edit, the
  // line resting on the right, a sweep back to the left (the original) at
  // twice the speed, a second there, a sweep to the right (the edit) at
  // twice the speed, the line fading out. All sweeps ease in and out.
  if (t < tm.outroHold) return m_layers[size_t(last)].copy();
  t -= tm.outroHold;
  if (t < tm.toOriginal) return blend(last, 0, ease(t / tm.toOriginal));
  t -= tm.toOriginal;
  if (t < tm.originalHold) return m_layers.front().copy();
  t -= tm.originalHold;
  if (t < tm.sweep) return split(ease(t / tm.sweep), 1.0);
  t -= tm.sweep;
  if (t < tm.sweepHold) return split(1.0, 1.0);
  t -= tm.sweepHold;
  const double fast = tm.sweep / 2.0;
  if (t < fast) return split(1.0 - ease(t / fast), 1.0);
  t -= fast;
  if (t < tm.leftHold) return split(0.0, 1.0);
  t -= tm.leftHold;
  if (t < fast) return split(ease(t / fast), 1.0);
  t -= fast;
  if (t < tm.lineFade) return split(1.0, 1.0 - ease(t / tm.lineFade));
  return m_layers[size_t(last)].copy();
}

}  // namespace photon::editvideo
