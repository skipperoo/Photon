#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D source;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 sourceSize;
    vec2 axis;
} ubuf;

float sample_log(vec2 uv) {
    return texture(source, uv).x;
}

void main() {
    // Hardcoded: gaussian weights with sigma = 3.50 sampled at [-14,14]
    vec2 texelSize = 1.0 / max(ubuf.sourceSize, vec2(1.0));
    vec2 stepUv = ubuf.axis * texelSize;

    float v = 0.11398719 * sample_log(qt_TexCoord0);
    v += 0.20624514 * (sample_log(qt_TexCoord0 + stepUv * 1.46942595) +
                       sample_log(qt_TexCoord0 - stepUv * 1.46942595));
    v += 0.13826867 * (sample_log(qt_TexCoord0 + stepUv * 3.42905340) +
                       sample_log(qt_TexCoord0 - stepUv * 3.42905340));
    v += 0.06731104 * (sample_log(qt_TexCoord0 + stepUv * 5.38960340) +
                       sample_log(qt_TexCoord0 - stepUv * 5.38960340));
    v += 0.02378969 * (sample_log(qt_TexCoord0 + stepUv * 7.35154728) +
                       sample_log(qt_TexCoord0 - stepUv * 7.35154728));
    v += 0.00610264 * (sample_log(qt_TexCoord0 + stepUv * 9.31528835) +
                       sample_log(qt_TexCoord0 - stepUv * 9.31528835));
    v += 0.00113588 * (sample_log(qt_TexCoord0 + stepUv * 11.28114775) +
                       sample_log(qt_TexCoord0 - stepUv * 11.28114775));
    v += 0.00015335 * (sample_log(qt_TexCoord0 + stepUv * 13.24935770) +
                       sample_log(qt_TexCoord0 - stepUv * 13.24935770));

    fragColor = vec4(v, v, v, 1.0);
}
