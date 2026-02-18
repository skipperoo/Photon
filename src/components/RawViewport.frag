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
    float tonemappingEnabled;
    float grainAmount;
    float grainSize;
    float grainRoughness;
    float vignetteAmount;
    float vignetteMidpoint;
    float vignetteRoundness;
    float vignetteFeather;
    vec4 imageRect; // x, y, width, height
    vec2 viewportSize;
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

vec3 apply_white_balance(vec3 color, float temp, float tnt) {
    vec3 temp_mult = vec3(1.0 + temp * 0.2, 1.0 + temp * 0.05, 1.0 - temp * 0.2);
    vec3 tint_mult = vec3(1.0 + tnt * 0.25, 1.0 - tnt * 0.25, 1.0 + tnt * 0.25);
    return color * temp_mult * tint_mult;
}

// --- AgX Tone Mapping ---
const float AGX_MIN_EV = -12.47393;
const float AGX_MAX_EV = 4.026069;

vec3 agx_sigmoid(vec3 x) {
    vec3 x2 = x * x;
    vec3 x4 = x2 * x2;
    return ( 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.86 * x2 * x + 0.429 * x2 + 0.115 * x );
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
    color = clamp((log2(color) - AGX_MIN_EV) / (AGX_MAX_EV - AGX_MIN_EV), 0.0, 1.0);
    color = agx_sigmoid(color);
    color = AgX_Out * color;
    return color;
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

void main()
{
    // Pixel coordinates in viewport pixels
    vec2 pixelPos = qt_TexCoord0 * ubuf.viewportSize;
    
    // Check if we are inside the image area
    if (pixelPos.x < ubuf.imageRect.x || pixelPos.x > ubuf.imageRect.x + ubuf.imageRect.z ||
        pixelPos.y < ubuf.imageRect.y || pixelPos.y > ubuf.imageRect.y + ubuf.imageRect.w) {
        fragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    // Normalized coordinates relative to the image (0.0 to 1.0)
    vec2 imgCoord = (pixelPos - ubuf.imageRect.xy) / ubuf.imageRect.zw;

    vec4 tex = texture(source, qt_TexCoord0);
    vec3 color = srgb_to_linear(tex.rgb);
    
    // 1. White Balance
    color = apply_white_balance(color, ubuf.temperature / 100.0, ubuf.tint / 100.0);

    // 2. Exposure
    color *= pow(2.0, ubuf.exposure);
    
    // 3. Contrast (Linear contrast)
    color = max(vec3(0.0), color);
    color = pow(color, vec3(ubuf.contrast));
    
    // 4. Whites & Blacks (Broad range)
    if (ubuf.whites != 0.0) {
        float white_level = 1.0 - (ubuf.whites / 100.0) * 0.5;
        color = color / max(white_level, 0.01);
    }
    
    if (ubuf.blacks != 0.0) {
        float luma_bl = get_luma(max(color, 0.0));
        float black_mask = 1.0 - smoothstep(0.0, 0.3, luma_bl);
        color = mix(color, color * pow(2.0, (ubuf.blacks / 100.0) * 1.5), black_mask);
    }

    // 5. Highlights & Shadows (Broad range)
    float luma = get_luma(max(color, 0.0));
    
    // Shadows
    if (ubuf.shadows != 0.0) {
        float shadow_mask = pow(1.0 - smoothstep(0.0, 0.5, luma), 2.0);
        color = mix(color, color * pow(2.0, (ubuf.shadows / 100.0) * 1.5), shadow_mask);
    }

    // Highlights
    if (ubuf.highlights != 0.0) {
        float highlight_mask = smoothstep(0.4, 1.0, tanh(luma * 1.5));
        float h_adj = ubuf.highlights / 100.0;
        if (h_adj < 0.0) {
            float gamma = 1.0 - h_adj * 1.5;
            vec3 h_color = pow(max(color, 0.0001), vec3(gamma));
            color = mix(color, h_color, highlight_mask);
        } else {
            color = mix(color, color * pow(2.0, h_adj * 1.5), highlight_mask);
        }
    }

    // 6. Saturation & Vibrance
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

    vec3 final_rgb = linear_to_srgb(color);

    // 8. Creative: Film Grain (Only on image area)
    if (ubuf.grainAmount > 0.0) {
        vec2 grainCoord = imgCoord * ubuf.imageRect.zw; // Use image pixels for grain scale
        float grain_frequency = (1.0 / max(ubuf.grainSize, 0.1));
        float noise = gradient_noise(grainCoord * grain_frequency);
        float luma_mask = smoothstep(0.05, 0.25, get_luma(final_rgb)) * (1.0 - smoothstep(0.5, 0.9, get_luma(final_rgb)));
        final_rgb += noise * (ubuf.grainAmount / 100.0) * 0.15 * luma_mask;
    }

    // 9. Creative: Vignette (Only on image area)
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

    fragColor = vec4(clamp(final_rgb, 0.0, 1.0), tex.a) * ubuf.qt_Opacity;
}
