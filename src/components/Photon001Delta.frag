#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D logSource;
layout(binding = 2) uniform sampler2D gaussSmall;
layout(binding = 3) uniform sampler2D gaussBig;
layout(binding = 4) uniform sampler2D minmaxmeanSource;
layout(binding = 5) uniform sampler2D momentsSource;
layout(binding = 6) uniform sampler2D reductionSource;

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
} ubuf;

const float PHOTON001_FLARE_LINEAR = 0.000244140625; // 2^-12
const float PHOTON001_FLARE_LOG = -12.0;
const float PHOTON001_EPS = 0.00000190734;

float photon001_encode_log_luma(float linearLuma) {
    return log2(max(linearLuma + PHOTON001_FLARE_LINEAR, PHOTON001_EPS));
}

vec2 endpoint_pin_mask(vec2 x) {
    x = clamp(x, 0.0, 1.0);
    vec2 invX = 1.0 - x;
    vec2 inv2 = invX * invX;
    vec2 inv4 = inv2 * inv2;
    vec2 inv8 = inv4 * inv4;
    vec2 inv16 = inv8 * inv8;
    vec2 base = 1.0 - inv8;
    vec2 strong = 1.0 - inv16;
    return mix(base, strong, smoothstep(vec2(0.35), vec2(1.0), x));
}

float photon001_tent_weight(float value, float center, float halfWidth) {
    return max(1.0 - abs(value - center) / max(halfWidth, PHOTON001_EPS), 0.0);
}

float photon001_triangle_weight(float value, float reference, float invDs) {
    float line0 = invDs * (value - reference) + 1.0;
    float line1 = invDs * (reference - value) + 1.0;
    return max(min(line0, line1), 0.0);
}

float photon001_local_laplacian_mask(float srcLog, float blurFineLog, float blurCoarseLog, float toneMid) {
    float maskFine = clamp(srcLog - blurFineLog, -2.0, 2.0);
    float maskCoarse = clamp(blurFineLog - blurCoarseLog, -2.0, 2.0);
    float baseResidual = clamp(maskFine * 0.70 + maskCoarse * 0.45, -2.5, 2.5);

    float accum = 0.0;
    float weightSum = 0.0;
    const float invDs = 0.5; // ds = 2 stops between references

    for (int i = 0; i < 5; ++i) {
        float ref = toneMid + (float(i) - 2.0) * 2.0;
        float alpha = photon001_triangle_weight(srcLog, ref, invDs);
        float levelGain = 1.0 + (float(i) - 2.0) * 0.08;
        accum += alpha * baseResidual * levelGain;
        weightSum += alpha;
    }

    return clamp(accum / max(weightSum, PHOTON001_EPS), -2.5, 2.5);
}

void main() {
    float srcGrayLog = texture(logSource, qt_TexCoord0).x;
    float blurFineLog = texture(gaussSmall, qt_TexCoord0).x;
    float blurCoarseLog = texture(gaussBig, qt_TexCoord0).x;

    vec3 minmaxmean = texture(minmaxmeanSource, qt_TexCoord0).xyz;
    vec2 moments = texture(momentsSource, qt_TexCoord0).xy;
    vec2 reduction = texture(reductionSource, qt_TexCoord0).xy;

    float highlightsAmt = ubuf.highlights / 100.0;
    float shadowsAmt = ubuf.shadows / 100.0;
    float whitesAmt = ubuf.whites / 100.0;
    float blacksAmt = ubuf.blacks / 100.0;
    float clarityAmt = ubuf.clarity / 100.0;

    float sceneWhiteNorm = max(ubuf.sceneWhite * pow(2.0, ubuf.exposure), 1e-4);
    float toneMid = photon001_encode_log_luma(max(sceneWhiteNorm * 0.18, PHOTON001_EPS));

    float wBlacks = photon001_tent_weight(srcGrayLog, toneMid - 3.8, 1.8);
    float wShadows = photon001_tent_weight(srcGrayLog, toneMid - 1.9, 1.9);
    float wHighlights = photon001_tent_weight(srcGrayLog, toneMid + 1.0, 1.9);
    float wWhites = photon001_tent_weight(srcGrayLog, toneMid + 3.1, 2.2);

    float mask = photon001_local_laplacian_mask(srcGrayLog, blurFineLog, blurCoarseLog, toneMid);

    float partSwitch = step(srcGrayLog, toneMid);
    float compressedLow = toneMid + (srcGrayLog - toneMid) * 0.78;
    float compressedHigh = toneMid + (srcGrayLog - toneMid) * 0.58;
    float baseCompressed = mix(compressedHigh, compressedLow, partSwitch);

    float localContrastSignal = srcGrayLog + mask - baseCompressed;
    localContrastSignal *= max(clarityAmt, 0.0);
    localContrastSignal *= clamp(1.0 + 0.35 * (-highlightsAmt + shadowsAmt), 1.0, 2.0);

    // Adaptation phases from min/max/mean -> moments -> reduction
    float rangeSpan = max(minmaxmean.y - minmaxmean.x, 0.0);
    float varianceGate = clamp(reduction.x * 96.0, 0.0, 1.0);
    float skewGate = clamp(reduction.y * 32.0, -1.0, 1.0);
    float localVariance = clamp(moments.x * 128.0, 0.0, 1.0);
    localContrastSignal *= mix(0.88, 1.22, 0.5 * varianceGate + 0.5 * localVariance);
    localContrastSignal += 0.08 * skewGate;
    localContrastSignal *= 1.0 + clamp(rangeSpan * 0.08, 0.0, 0.25);

    vec2 localContrastSignal2 = vec2(max(localContrastSignal, 0.0), min(localContrastSignal, 0.0));

    vec2 lumWeight = vec2(
        clamp(wHighlights + 0.6 * wWhites, 0.0, 1.0),
        clamp(wShadows + 0.6 * wBlacks, 0.0, 1.0)
    );
    vec2 endpointStrength = clamp(
        vec2(abs(highlightsAmt) + 0.35 * abs(whitesAmt), abs(shadowsAmt) + 0.35 * abs(blacksAmt)),
        0.0,
        1.0
    );
    vec2 claritySHPinMask = mix(endpoint_pin_mask(lumWeight), vec2(1.0), endpointStrength * endpointStrength);

    vec2 hsPinMask;
    hsPinMask.y = mix(0.5 + 0.5 * max(1.0 - sign(shadowsAmt), 0.0), 1.0, claritySHPinMask.x);
    hsPinMask.x = mix(1.0, 0.5, (1.0 - claritySHPinMask.y) * max(-sign(highlightsAmt), 0.0));
    hsPinMask.x = mix(1.0, hsPinMask.x, clamp(abs(highlightsAmt), 0.0, 1.0));

    float maxAbsHS = max(max(abs(highlightsAmt), abs(shadowsAmt)), PHOTON001_EPS);
    float baseOffset = 0.85 * (highlightsAmt + shadowsAmt) / maxAbsHS;
    vec2 offsetHS = vec2(wHighlights, wShadows) * vec2(abs(highlightsAmt), abs(shadowsAmt)) * baseOffset;
    vec2 deltaHS = vec2(-highlightsAmt, shadowsAmt);
    deltaHS = clamp(deltaHS, -1.0, 1.0);
    deltaHS *= vec2(min(mask, 0.0), max(mask, 0.0));
    deltaHS += offsetHS;

    float deltaStops = dot(deltaHS, hsPinMask);
    deltaStops += whitesAmt * wWhites * hsPinMask.x;
    deltaStops += blacksAmt * wBlacks * hsPinMask.y;
    deltaStops += dot(localContrastSignal2, claritySHPinMask);

    float deltaSign = sign(deltaStops);
    float flareSwitch = 1.0 - max(deltaSign, 0.0);
    float zeroSwitch = 1.0 - abs(deltaSign);
    float flare = flareSwitch * PHOTON001_FLARE_LOG;
    float startpoint = flare - (deltaStops + deltaStops);
    float t1 = step(startpoint, srcGrayLog);
    float t2 = step(srcGrayLog, startpoint);
    float t = clamp((srcGrayLog - startpoint) / (flare - startpoint + zeroSwitch), 0.0, 1.0);
    t *= t * (1.0 - mix(t2, t1, flareSwitch));
    deltaStops = mix(deltaStops, 0.0, t);

    // deltamask phase with shadow-gain cap (+4 stops max lift)
    float dst = srcGrayLog + deltaStops;
    float diff = dst - srcGrayLog;
    if (diff > 0.0) {
        diff = min(diff, 4.0);
    }
    deltaStops = diff;

    fragColor = vec4(deltaStops, 0.0, 0.0, 1.0);
}
