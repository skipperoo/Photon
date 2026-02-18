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
    float tonemappingEnabled; // bool passed as float
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
// Ported from RapidRAW / AgX Spec

const float AGX_MIN_EV = -12.47393;
const float AGX_MAX_EV = 4.026069;

vec3 agx_sigmoid(vec3 x) {
    vec3 x2 = x * x;
    vec3 x4 = x2 * x2;
    return ( 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.86 * x2 * x + 0.429 * x2 + 0.115 * x ) / 1.0;
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

void main()
{
    vec4 tex = texture(source, qt_TexCoord0);
    vec3 color = srgb_to_linear(tex.rgb);
    
    // 1. White Balance
    color = apply_white_balance(color, ubuf.temperature / 100.0, ubuf.tint / 100.0);

    // 2. Exposure
    color *= pow(2.0, ubuf.exposure);
    
    // 3. Contrast
    color = (color - 0.5) * ubuf.contrast + 0.5;
    
    // 4. Highlights / Shadows / Whites / Blacks
    float luma = get_luma(max(color, 0.0));
    float hWeight = smoothstep(0.5, 1.0, luma);
    color += hWeight * (ubuf.highlights / 100.0) * color;
    float sWeight = 1.0 - smoothstep(0.0, 0.5, luma);
    color += sWeight * (ubuf.shadows / 100.0) * color;
    color += (ubuf.whites / 100.0) * smoothstep(0.8, 1.0, luma);
    color += (ubuf.blacks / 100.0) * (1.0 - smoothstep(0.0, 0.2, luma));

    // 5. Saturation & Vibrance
    float gray = get_luma(max(color, 0.0));
    color = mix(vec3(gray), color, 1.0 + (ubuf.saturation / 100.0));
    float max_color = max(color.r, max(color.g, color.b));
    float avg_color = (color.r + color.g + color.b) / 3.0;
    float amt = (max_color - avg_color) * (-ubuf.vibrance / 100.0) * 3.0;
    color = mix(color, vec3(max_color), amt);

    // 6. Tonemapping
    if (ubuf.tonemappingEnabled > 0.5) {
        color = agx_tonemap(color);
    }

    fragColor = vec4(linear_to_srgb(color), tex.a) * ubuf.qt_Opacity;
}
