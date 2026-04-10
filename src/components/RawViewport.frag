#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D source;
layout(binding = 2) uniform sampler2D toneLUT;
layout(binding = 3) uniform sampler2D photon001Delta;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float exposure;
    float contrast;
    float highlights;
    float shadows;
    float whites;
    float sceneWhite;
    float blacks;
    float adaptation;
    float vibrance;
    float saturation;
    float temperature;
    float tint;
    float tonemappingEnabled;
    float grainAmount;
    float grainSize;
    float grainRoughness;
    float vignetteAmount;
    float vignetteMidpoint;
    float vignetteRoundness;
    float vignetteFeather;
    vec4 imageRect; 
    vec2 viewportSize;
    vec4 backgroundColor;
    float denoiseAmount;
    vec2 sourceSize;
    float isPreview;
    float clarity;
    float dehaze;
    float structure;
    float centre;
    float sharpness;
    
    // HSL Panel (24 floats)
    float hslRedHue; float hslRedSaturation; float hslRedLuminance;
    float hslOrangeHue; float hslOrangeSaturation; float hslOrangeLuminance;
    float hslYellowHue; float hslYellowSaturation; float hslYellowLuminance;
    float hslGreenHue; float hslGreenSaturation; float hslGreenLuminance;
    float hslAquaHue; float hslAquaSaturation; float hslAquaLuminance;
    float hslBlueHue; float hslBlueSaturation; float hslBlueLuminance;
    float hslPurpleHue; float hslPurpleSaturation; float hslPurpleLuminance;
    float hslMagentaHue; float hslMagentaSaturation; float hslMagentaLuminance;

    // Color Grading (11 floats)
    float cgShadowsHue; float cgShadowsSaturation; float cgShadowsLuminance;
    float cgMidtonesHue; float cgMidtonesSaturation; float cgMidtonesLuminance;
    float cgHighlightsHue; float cgHighlightsSaturation; float cgHighlightsLuminance;
    float cgBalance; float cgBlending;

    // Sharpening Mask
    float sharpenMask;
    float maskFeather;
    float focusDetect;
    float showSharpenMask;

    // Tone Curve
    float toneCurveActive;

    // Before/After bypass
    float showOriginal;
} ubuf;

const vec3 LUMA_COEFF = vec3(0.2126, 0.7152, 0.0722);

float get_luma(vec3 c) {
    return dot(c, LUMA_COEFF);
}

vec3 srgb_to_linear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

vec3 linear_to_srgb(vec3 c) {
    vec3 c_clamped = clamp(c, 0.0, 1.0);
    return mix(c_clamped * 12.92, 1.055 * pow(c_clamped, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c_clamped));
}

// --- Scharr Edge Detection for Sharpening Mask ---
// Returns edge strength [0,1] at a single point.
float compute_edge_mask_raw(sampler2D tex, vec2 uv, vec2 texelSize, float maskAmount) {
    // Sample 3x3 neighborhood luminance
    float tl = get_luma(srgb_to_linear(texture(tex, uv + vec2(-1.0, -1.0) * texelSize).rgb));
    float tc = get_luma(srgb_to_linear(texture(tex, uv + vec2( 0.0, -1.0) * texelSize).rgb));
    float tr = get_luma(srgb_to_linear(texture(tex, uv + vec2( 1.0, -1.0) * texelSize).rgb));
    float ml = get_luma(srgb_to_linear(texture(tex, uv + vec2(-1.0,  0.0) * texelSize).rgb));
    float mr = get_luma(srgb_to_linear(texture(tex, uv + vec2( 1.0,  0.0) * texelSize).rgb));
    float bl = get_luma(srgb_to_linear(texture(tex, uv + vec2(-1.0,  1.0) * texelSize).rgb));
    float bc = get_luma(srgb_to_linear(texture(tex, uv + vec2( 0.0,  1.0) * texelSize).rgb));
    float br = get_luma(srgb_to_linear(texture(tex, uv + vec2( 1.0,  1.0) * texelSize).rgb));

    // Scharr operator (better rotational symmetry than Sobel)
    float gx = -3.0*tl + 3.0*tr - 10.0*ml + 10.0*mr - 3.0*bl + 3.0*br;
    float gy = -3.0*tl - 10.0*tc - 3.0*tr + 3.0*bl + 10.0*bc + 3.0*br;
    float gradient = sqrt(gx*gx + gy*gy);

    // Threshold scales with maskAmount: higher = tighter mask
    float threshold = maskAmount / 100.0 * 0.3;
    float gain = 4.0 + maskAmount / 100.0 * 12.0;
    float mask = clamp((gradient - threshold) * gain, 0.0, 1.0);

    // Smooth the mask slightly to avoid aliasing at mask boundaries
    return mask * mask * (3.0 - 2.0 * mask);
}

// Feathered edge mask: averages mask at center + 4 cardinal neighbors
float compute_edge_mask(sampler2D tex, vec2 uv, vec2 texelSize, float maskAmount, float feather) {
    if (maskAmount <= 0.0) return 1.0;

    float center = compute_edge_mask_raw(tex, uv, texelSize, maskAmount);
    if (feather <= 0.0) return center;

    // Sample at 4 cardinal offsets scaled by feather amount (1–6 texels)
    float r = 1.0 + feather / 100.0 * 5.0;
    float n = compute_edge_mask_raw(tex, uv + vec2(0.0, -r) * texelSize, texelSize, maskAmount);
    float s = compute_edge_mask_raw(tex, uv + vec2(0.0,  r) * texelSize, texelSize, maskAmount);
    float w = compute_edge_mask_raw(tex, uv + vec2(-r, 0.0) * texelSize, texelSize, maskAmount);
    float e = compute_edge_mask_raw(tex, uv + vec2( r, 0.0) * texelSize, texelSize, maskAmount);

    // Weighted average: center 40%, neighbors 15% each
    return center * 0.4 + (n + s + w + e) * 0.15;
}

// Focus detection via local luminance variance over a wide area.
// In-focus regions have high variance; bokeh/OOF regions have low variance.
float compute_focus_gate(sampler2D tex, vec2 uv, vec2 texelSize, float focusAmount) {
    if (focusAmount <= 0.0) return 1.0;

    // Sample luminance at 13 points in a wide sparse pattern (~12 texel radius)
    float r = 12.0;
    float sum = 0.0;
    float sumSq = 0.0;
    float L;

    // Center
    L = get_luma(srgb_to_linear(texture(tex, uv).rgb));
    sum += L; sumSq += L * L;
    // Cardinal directions at full radius
    L = get_luma(srgb_to_linear(texture(tex, uv + vec2( r, 0.0) * texelSize).rgb));
    sum += L; sumSq += L * L;
    L = get_luma(srgb_to_linear(texture(tex, uv + vec2(-r, 0.0) * texelSize).rgb));
    sum += L; sumSq += L * L;
    L = get_luma(srgb_to_linear(texture(tex, uv + vec2(0.0,  r) * texelSize).rgb));
    sum += L; sumSq += L * L;
    L = get_luma(srgb_to_linear(texture(tex, uv + vec2(0.0, -r) * texelSize).rgb));
    sum += L; sumSq += L * L;
    // Diagonals at 0.7 * radius
    float d = r * 0.7;
    L = get_luma(srgb_to_linear(texture(tex, uv + vec2( d,  d) * texelSize).rgb));
    sum += L; sumSq += L * L;
    L = get_luma(srgb_to_linear(texture(tex, uv + vec2(-d, -d) * texelSize).rgb));
    sum += L; sumSq += L * L;
    L = get_luma(srgb_to_linear(texture(tex, uv + vec2( d, -d) * texelSize).rgb));
    sum += L; sumSq += L * L;
    L = get_luma(srgb_to_linear(texture(tex, uv + vec2(-d,  d) * texelSize).rgb));
    sum += L; sumSq += L * L;
    // Inner ring at half radius for mid-frequency detail
    float h = r * 0.5;
    L = get_luma(srgb_to_linear(texture(tex, uv + vec2( h, 0.0) * texelSize).rgb));
    sum += L; sumSq += L * L;
    L = get_luma(srgb_to_linear(texture(tex, uv + vec2(-h, 0.0) * texelSize).rgb));
    sum += L; sumSq += L * L;
    L = get_luma(srgb_to_linear(texture(tex, uv + vec2(0.0,  h) * texelSize).rgb));
    sum += L; sumSq += L * L;
    L = get_luma(srgb_to_linear(texture(tex, uv + vec2(0.0, -h) * texelSize).rgb));
    sum += L; sumSq += L * L;

    // Variance = E[X^2] - E[X]^2
    float mean = sum / 13.0;
    float variance = max(sumSq / 13.0 - mean * mean, 0.0);
    // Standard deviation as focus measure
    float stddev = sqrt(variance);

    float t = focusAmount / 100.0;
    float strength = t * t;
    // Threshold: higher focusAmount demands higher local variance to pass
    float threshold = 0.005 + strength * 0.04;
    float gain = 6.0 + strength * 20.0;
    float gate = clamp((stddev - threshold) * gain, 0.0, 1.0);
    gate = gate * gate * (3.0 - 2.0 * gate);
    return mix(1.0, gate, strength);
}

// --- Real-time Non-Local Means (NLM) Noise Reduction ---
// Uses 3x3 patches within a 7x7 search window for high-fidelity denoising.
vec3 apply_gpu_denoise(vec3 center_color, vec2 texCoord, sampler2D tex, float amount) {
    if (amount <= 0.0) return center_color;

    float h = 0.01 + (amount / 100.0) * 0.1; 
    float h2 = h * h;
    
    vec3 accum_color = vec3(0.0);
    float accum_weight = 0.0;
    
    vec2 texelSize = 1.0 / ubuf.sourceSize;

    // We pre-sample the center patch 3x3 to avoid redundant texture lookups
    vec3 center_patch[9];
    for (int py = -1; py <= 1; py++) {
        for (int px = -1; px <= 1; px++) {
            center_patch[(py+1)*3 + (px+1)] = srgb_to_linear(texture(tex, texCoord + vec2(float(px), float(py)) * texelSize).rgb);
        }
    }

    for (int dy = -3; dy <= 3; dy++) {
        for (int dx = -3; dx <= 3; dx++) {
            vec2 offset = vec2(float(dx), float(dy));
            vec2 sampleCoord = texCoord + offset * texelSize;
            
            // Compute Patch SSD (3x3)
            float patch_ssd = 0.0;
            for (int py = -1; py <= 1; py++) {
                for (int px = -1; px <= 1; px++) {
                    vec3 s = srgb_to_linear(texture(tex, sampleCoord + vec2(float(px), float(py)) * texelSize).rgb);
                    vec3 c = center_patch[(py+1)*3 + (px+1)];
                    vec3 diff = s - c;
                    patch_ssd += dot(diff, diff);
                }
            }
            
            // Weight = exp(-MSE / h^2)
            float weight = exp(-(patch_ssd / 9.0) / h2);
            
            // Spatial dampening (Gaussian)
            float spatial_w = exp(-dot(offset, offset) / 16.0);
            weight *= spatial_w;

            accum_color += center_patch[4] * weight; // Use the center pixel of the sample patch
            accum_weight += weight;
        }
    }
    
    return accum_color / max(accum_weight, 0.0001);
}

vec3 apply_white_balance(vec3 color, float temp, float tnt) {
    vec3 temp_mult = vec3(1.0 + temp * 0.2, 1.0 + temp * 0.05, 1.0 - temp * 0.2);
    vec3 tint_mult = vec3(1.0 + tnt * 0.25, 1.0 - tnt * 0.25, 1.0 + tnt * 0.25);
    return color * temp_mult * tint_mult;
}

vec3 sample_source_linear(vec2 uv) {
    return srgb_to_linear(texture(source, uv).rgb);
}

vec3 photon001_gaussian_sigma1_axis(vec2 uv, vec2 texelSize, vec2 axis) {
    vec2 stepUv = axis * texelSize;
    vec3 b = sample_source_linear(uv) * 0.39894347;
    b += 0.29596257 * (sample_source_linear(uv + stepUv * 1.18242552) + sample_source_linear(uv - stepUv * 1.18242552));
    b += 0.00456569 * (sample_source_linear(uv + stepUv * 3.02931223) + sample_source_linear(uv - stepUv * 3.02931223));
    return b;
}

vec3 photon001_gaussian_sigma35_axis(vec2 uv, vec2 texelSize, vec2 axis) {
    vec2 stepUv = axis * texelSize;
    vec3 b = sample_source_linear(uv) * 0.11398719;
    b += 0.20624514 * (sample_source_linear(uv + stepUv * 1.46942595) + sample_source_linear(uv - stepUv * 1.46942595));
    b += 0.13826867 * (sample_source_linear(uv + stepUv * 3.42905340) + sample_source_linear(uv - stepUv * 3.42905340));
    b += 0.06731104 * (sample_source_linear(uv + stepUv * 5.38960340) + sample_source_linear(uv - stepUv * 5.38960340));
    b += 0.02378969 * (sample_source_linear(uv + stepUv * 7.35154728) + sample_source_linear(uv - stepUv * 7.35154728));
    b += 0.00610264 * (sample_source_linear(uv + stepUv * 9.31528835) + sample_source_linear(uv - stepUv * 9.31528835));
    b += 0.00113588 * (sample_source_linear(uv + stepUv * 11.28114775) + sample_source_linear(uv - stepUv * 11.28114775));
    b += 0.00015335 * (sample_source_linear(uv + stepUv * 13.24935770) + sample_source_linear(uv - stepUv * 13.24935770));
    return b;
}

vec3 compute_fine_blur(vec2 uv, vec2 texelSize) {
    vec3 horizontal = photon001_gaussian_sigma1_axis(uv, texelSize, vec2(1.0, 0.0));
    vec3 vertical = photon001_gaussian_sigma1_axis(uv, texelSize, vec2(0.0, 1.0));
    return 0.5 * (horizontal + vertical);
}

vec3 compute_coarse_blur(vec2 uv, vec2 texelSize) {
    vec3 horizontal = photon001_gaussian_sigma35_axis(uv, texelSize, vec2(1.0, 0.0));
    vec3 vertical = photon001_gaussian_sigma35_axis(uv, texelSize, vec2(0.0, 1.0));
    return 0.5 * (horizontal + vertical);
}

// --- Local Contrast, Clarity, Dehaze & Centre (Ported from RapidRAW) ---

vec3 apply_local_contrast(vec3 color_linear, vec3 blurred_linear, float amount, int mode) {
    if (amount == 0.0) return color_linear;

    // Doubling the action for Sharpening, Clarity and Structure
    float effective_amount = amount * 2.0;
    if (mode == 0) effective_amount = amount * 10.0; // 5x stronger sharpening

    float center_luma = get_luma(color_linear);
    float shadow_protection = smoothstep(0.0, 0.05, center_luma);
    float highlight_protection = 1.0 - smoothstep(0.85, 1.0, center_luma);
    float midtone_mask = shadow_protection * highlight_protection;
    
    if (midtone_mask < 0.001) return color_linear;

    float blurred_luma = get_luma(blurred_linear);
    float safe_center_luma = max(center_luma, 0.0001);
    float safe_blurred_luma = max(blurred_luma, 0.0001);

    vec3 final_color;
    if (effective_amount < 0.0) {
        vec3 blurred_projected = color_linear * (safe_blurred_luma / safe_center_luma);
        float blur_amt = -effective_amount;
        if (mode == 0) blur_amt *= 0.5; // Sharpening mode
        final_color = mix(color_linear, blurred_projected, blur_amt);
    } else {
        float log_ratio = log2(safe_center_luma / safe_blurred_luma);
        float adj_amount = effective_amount;
        if (mode == 0) { // Sharpening mode
            float edge_dampener = 1.0 - pow(clamp(abs(log_ratio) / 3.0, 0.0, 1.0), 0.5);
            adj_amount = effective_amount * edge_dampener;
        }
        final_color = color_linear * exp2(log_ratio * adj_amount);
    }
    
    return mix(color_linear, final_color, midtone_mask);
}

vec3 apply_dehaze(vec3 color, float amount) {
    if (amount == 0.0) return color;
    
    // Halving the effect again (total 0.25 of original)
    float effective_amount = amount * 0.25;
    
    vec3 atmospheric_light = vec3(0.95, 0.97, 1.0);
    if (effective_amount > 0.0) {
        float dark_channel = min(color.r, min(color.g, color.b));
        float t = 1.0 - effective_amount * (1.0 - dark_channel);
        vec3 recovered = (color - atmospheric_light) / max(t, 0.1) + atmospheric_light;
        vec3 result = mix(color, recovered, effective_amount);
        result = 0.5 + (result - 0.5) * (1.0 + effective_amount * 0.15);
        float luma = get_luma(result);
        return mix(vec3(luma), result, 1.0 + effective_amount * 0.1);
    } else {
        return mix(color, atmospheric_light, abs(effective_amount) * 0.7);
    }
}

vec3 apply_centre_effects(vec3 color, float amount, vec2 imgCoord) {
    if (amount == 0.0) return color;
    
    // Halving the effect
    float effective_amount = amount * 0.5;
    
    vec2 uv_centered = (imgCoord - 0.5) * 2.0;
    float d = length(uv_centered) * 0.5; // Simple circle for now
    float centre_mask = 1.0 - smoothstep(0.1, 0.7, d);

    // 1. Radial Exposure & Color
    float exposure_boost = centre_mask * (effective_amount / 100.0) * 0.5;
    color *= pow(2.0, exposure_boost);
    
    float vibrance_boost = centre_mask * (effective_amount / 100.0) * 0.4;
    float gray = get_luma(color);
    color = mix(vec3(gray), color, 1.0 + vibrance_boost);

    return color;
}

// --- HSL Core Math ---
vec3 rgb_to_hsv(vec3 c) {
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

vec3 hsv_to_rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

float get_hsl_influence(float hue, float center, float width) {
    float dist = min(abs(hue - center), 1.0 - abs(hue - center));
    float effectiveWidth = max(width * 1.25, 1e-6);
    float falloff = dist / (effectiveWidth * 0.5);
    return exp(-0.85 * falloff * falloff);
}

float compute_target_luma(float luma, float stops) {
    if (stops == 0.0) return luma;
    float target = luma * pow(2.0, stops);
    
    // Symmetrical soft-clipping for HSL to prevent "blowing out"
    // while maintaining a more linear response than the specialized shoulder function.
    if (target > 1.0) {
        float over = target - 1.0;
        target = 1.0 + over / (1.0 + over * 1.25);
    }
    return max(target, 0.0);
}

float compute_toe_target(float luma, float stops) {
    if (stops == 0.0) return luma;

    // Multiplicative base
    float target = luma * pow(2.0, stops);

    if (stops > 0.0) {
        // Soft Gamma Lift (Prevents Posterization)
        // A power curve is much smoother than a linear lift for deep darks.
        float liftGamma = 1.0 / (1.0 + stops * 0.5);
        float liftTarget = pow(max(luma, 1e-6), liftGamma);
        
        // Only apply the gamma lift to the bottom 15% of the range
        float toeMask = 1.0 - smoothstep(0.0, 0.15, luma);
        target = mix(target, liftTarget, toeMask * 0.4);
    }
    
    return max(target, 0.0);
}


vec3 apply_luma_target(vec3 color, float lumaIn, float targetLuma) {
    targetLuma      = max(targetLuma, 0.0);
    float safeLuma  = max(lumaIn, 1e-4);
    float lumaRatio = targetLuma / safeLuma;

    // --- Noise Floor Protection ---
    // Cap the lift ratio in deep blacks to prevent noise/posterization.
    // 1.0x cap at pure black, scaling up to 10.0x at 0.08 luma.
    float maxRatio = 1.0 + 9.0 * smoothstep(0.0, 0.08, lumaIn);
    float safeRatio = clamp(lumaRatio, 0.0, maxRatio);

    // --- Pure Multiplicative Adjustment ---
    // Scaling R, G, and B equally preserves Hue and Saturation
    return color * safeRatio;
}

const float PHOTON001_FLARE_LINEAR = 0.000244140625; // 2^-12
const float PHOTON001_FLARE_LOG = -12.0;
const float PHOTON001_EPS = 0.00000190734;

const mat3 RGB_TO_PROPHOTO = mat3(
    0.529285, 0.098394, 0.016823,
    0.330046, 0.873493, 0.117671,
    0.140669, 0.028113, 0.865506
);

const vec3 PROPHOTO_LUMA_WEIGHTS = vec3(0.25, 0.5, 0.25);

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

float photon001_decode_log_luma(float logLuma) {
    return max(exp2(logLuma) - PHOTON001_FLARE_LINEAR, PHOTON001_EPS);
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

float photon001_log_luma(vec3 c) {
    return photon001_encode_log_luma(photon001_working_luma_linear(c));
}

float photon001_tent_weight(float value, float center, float halfWidth) {
    return max(1.0 - abs(value - center) / max(halfWidth, PHOTON001_EPS), 0.0);
}

vec3 apply_photon001_tone_ranges(
    vec3 color,
    vec3 blurredFine,
    vec3 blurredCoarse,
    float highlightsAmt,
    float shadowsAmt,
    float whitesAmt,
    float blacksAmt,
    float clarityAmt,
    float sceneWhiteNorm
) {
    float srcGrayLinear = photon001_working_luma_linear(color);
    float srcGrayLog = photon001_encode_log_luma(srcGrayLinear);
    float blurFineLog = photon001_log_luma(blurredFine);
    float blurCoarseLog = photon001_log_luma(blurredCoarse);
    float toneMid = photon001_encode_log_luma(max(sceneWhiteNorm * 0.18, PHOTON001_EPS));

    // Approximation of tonal windows in log-space (black -> shadow -> mid -> highlight -> white).
    // Moving the center (toneMid - center) changes the tonal range of action,
    // while moving the width (second param), changes the overlap with other tones.
    float wBlacks = photon001_tent_weight(srcGrayLog, toneMid - 3.8, 1.8);
    float wShadows = photon001_tent_weight(srcGrayLog, toneMid - 1.9, 1.9);
    float wHighlights = photon001_tent_weight(srcGrayLog, toneMid + 1.0, 1.9);
    float wWhites = photon001_tent_weight(srcGrayLog, toneMid + 3.1, 2.2);

    // Stronger single-pass 2-scale local mask proxy (fine + coarse residuals).
    float maskFine = clamp(srcGrayLog - blurFineLog, -2.0, 2.0);
    float maskCoarse = clamp(blurFineLog - blurCoarseLog, -2.0, 2.0);
    float mask = clamp(maskFine * 0.70 + maskCoarse * 0.45, -2.5, 2.5);

    float partSwitch = step(srcGrayLog, toneMid);
    float compressedLow = toneMid + (srcGrayLog - toneMid) * 0.78;
    float compressedHigh = toneMid + (srcGrayLog - toneMid) * 0.58;
    float baseCompressed = mix(compressedHigh, compressedLow, partSwitch);

    float localContrastSignal = srcGrayLog + mask - baseCompressed;
    localContrastSignal *= max(clarityAmt, 0.0);
    localContrastSignal *= clamp(1.0 + 0.35 * (-highlightsAmt + shadowsAmt), 1.0, 2.0);
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

    // Analogous to ToneMapLimitShadowGain path (max +4 stops lift).
    deltaStops = min(deltaStops, 4.0);

    float targetLog = srcGrayLog + deltaStops;
    float targetLuma = photon001_decode_log_luma(targetLog);

    if (targetLuma > sceneWhiteNorm && deltaStops > 0.0) {
        float over = targetLuma - sceneWhiteNorm;
        float knee = max(sceneWhiteNorm * 0.7, PHOTON001_EPS);
        float compress = over / (1.0 + over / knee);
        targetLuma = sceneWhiteNorm + compress;
    }

    return apply_luma_target(color, srcGrayLinear, targetLuma);
}

vec3 apply_photon001_delta_stops(vec3 color, float deltaStops, float sceneWhiteNorm) {
    float srcGrayLinear = photon001_working_luma_linear(color);
    float srcGrayLog = photon001_encode_log_luma(srcGrayLinear);

    float targetLog = srcGrayLog + deltaStops;
    float targetLuma = photon001_decode_log_luma(targetLog);

    if (targetLuma > sceneWhiteNorm && deltaStops > 0.0) {
        float over = targetLuma - sceneWhiteNorm;
        float knee = max(sceneWhiteNorm * 0.7, PHOTON001_EPS);
        float compress = over / (1.0 + over / knee);
        targetLuma = sceneWhiteNorm + compress;
    }

    return apply_luma_target(color, srcGrayLinear, targetLuma);
}

float sample_tone_lut_channel(float value, int channel) {
    int idx = int(clamp(floor(clamp(value, 0.0, 1.0) * 65535.0 + 0.5), 0.0, 65535.0));
    int x = idx & 255;
    int y = channel * 256 + (idx >> 8);
    vec2 uv = vec2((float(x) + 0.5) / 256.0, (float(y) + 0.5) / 1024.0);
    vec4 packed = texture(toneLUT, uv);
    float hi = floor(packed.r * 255.0 + 0.5);
    float lo = floor(packed.g * 255.0 + 0.5);
    return (hi * 256.0 + lo) / 65535.0;
}

// --- Color Grading Math ---
vec3 apply_region_tint(vec3 color, float hue, float sat, float lum) {
    vec3 tint_rgb = hsv_to_rgb(vec3(hue / 360.0, sat / 100.0, 1.0));
    color = mix(color, color * tint_rgb, sat / 100.0);
    color *= (1.0 + (lum / 100.0));
    return color;
}

vec3 color_grade(vec3 color, float luma) {
    float balance = ubuf.cgBalance / 100.0; // -1 to 1
    float blending = ubuf.cgBlending / 100.0; // 0 to 1
    
    float base_shadow_crossover = 0.1;
    float base_highlight_crossover = 0.5;
    float balance_range = 0.5;
    
    float shadow_crossover = base_shadow_crossover + max(0.0, -balance) * balance_range;
    float highlight_crossover = base_highlight_crossover - max(0.0, balance) * balance_range;
    float feather = 0.2 * blending;
    
    float final_shadow_crossover = min(shadow_crossover, highlight_crossover - 0.01);
    
    float shadow_mask = 1.0 - smoothstep(final_shadow_crossover - feather, final_shadow_crossover + feather, luma);
    float highlight_mask = smoothstep(highlight_crossover - feather, highlight_crossover + feather, luma);
    float midtone_mask = max(0.0, 1.0 - shadow_mask - highlight_mask);
    
    // Apply tints using hsv_to_rgb for cinematic coloring
    vec3 c_s = apply_region_tint(color, ubuf.cgShadowsHue, ubuf.cgShadowsSaturation, ubuf.cgShadowsLuminance);
    vec3 c_m = apply_region_tint(color, ubuf.cgMidtonesHue, ubuf.cgMidtonesSaturation, ubuf.cgMidtonesLuminance);
    vec3 c_h = apply_region_tint(color, ubuf.cgHighlightsHue, ubuf.cgHighlightsSaturation, ubuf.cgHighlightsLuminance);
    
    return c_s * shadow_mask + c_m * midtone_mask + c_h * highlight_mask;
}

// --- AgX Tone Mapping (Ported from reference) ---
const float AGX_MIN_EV = -15.2;
const float AGX_MAX_EV = 8;

float agx_sigmoid(float x, float power) {
    return x / pow(1.0 + pow(x, power), 1.0 / power);
}

float agx_scaled_sigmoid(float x, float scale, float slope, float power, float tx, float ty) {
    return scale * agx_sigmoid(slope * (x - tx) / scale, power) + ty;
}

float agx_apply_curve_channel(float x) {
    const float TOE_TX = 0.6060606;
    const float TOE_TY = 0.43446;
    const float SLOPE = 2.3843;
    const float TOE_SCALE = -1.0359;
    const float TOE_POWER = 1.5;
    
    const float SH_TX = 0.6060606;
    const float SH_TY = 0.43446;
    const float SH_SCALE = 1.3475;
    const float SH_POWER = 1.5;
    
    const float INTERCEPT = -1.0112;

    if (x < TOE_TX) {
        return agx_scaled_sigmoid(x, TOE_SCALE, SLOPE, TOE_POWER, TOE_TX, TOE_TY);
    } else if (x <= SH_TX) {
        return SLOPE * x + INTERCEPT;
    } else {
        return agx_scaled_sigmoid(x, SH_SCALE, SLOPE, SH_POWER, SH_TX, SH_TY);
    }
}

vec3 aces_tonemap(vec3 color) {
    // Narkowicz 2015 ACES approximation
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    
    // Apply the polynomial curve
    color = (color * (a * color + b)) / (color * (c * color + d) + e);
    
    return clamp(color, 0.0, 1.0);
}

// --- DaVinci Tonemapping ---
float davinci_rolloff(float x, float a, float b) {
    return a * (x / (x + b));
}

vec3 davinci_tonemap(vec3 color, float adaptation) {

    float input_white = 16.0;
    float output_white = 1.0;
    // float adaptation = 12.0;

    if (input_white <= output_white) {
        return min(color, vec3(output_white));
    }

    float b = (input_white - (adaptation / 100.0) * (input_white / output_white))
            / ((input_white / output_white) - 1.0);
    float a = output_white / (input_white / (input_white + b));

    color = min(color, vec3(input_white));

    color.r = davinci_rolloff(color.r, a, b);
    color.g = davinci_rolloff(color.g, a, b);
    color.b = davinci_rolloff(color.b, a, b);

    return clamp(color, vec3(0.0), vec3(output_white));
}

vec3 agx_tonemap(vec3 color) {
    const mat3 AgX_Inset = mat3(
        0.856627153315983, 0.137318972929847, 0.11189821299995,
        0.098403403513892, 0.826432433318306, 0.083220990554183,
        0.044970474436218, 0.036252551516413, 0.804880733512495
    );
    const mat3 AgX_Out = mat3(
        1.19687900512017, -0.098420828164883, -0.099837617823979,
        -0.052896851757456, 1.15190312990417, -0.098961176844843,
        -0.05514323170335, -0.053382142916275, 1.19837733946797
    );
    
    color = max(color, 1e-10);
    color = AgX_Inset * color;
    
    // Log encoding relative to 0.18
    vec3 x_rel = color / 0.18;
    vec3 log_encoded = (log2(x_rel) - AGX_MIN_EV) / (AGX_MAX_EV - AGX_MIN_EV);
    vec3 mapped = clamp(log_encoded, 0.0, 1.0);
    
    vec3 curved;
    curved.r = agx_apply_curve_channel(mapped.r);
    curved.g = agx_apply_curve_channel(mapped.g);
    curved.b = agx_apply_curve_channel(mapped.b);
    
    // AgX ends with bringing it back to linear space
    vec3 tonemapped_linear = pow(max(curved, 0.0), vec3(2.4));
    
    return AgX_Out * tonemapped_linear;
}

// --- Film Grain & Noise ---
float hash(vec2 p) {
    vec3 p3  = fract(vec3(p.xyx) * .1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float gradient_noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float ga = hash(i + vec2(0.0, 0.0));
    float gb = hash(i + vec2(1.0, 0.0));
    float gc = hash(i + vec2(0.0, 1.0));
    float gd = hash(i + vec2(1.0, 1.0));
    return mix(mix(ga, gb, u.x), mix(gc, gd, u.x), u.y) * 2.0 - 1.0;
}

float dither(vec2 p) {
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453) - 0.5;
}

void main()
{
    vec2 pixelPos = qt_TexCoord0 * ubuf.viewportSize;
    if (pixelPos.x < ubuf.imageRect.x || pixelPos.x > ubuf.imageRect.x + ubuf.imageRect.z ||
        pixelPos.y < ubuf.imageRect.y || pixelPos.y > ubuf.imageRect.y + ubuf.imageRect.w) {
        fragColor = ubuf.backgroundColor;
        return;
    }
    vec2 imgCoord = (pixelPos - ubuf.imageRect.xy) / ubuf.imageRect.zw;

    vec4 tex = texture(source, qt_TexCoord0);

    if (ubuf.isPreview > 0.5) {
        fragColor = vec4(tex.rgb, tex.a) * ubuf.qt_Opacity;
        return;
    }

    // Before/After: show unprocessed RAW with only sRGB→linear→sRGB
    if (ubuf.showOriginal > 0.5) {
        fragColor = vec4(tex.rgb, tex.a) * ubuf.qt_Opacity;
        return;
    }

    vec3 color = srgb_to_linear(tex.rgb);
    
    // 0. Noise Reduction (Real-time GPU pass)
    color = apply_gpu_denoise(color, qt_TexCoord0, source, ubuf.denoiseAmount);

    // --- Approximated Blur for Local Contrast (Clarity, Structure, Sharpness) ---
    // Stronger two-scale blur with more taps to better emulate a single-pass local pyramid mask.
    vec2 texelSize = 1.0 / ubuf.sourceSize;
    vec3 blurredFine = compute_fine_blur(qt_TexCoord0, texelSize);
    vec3 blurredCoarse = compute_coarse_blur(qt_TexCoord0, texelSize);
    vec3 blurred = mix(blurredFine, blurredCoarse, 0.35);

    // Compute edge mask with feathering, then gate by focus detection
    float edgeMask = compute_edge_mask(source, qt_TexCoord0, texelSize, ubuf.sharpenMask, ubuf.maskFeather);
    float focusGate = compute_focus_gate(source, qt_TexCoord0, texelSize, ubuf.focusDetect);
    float finalMask = edgeMask * focusGate;

    // Alt+drag mask preview: show combined mask as grayscale and return early
    if (ubuf.showSharpenMask > 0.5 && (ubuf.sharpenMask > 0.0 || ubuf.focusDetect > 0.0)) {
        fragColor = vec4(vec3(finalMask), 1.0);
        return;
    }

    vec3 preSharp = color;
    color = apply_local_contrast(color, blurred, ubuf.sharpness / 100.0, 0);
    // Blend sharpened vs original using combined mask
    color = mix(preSharp, color, finalMask);
    
    // Apply Structure (Mode 1, but with different scaling/interpretation if needed)
    color = apply_local_contrast(color, blurred, ubuf.structure / 100.0, 1);

    // Apply Dehaze
    color = apply_dehaze(color, ubuf.dehaze / 100.0);

    // Apply Centre Effects
    color = apply_centre_effects(color, ubuf.centre, imgCoord);

    // 1. White Balance
    color = apply_white_balance(color, ubuf.temperature / 100.0, ubuf.tint / 100.0);

    // 2. Exposure
    float exposure = pow(2.0, ubuf.exposure);

    color *= exposure;
    float luma = get_luma(max(color, 0.0));
    if (luma > ubuf.sceneWhite && ubuf.exposure > 0.0) {
        float over     = luma - ubuf.sceneWhite;
        // The higher the shoulder, the less the highlights get compressed
        float knee     = ubuf.sceneWhite * 0.7;          // shoulder width
        float compress = over / (1.0 + over / knee);     // Reinhard-style on the excess
        float targetL  = ubuf.sceneWhite + compress;
        color = apply_luma_target(color, luma, targetL);
    }
    float sceneWhiteNorm = max(ubuf.sceneWhite * exposure, 1e-4);
    float deltaStops = texture(photon001Delta, qt_TexCoord0).x;
    color = apply_photon001_delta_stops(color, deltaStops, sceneWhiteNorm);
    //color = davinci_tonemap(color, ubuf.adaptation);
    
    // 3. Contrast
    color = max(vec3(0.0), color);
    color = pow(color, vec3(ubuf.contrast));

    // --- HSL PANEL ---
    vec3 hsv = rgb_to_hsv(color);
    float hue = hsv.x;
    float sat = hsv.y;
    float hue_shift = 0.0;
    float sat_mult = 0.0;
    float lum_adj = 0.0;
    float influence_sum = 0.0;

    float centers[8] = { 358.0/360.0, 25.0/360.0, 60.0/360.0, 115.0/360.0, 180.0/360.0, 225.0/360.0, 280.0/360.0, 330.0/360.0 };
    float widths[8] = { 35.0/360.0, 45.0/360.0, 40.0/360.0, 90.0/360.0, 60.0/360.0, 60.0/360.0, 55.0/360.0, 50.0/360.0 };
    float h_adjs[8] = { ubuf.hslRedHue, ubuf.hslOrangeHue, ubuf.hslYellowHue, ubuf.hslGreenHue, ubuf.hslAquaHue, ubuf.hslBlueHue, ubuf.hslPurpleHue, ubuf.hslMagentaHue };
    float s_adjs[8] = { ubuf.hslRedSaturation, ubuf.hslOrangeSaturation, ubuf.hslYellowSaturation, ubuf.hslGreenSaturation, ubuf.hslAquaSaturation, ubuf.hslBlueSaturation, ubuf.hslPurpleSaturation, ubuf.hslMagentaSaturation };
    float l_adjs[8] = { ubuf.hslRedLuminance, ubuf.hslOrangeLuminance, ubuf.hslYellowLuminance, ubuf.hslGreenLuminance, ubuf.hslAquaLuminance, ubuf.hslBlueLuminance, ubuf.hslPurpleLuminance, ubuf.hslMagentaLuminance };

    for (int i = 0; i < 8; i++) {
        float influence = get_hsl_influence(hue, centers[i], widths[i]);
        influence_sum += influence;
        hue_shift += (h_adjs[i] / 100.0) * 0.1 * influence;
        sat_mult += (s_adjs[i] / 100.0) * influence;
        lum_adj += (l_adjs[i] / 100.0) * influence;
    }

    float norm = max(1.0, influence_sum);
    hue_shift /= norm;
    sat_mult /= norm;
    lum_adj /= norm;

    float chromaProtect = smoothstep(0.04, 0.22, sat);
    hue_shift *= chromaProtect;
    sat_mult = mix(sat_mult * 0.35, sat_mult, chromaProtect);
    lum_adj *= mix(0.4, 1.0, chromaProtect);

    hsv.x = fract(hsv.x + hue_shift);
    float satScale = 1.0 + clamp(sat_mult, -0.85, 1.25);
    hsv.y = clamp(hsv.y * satScale, 0.0, 1.0);
    color = hsv_to_rgb(hsv);
    float lumaAfterHueSat = get_luma(max(color, 0.0));
    float lumStops = clamp(lum_adj, -0.75, 0.75) * 0.70;
    float targetHslLuma = compute_target_luma(lumaAfterHueSat, lumStops);
    color = apply_luma_target(color, lumaAfterHueSat, targetHslLuma);

    // --- COLOR GRADING --- (Applied before global saturation/vibrance)
    color = color_grade(color, get_luma(max(color, 0.0)));

    // 6. Saturation & Vibrance (Global)
    float gray = get_luma(max(color, 0.0));
    color = mix(vec3(gray), color, 1.0 + (ubuf.saturation / 100.0));
    float max_color = max(color.r, max(color.g, color.b));
    float avg_color = (color.r + color.g + color.b) / 3.0;
    float amt = (max_color - avg_color) * (-ubuf.vibrance / 100.0) * 3.0;
    color = mix(color, vec3(max_color), amt);

    // 7. Tonemapping
    if (ubuf.tonemappingEnabled > 0.5) {
        color = agx_tonemap(color);
    }

    // 8. Tone Curve LUT (applied in linear, pre-sRGB)
    // 256×1024 texture: 4 stacked 256×256 planes (Luma, R, G, B),
    // each encoding 65536 entries packed as 16-bit RG bytes.
    if (ubuf.toneCurveActive > 0.5) {
        vec3 c = clamp(color, 0.0, 1.0);

        // Reference-style luma curve: remap channel min/max through the same
        // curve, then linearly reproject channel values between those bounds.
        float fMin = min(min(c.r, c.g), c.b);
        float fMax = max(max(c.r, c.g), c.b);
        float nMin = sample_tone_lut_channel(fMin, 0);
        float nMax = sample_tone_lut_channel(fMax, 0);
        float scale = (nMax - nMin) / (fMax - fMin + 0.00001);
        c = (c - vec3(fMin)) * scale + vec3(nMin);

        // Apply RGB channel curves after luma remap.
        c.r = sample_tone_lut_channel(clamp(c.r, 0.0, 1.0), 1);
        c.g = sample_tone_lut_channel(clamp(c.g, 0.0, 1.0), 2);
        c.b = sample_tone_lut_channel(clamp(c.b, 0.0, 1.0), 3);

        // Keep highlight data above 1.0 from the unclamped working color.
        color = mix(c, color, step(1.001, max(color.r, max(color.g, color.b))));
    }

    vec3 final_rgb = linear_to_srgb(color);

    // 8. Creative: Film Grain
    if (ubuf.grainAmount > 0.0) {
        vec2 grainCoord = imgCoord * ubuf.imageRect.zw; 
        float grain_frequency = (1.0 / max(ubuf.grainSize, 0.1));
        float noise_base = gradient_noise(grainCoord * grain_frequency);
        float noise_rough = gradient_noise(grainCoord * grain_frequency * 2.0 + vec2(5.2, 1.3));
        float noise = mix(noise_base, noise_rough, ubuf.grainRoughness);
        float luma_mask = smoothstep(0.05, 0.25, get_luma(final_rgb)) * (1.0 - smoothstep(0.5, 0.9, get_luma(final_rgb)));
        final_rgb += noise * (ubuf.grainAmount / 100.0) * 0.15 * luma_mask;
    }

    // 9. Creative: Vignette
    if (ubuf.vignetteAmount != 0.0) {
        vec2 uv_centered = (imgCoord - 0.5) * 2.0;
        float v_round = 1.0 - (ubuf.vignetteRoundness / 100.0);
        vec2 uv_round = sign(uv_centered) * pow(abs(uv_centered), vec2(v_round));
        float d = length(uv_round) * 0.5;
        float v_mid = ubuf.vignetteMidpoint / 100.0;
        float v_feather = max(ubuf.vignetteFeather / 100.0, 0.01);
        float vignette_mask = smoothstep(v_mid - v_feather, v_mid + v_feather, d);
        if (ubuf.vignetteAmount < 0.0) {
            final_rgb *= (1.0 + (ubuf.vignetteAmount / 100.0) * vignette_mask);
        } else {
            final_rgb = mix(final_rgb, vec3(1.0), (ubuf.vignetteAmount / 100.0) * vignette_mask);
        }
    }

    float dither_amount = 1.0 / 255.0;
    final_rgb += dither(pixelPos) * dither_amount;

    fragColor = vec4(clamp(final_rgb, 0.0, 1.0), tex.a) * ubuf.qt_Opacity;
}
