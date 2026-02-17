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
} ubuf;

void main()
{
    vec4 color = texture(source, qt_TexCoord0);
    
    // 1. Exposure (EV scale: 2^exposure)
    color.rgb *= pow(2.0, ubuf.exposure);
    
    // 2. Contrast
    color.rgb = (color.rgb - 0.5) * ubuf.contrast + 0.5;
    
    // 3. Highlights / Shadows / Whites / Blacks
    float luma = dot(color.rgb, vec3(0.299, 0.587, 0.114));
    
    // Highlights
    float hWeight = smoothstep(0.5, 1.0, luma);
    color.rgb += hWeight * (ubuf.highlights / 100.0) * color.rgb;
    
    // Shadows
    float sWeight = 1.0 - smoothstep(0.0, 0.5, luma);
    color.rgb += sWeight * (ubuf.shadows / 100.0) * color.rgb;
    
    // Whites
    color.rgb += (ubuf.whites / 100.0) * smoothstep(0.8, 1.0, luma);
    
    // Blacks
    color.rgb += (ubuf.blacks / 100.0) * (1.0 - smoothstep(0.0, 0.2, luma));

    // 4. Saturation
    float gray = dot(color.rgb, vec3(0.299, 0.587, 0.114));
    color.rgb = mix(vec3(gray), color.rgb, 1.0 + (ubuf.saturation / 100.0));
    
    // 5. Vibrance (Selective saturation)
    float max_color = max(color.r, max(color.g, color.b));
    float avg_color = (color.r + color.g + color.b) / 3.0;
    float amt = (max_color - avg_color) * (-ubuf.vibrance / 100.0) * 3.0;
    color.rgb = mix(color.rgb, vec3(max_color), amt);

    fragColor = color * ubuf.qt_Opacity;
}
