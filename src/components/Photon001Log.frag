#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D source;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float exposure;
    float temperature;
    float tint;
    vec4 imageRect;
    vec2 viewportSize;
} ubuf;

const float PHOTON001_FLARE_LINEAR = 0.000244140625; // 2^-12
const float PHOTON001_EPS = 0.00000190734;

const mat3 RGB_TO_PROPHOTO = mat3(
    0.529285, 0.098394, 0.016823,
    0.330046, 0.873493, 0.117671,
    0.140669, 0.028113, 0.865506
);

const vec3 PROPHOTO_LUMA_WEIGHTS = vec3(0.25, 0.5, 0.25);

vec3 srgb_to_linear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

vec3 apply_white_balance(vec3 color, float temp, float tnt) {
    vec3 temp_mult = vec3(1.0 + temp * 0.2, 1.0 + temp * 0.05, 1.0 - temp * 0.2);
    vec3 tint_mult = vec3(1.0 + tnt * 0.25, 1.0 - tnt * 0.25, 1.0 + tnt * 0.25);
    return color * temp_mult * tint_mult;
}

vec3 eval_undo_render_curve(vec3 col) {
    vec2 fMinMax;
    vec2 nMinMax;
    const float eps = 0.00001;

    fMinMax.x = min(min(col.r, col.g), col.b);
    fMinMax.y = max(max(col.r, col.g), col.b);

    vec2 t = pow(fMinMax, vec2(3.14453125));
    nMinMax = pow(fMinMax, vec2(0.8125)) * 0.3828125 * (1.0 - t)
            + (1.0 - pow(1.0 - fMinMax, vec2(0.69140625))) * t;

    fMinMax.y = (nMinMax.y - nMinMax.x) / (fMinMax.y - fMinMax.x + eps);
    return (col - fMinMax.x) * fMinMax.y + nMinMax.x;
}

float photon001_working_luma_linear(vec3 c) {
    vec3 prophoto = RGB_TO_PROPHOTO * clamp(c, 0.0001, 0.999);
    vec3 unmapped = clamp(eval_undo_render_curve(prophoto), 0.0, 1.0);
    return max(dot(unmapped, PROPHOTO_LUMA_WEIGHTS), PHOTON001_EPS);
}

float photon001_encode_log_luma(float linearLuma) {
    return log2(max(linearLuma + PHOTON001_FLARE_LINEAR, PHOTON001_EPS));
}

void main() {
    vec2 pixelPos = qt_TexCoord0 * ubuf.viewportSize;
    if (pixelPos.x < ubuf.imageRect.x || pixelPos.x > ubuf.imageRect.x + ubuf.imageRect.z ||
        pixelPos.y < ubuf.imageRect.y || pixelPos.y > ubuf.imageRect.y + ubuf.imageRect.w) {
        fragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec3 color = srgb_to_linear(texture(source, qt_TexCoord0).rgb);
    color = apply_white_balance(color, ubuf.temperature / 100.0, ubuf.tint / 100.0);
    color *= pow(2.0, ubuf.exposure);

    float logL = photon001_encode_log_luma(photon001_working_luma_linear(color));
    fragColor = vec4(vec3(logL), 1.0);
}
