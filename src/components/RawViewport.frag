#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D source;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float exposure;
    float contrast;
    float highlights;
    float shadows;
    float whites;
    float blacks;
    float vibrance;
    float saturation;
    float temperature;
    float tint;
    float tonemappingMode;
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

// --- Local Contrast, Clarity, Dehaze & Centre (Ported from RapidRAW) ---

vec3 apply_local_contrast(vec3 color_linear, vec3 blurred_linear, float amount, int mode) {
    if (amount == 0.0) return color_linear;

    // Doubling the action for Sharpening, Clarity and Structure
    float effective_amount = amount * 2.0;

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
            adj_amount = effective_amount * edge_dampener * 0.8;
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
    float falloff = dist / (width * 0.5);
    return exp(-1.5 * falloff * falloff);
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

// --- DaVinci Tonemapping ---
float davinci_rolloff(float x, float a, float b) {
    return a * (x / (x + b));
}

vec3 davinci_tonemap(vec3 color, float exposure_val) {
    color *= pow(2.0, exposure_val);

    float input_white = 16.0;
    float output_white = 1.0;
    float adaptation = 9.0;

    float b = (input_white - (adaptation / 100.0) * (input_white / output_white))
            / ((input_white / output_white) - 1.0);
    float a = output_white / (input_white / (input_white + b));

    color = min(color, vec3(input_white));

    color.r = davinci_rolloff(color.r, a, b);
    color.g = davinci_rolloff(color.g, a, b);
    color.b = davinci_rolloff(color.b, a, b);

    return clamp(color, vec3(0.0), vec3(output_white));
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

    vec3 color = srgb_to_linear(tex.rgb);
    
    // 0. Noise Reduction (Real-time GPU pass)
    color = apply_gpu_denoise(color, qt_TexCoord0, source, ubuf.denoiseAmount);

    // --- Approximated Blur for Local Contrast (Clarity, Structure, Sharpness) ---
    // We use a multi-tap sample to approximate a blurred version of the current pixel.
    vec2 texelSize = 1.0 / ubuf.sourceSize;
    vec3 blurred = color * 0.25;
    blurred += srgb_to_linear(texture(source, qt_TexCoord0 + vec2(1.5, 1.5) * texelSize).rgb) * 0.1875;
    blurred += srgb_to_linear(texture(source, qt_TexCoord0 + vec2(-1.5, -1.5) * texelSize).rgb) * 0.1875;
    blurred += srgb_to_linear(texture(source, qt_TexCoord0 + vec2(1.5, -1.5) * texelSize).rgb) * 0.1875;
    blurred += srgb_to_linear(texture(source, qt_TexCoord0 + vec2(-1.5, 1.5) * texelSize).rgb) * 0.1875;

    // Apply Sharpening (Mode 0)
    color = apply_local_contrast(color, blurred, ubuf.sharpness / 100.0, 0);
    
    // Apply Clarity (Mode 1)
    color = apply_local_contrast(color, blurred, ubuf.clarity / 100.0, 1);
    
    // Apply Structure (Mode 1, but with different scaling/interpretation if needed)
    color = apply_local_contrast(color, blurred, ubuf.structure / 100.0, 1);

    // Apply Dehaze
    color = apply_dehaze(color, ubuf.dehaze / 100.0);

    // Apply Centre Effects
    color = apply_centre_effects(color, ubuf.centre, imgCoord);

    // 1. White Balance
    color = apply_white_balance(color, ubuf.temperature / 100.0, ubuf.tint / 100.0);

    // 2. Exposure (DaVinci mode integrates exposure into its rolloff)
    if (ubuf.tonemappingMode < 1.5) {
        color *= pow(2.0, ubuf.exposure);
    }
    
    // 3. Contrast
    color = max(vec3(0.0), color);
    color = pow(color, vec3(ubuf.contrast));
    
    // 4. Whites & Blacks
    if (ubuf.whites != 0.0) {
        float white_level = 1.0 - (ubuf.whites / 100.0) * 0.5;
        color = color / max(white_level, 0.01);
    }
    if (ubuf.blacks != 0.0) {
        float luma_bl = get_luma(max(color, 0.0));
        float black_mask = 1.0 - smoothstep(0.0, 0.3, luma_bl);
        color = mix(color, color * pow(2.0, (ubuf.blacks / 100.0) * 1.5), black_mask);
    }

    // 5. Highlights & Shadows
    float luma = get_luma(max(color, 0.0));
    
    // Shadow Mask (inspired by crossover logic at WGSL line 615)
    if (ubuf.shadows != 0.0) {
        float shadow_mask = 1.0 - smoothstep(0.0, 0.25, luma);
        float adjustment = (ubuf.shadows / 100.0) * 1.5;
        color *= mix(1.0, pow(2.0, adjustment), shadow_mask);
    }
    
    // Highlight Mask (inspired by crossover logic at WGSL line 615 + specialized reduction)
    if (ubuf.highlights != 0.0) {
        float highlight_mask = smoothstep(0.3, 0.95, tanh(luma * 1.5));
        float h_adj = ubuf.highlights / 100.0;
        
        vec3 h_color;
        if (h_adj < 0.0) {
            // Advanced Reduction: Gamma for normal range, Compression for over-exposed
            float new_luma;
            if (luma <= 1.0) {
                float gamma = 1.0 - h_adj * 1.75;
                new_luma = pow(max(luma, 0.0001), gamma);
            } else {
                float luma_excess = luma - 1.0;
                float compression_strength = -h_adj * 6.0;
                float compressed_excess = luma_excess / (1.0 + luma_excess * compression_strength);
                new_luma = 1.0 + compressed_excess;
            }
            
            h_color = color * (new_luma / max(luma, 0.0001));
            
            // Desaturate extremely bright highlights to avoid color shifts
            float desat = smoothstep(1.0, 5.0, luma);
            h_color = mix(h_color, vec3(new_luma), desat);
        } else {
            h_color = color * pow(2.0, h_adj * 1.5);
        }
        
        color = mix(color, h_color, highlight_mask);
    }

    // --- HSL PANEL ---
    vec3 hsv = rgb_to_hsv(color);
    float hue = hsv.x;
    float sat = hsv.y;
    float hue_shift = 0.0;
    float sat_mult = 0.0;
    float lum_adj = 0.0;

    float centers[8] = { 358.0/360.0, 25.0/360.0, 60.0/360.0, 115.0/360.0, 180.0/360.0, 225.0/360.0, 280.0/360.0, 330.0/360.0 };
    float widths[8] = { 35.0/360.0, 45.0/360.0, 40.0/360.0, 90.0/360.0, 60.0/360.0, 60.0/360.0, 55.0/360.0, 50.0/360.0 };
    float h_adjs[8] = { ubuf.hslRedHue, ubuf.hslOrangeHue, ubuf.hslYellowHue, ubuf.hslGreenHue, ubuf.hslAquaHue, ubuf.hslBlueHue, ubuf.hslPurpleHue, ubuf.hslMagentaHue };
    float s_adjs[8] = { ubuf.hslRedSaturation, ubuf.hslOrangeSaturation, ubuf.hslYellowSaturation, ubuf.hslGreenSaturation, ubuf.hslAquaSaturation, ubuf.hslBlueSaturation, ubuf.hslPurpleSaturation, ubuf.hslMagentaSaturation };
    float l_adjs[8] = { ubuf.hslRedLuminance, ubuf.hslOrangeLuminance, ubuf.hslYellowLuminance, ubuf.hslGreenLuminance, ubuf.hslAquaLuminance, ubuf.hslBlueLuminance, ubuf.hslPurpleLuminance, ubuf.hslMagentaLuminance };

    for (int i = 0; i < 8; i++) {
        float influence = get_hsl_influence(hue, centers[i], widths[i]);
        hue_shift += (h_adjs[i] / 100.0) * 0.1 * influence;
        sat_mult += (s_adjs[i] / 100.0) * influence;
        lum_adj += (l_adjs[i] / 100.0) * influence;
    }

    hsv.x = fract(hsv.x + hue_shift);
    hsv.y = clamp(hsv.y * (1.0 + sat_mult), 0.0, 1.0);
    color = hsv_to_rgb(hsv);
    color *= (1.0 + lum_adj);

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
    if (ubuf.tonemappingMode > 1.5) {
        color = davinci_tonemap(color, ubuf.exposure);
    } else if (ubuf.tonemappingMode > 0.5) {
        color = agx_tonemap(color);
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
