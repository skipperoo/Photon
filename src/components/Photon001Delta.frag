#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D logSource;
layout(binding = 2) uniform sampler2D gaussSmall;
layout(binding = 3) uniform sampler2D gaussBig;
layout(binding = 4) uniform sampler2D toneMask;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float highlights;
    float shadows;
    float whites;
    float blacks;
    float clarity;
    float sceneWhite;
    float exposure;
    float sceneDetailScale;
    float sceneHighlightPin;
    float sceneCompression;
    float toneBandScale;
    vec4 imageRect;     // displayed image rect in viewport pixels
    vec2 viewportSize;
    vec2 maskSize;      // tone mask grid (a | mu | g planes: maskSize.x each)
    vec4 maskRoi;       // part of the image the displayed texture covers
} ubuf;

const float PHOTON001_FLARE_LINEAR = 0.000244140625; // 2^-12
const float PHOTON001_FLARE_LOG = -12.0;
const float PHOTON001_EPS = 0.00000190734;

float photon001_encode_log_luma(float linearLuma) {
    return log2(max(linearLuma + PHOTON001_FLARE_LINEAR, PHOTON001_EPS));
}

// Mirrors photon::tone (src/engine/ToneMath.h) line by line: keep in sync.
float smoothstep01(float t) {
    t = clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}
// Display-referred bands (x = log2 luminance relative to mid-grey 0.18).
float band_weight(float x, float center, float low, float high) {
    float d = x < center ? (center - x) / low : (x - center) / high;
    return smoothstep01(1.0 - d);
}
// Constants fitted to Reference (photon::tone, ToneMath.h).
float shadows_weight(float x) { return band_weight(x, -5.44, 3.16, 6.0); }
float signed_gain(float v, float neg, float pos) { return v < 0.0 ? v * neg : v * pos; }
float srgb_encode(float l) {
    l = clamp(l, 0.0, 1.0);
    return l <= 0.0031308 ? l * 12.92 : 1.055 * pow(l, 1.0 / 2.4) - 0.055;
}
float srgb_decode(float e) {
    e = clamp(e, 0.0, 1.0);
    return e <= 0.04045 ? e / 12.92 : pow((e + 0.055) / 1.055, 2.4);
}
// photon::tone::blacksStops: levels black point on the display-encoded luminance.
float blacks_stops(float l, float b) {
    if (b == 0.0) return 0.0;
    float e = srgb_encode(l);
    float eOut;
    if (b < 0.0) {
        float bp = min(0.20 * pow(-b, 1.14), 0.95);
        eOut = max(e - bp, 0.0) / (1.0 - bp);
    } else {
        eOut = e + 0.064 * pow(b, 1.02) * (1.0 - e);
    }
    return log2(max(srgb_decode(eOut), 1e-7) / max(l, 1e-7));
}
// photon::tone::liftRolloffStops (kLiftShoulder 4, kMaxLiftStops 4).
float lift_rolloff_stops(float m, float lift) {
    lift = min(lift, 4.0);
    if (m >= 1.0) return 0.0;
    if (m <= 1e-6) return lift;
    float g = m / pow(1.0 - pow(m, 4.0), 0.25);
    float kg = exp2(lift) * g;
    float outM = kg / pow(1.0 + pow(kg, 4.0), 0.25);
    return log2(outM / m);
}
float highlights_weight(float x) { return band_weight(x, 0.83, 5.0, 4.0); }
float whites_weight(float x) { return smoothstep01((x + 6.96) / 5.82); }
// photon::tone::whitesStops
float whites_stops(float x, float w) {
    if (w >= 0.0) return 2.65 * pow(w, 2.62) * whites_weight(x);
    float t = clamp((x + 4.88) / (2.474 + 4.88), 0.0, 1.0);
    return 0.553 * w * t * t;
}

// Edge-aware tone mask (photon::tone::ToneMask): 16-bit values packed in the
// R (high) / G (low) bytes; planes a | mu | g side by side. Manual bilinear
// filtering (packed bytes cannot be filtered by the sampler).
float mask_texel(int x, int y, float plane) {
    float w = max(ubuf.maskSize.x, 1.0);
    float h = max(ubuf.maskSize.y, 1.0);
    vec2 uv = vec2((float(x) + plane * w + 0.5) / (3.0 * w), (float(y) + 0.5) / h);
    vec4 p = texture(toneMask, uv);
    return (floor(p.r * 255.0 + 0.5) * 256.0 + floor(p.g * 255.0 + 0.5)) / 65535.0;
}

float mask_sample(vec2 uv, float plane) {
    float w = max(ubuf.maskSize.x, 1.0);
    float h = max(ubuf.maskSize.y, 1.0);
    float fx = clamp(uv.x * w - 0.5, 0.0, w - 1.0);
    float fy = clamp(uv.y * h - 0.5, 0.0, h - 1.0);
    int x0 = int(floor(fx));
    int y0 = int(floor(fy));
    int x1 = min(x0 + 1, int(w) - 1);
    int y1 = min(y0 + 1, int(h) - 1);
    float tx = fx - float(x0);
    float ty = fy - float(y0);
    float top = mix(mask_texel(x0, y0, plane), mask_texel(x1, y0, plane), tx);
    float bottom = mix(mask_texel(x0, y1, plane), mask_texel(x1, y1, plane), tx);
    return mix(top, bottom, ty);
}

// Log luminance through the exposure map (photon::tone::exposureLuma).
float exposed_log(float logL, float exposureGain) {
    float l = max(exp2(logL) - PHOTON001_FLARE_LINEAR, PHOTON001_EPS);
    float outL = l * exposureGain;
    if (ubuf.exposure > 0.0) {
        float headroom = 1.0 - l;
        if (headroom > 0.0) {
            float q = l * (exposureGain - 1.0) / headroom;
            float p = 1.0 + clamp((ubuf.exposure - 1.0) * 0.5, 0.0, 1.0);
            outL = l + headroom * (q / pow(1.0 + pow(q, p), 1.0 / p));
        } else {
            outL = l;
        }
    }
    return photon001_encode_log_luma(outL);
}

void main() {
    vec3 logs = texture(logSource, qt_TexCoord0).xyz;
    float srcLog = logs.x;     // working (unrendered) log luminance
    float actualLog = logs.y;  // Rec.709 log luminance
    float maxLog = logs.z;     // log2 of the brightest channel
    float fineLog = texture(gaussSmall, qt_TexCoord0).x;
    float coarseLog = texture(gaussBig, qt_TexCoord0).x;

    float highlights = ubuf.highlights / 100.0;
    float shadows = ubuf.shadows / 100.0;
    float whites = ubuf.whites / 100.0;
    float blacks = ubuf.blacks / 100.0;
    float clarity = ubuf.clarity / 100.0;
    float bandScale = ubuf.toneBandScale;

    float exposureGain = exp2(ubuf.exposure > 0.0 ? ubuf.exposure * 0.85 : ubuf.exposure);
    float toneMid = photon001_encode_log_luma(0.18);

    // Band position: the edge-aware tone mask at this pixel
    // (photon::tone::maskLogExposed): mask = a * (g + shrink(I - g)) +
    // (1 - a) * mu, with g and mu through the exposure map. A 1 x 1 grid is
    // the identity (no mask yet).
    float bandLog = actualLog;
    if (ubuf.maskSize.x > 1.5 && ubuf.maskSize.y > 1.5) {
        vec2 pixelPos = qt_TexCoord0 * ubuf.viewportSize;
        vec2 imgUV = clamp((pixelPos - ubuf.imageRect.xy) / max(ubuf.imageRect.zw, vec2(1.0)),
                           0.0, 1.0);
        vec2 maskUV = ubuf.maskRoi.xy + imgUV * ubuf.maskRoi.zw;
        float maskA = mask_sample(maskUV, 0.0);
        float maskMu = -16.0 + 20.0 * mask_sample(maskUV, 1.0);
        float maskG = -16.0 + 20.0 * mask_sample(maskUV, 2.0);
        float gExposed = exposed_log(maskG, exposureGain);
        float d = actualLog - gExposed;
        // shrink(d) = d - T tanh(d / T), T = photon::tone::kDetailThreshold
        float guide = gExposed + d - 0.5 * tanh(d / 0.5);
        bandLog = maskA * guide + (1.0 - maskA) * exposed_log(maskMu, exposureGain);
    }

    float x = bandLog - toneMid;            // region (Shadows, Highlights)
    float xPix = actualLog - toneMid;       // pixel (Blacks, Whites, Clarity)
    float m = exp2(min(maxLog, 0.0));       // brightest channel

    float lapFine = srcLog - fineLog;
    float lapMid = fineLog - coarseLog;
    float detail = clamp(lapFine + 0.6 * lapMid, -2.5, 2.5);
    float localContrast = detail * clarity * ubuf.sceneDetailScale *
                          clamp(1.0 + 0.35 * (-highlights + shadows), 1.0, 2.0);
    // +Shadows keeps the local contrast of the regions it lifts.
    localContrast += detail * 0.3 * max(shadows, 0.0) * shadows_weight(x) *
                     ubuf.sceneDetailScale;
    localContrast *= smoothstep01((xPix + 7.0) / 3.0);

    float hl = signed_gain(highlights, 0.99, 1.03) * highlights_weight(x);
    float recovery = bandScale * (hl + signed_gain(shadows, 1.89, 2.19) * shadows_weight(x) +
                                  whites_stops(xPix, whites));
    recovery += localContrast;

    if (recovery > 0.0) {
        recovery = lift_rolloff_stops(m, recovery);
    } else if (recovery < 0.0) {
        float a = srcLog - PHOTON001_FLARE_LOG;
        if (a <= 0.0) {
            recovery = 0.0;
        } else if (-recovery > 0.5 * a) {
            recovery = -(a - a * a / (4.0 * -recovery));
        }
    }

    // Blacks: levels black point (after the fades, like photon::tone).
    recovery += blacks_stops(max(exp2(actualLog) - PHOTON001_FLARE_LINEAR, PHOTON001_EPS) *
                             exp2(recovery), blacks);

    // .y (separate white-point stops) is no longer used: Whites is in .x.
    fragColor = vec4(recovery, 0.0, 0.0, 1.0);
}
