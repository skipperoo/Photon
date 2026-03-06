#pragma once

#include <QImage>
#include <QJsonObject>
#include <algorithm>
#include <cmath>
#include <vector>

class QRhi;
class QQuickWindow;

namespace photon {

struct HSV {
  float h, s, v;
};

class ImageDeveloper {
 public:
  static QImage develop(const ushort* src, int width, int height,
                        const QJsonObject& settings, QRhi* rhi = nullptr,
                        QQuickWindow* window = nullptr);

 private:
  static float smoothstep(float edge0, float edge1, float x) {
    float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
  }

  static float mix(float a, float b, float t) { return a + t * (b - a); }

  static HSV rgb_to_hsv(float r, float g, float b) {
    float min = std::min({r, g, b});
    float max = std::max({r, g, b});
    float delta = max - min;
    HSV hsv = {0, 0, max};
    if (delta > 0.00001f) {
      hsv.s = delta / max;
      if (r == max)
        hsv.h = (g - b) / delta;
      else if (g == max)
        hsv.h = 2.0f + (b - r) / delta;
      else
        hsv.h = 4.0f + (r - g) / delta;
      hsv.h *= 60.0f;
      if (hsv.h < 0) hsv.h += 360.0f;
    }
    return hsv;
  }

  static void hsv_to_rgb(float h, float s, float v, float& r, float& g,
                         float& b) {
    if (s <= 0.00001f) {
      r = g = b = v;
      return;
    }
    h /= 60.0f;
    int i = (int)h;
    float f = h - i;
    float p = v * (1.0f - s);
    float q = v * (1.0f - s * f);
    float t = v * (1.0f - s * (1.0f - f));
    switch (i % 6) {
      case 0:
        r = v;
        g = t;
        b = p;
        break;
      case 1:
        r = q;
        g = v;
        b = p;
        break;
      case 2:
        r = p;
        g = v;
        b = t;
        break;
      case 3:
        r = p;
        g = q;
        b = v;
        break;
      case 4:
        r = t;
        g = p;
        b = v;
        break;
      case 5:
        r = v;
        g = p;
        b = q;
        break;
    }
  }

  static float get_hsl_influence(float h, float center, float width) {
    float dist = std::abs(h - center);
    if (dist > 180.0f) dist = 360.0f - dist;
    float effectiveWidth = std::max(width * 1.25f, 1e-6f);
    float falloff = dist / (effectiveWidth * 0.5f);
    return std::exp(-0.85f * falloff * falloff);
  }
};

}  // namespace photon
