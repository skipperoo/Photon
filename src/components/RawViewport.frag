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
    // Simplified kelvin-like transform
    vec3 temp_mult = vec3(1.0 + temp * 0.2, 1.0 + temp * 0.05, 1.0 - temp * 0.2);
    vec3 tint_mult = vec3(1.0 + tnt * 0.25, 1.0 - tnt * 0.25, 1.0 + tnt * 0.25);
    return color * temp_mult * tint_mult;
}

void main()
{
    vec4 tex = texture(source, qt_TexCoord0);
    
    // Convert to Linear space for math
    vec3 color = srgb_to_linear(tex.rgb);
    
    // 1. White Balance
    color = apply_white_balance(color, ubuf.temperature / 100.0, ubuf.tint / 100.0);

    // 2. Exposure (Linear scale)
    color *= pow(2.0, ubuf.exposure);
    
    // 3. Contrast (Linear-ish contrast)
    color = (color - 0.5) * ubuf.contrast + 0.5;
    
    // 4. Highlights / Shadows / Whites / Blacks
    float luma = get_luma(max(color, 0.0));
    
    // Highlights
    float hWeight = smoothstep(0.5, 1.0, luma);
    color += hWeight * (ubuf.highlights / 100.0) * color;
    
    // Shadows
    float sWeight = 1.0 - smoothstep(0.0, 0.5, luma);
    color += sWeight * (ubuf.shadows / 100.0) * color;
    
    // Whites
    color += (ubuf.whites / 100.0) * smoothstep(0.8, 1.0, luma);
    
    // Blacks
    color += (ubuf.blacks / 100.0) * (1.0 - smoothstep(0.0, 0.2, luma));

    // 5. Saturation
    float gray = get_luma(color);
    color = mix(vec3(gray), color, 1.0 + (ubuf.saturation / 100.0));
    
    // 6. Vibrance (Selective saturation)
    float max_color = max(color.r, max(color.g, color.b));
    float avg_color = (color.r + color.g + color.b) / 3.0;
    float amt = (max_color - avg_color) * (-ubuf.vibrance / 100.0) * 3.0;
    color = mix(color, vec3(max_color), amt);

    // Convert back to sRGB for display
    fragColor = vec4(linear_to_srgb(color), tex.a) * ubuf.qt_Opacity;
}
