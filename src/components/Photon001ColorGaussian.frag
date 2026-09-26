#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D source;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 sourceSize;
    vec2 axis;
    float coarse;
    float inputIsLinear;
} ubuf;

vec3 srgb_to_linear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

vec3 sample_linear(vec2 uv) {
    vec3 c = texture(source, uv).rgb;
    if (ubuf.inputIsLinear > 0.5) return c;
    return srgb_to_linear(c);
}

void main() {
    vec2 texelSize = 1.0 / max(ubuf.sourceSize, vec2(1.0));
    vec2 stepUv = ubuf.axis * texelSize;

    vec3 v;
    if (ubuf.coarse > 0.5) {
        v = 0.11398719 * sample_linear(qt_TexCoord0);
        v += 0.20624514 * (sample_linear(qt_TexCoord0 + stepUv * 1.46942595) + sample_linear(qt_TexCoord0 - stepUv * 1.46942595));
        v += 0.13826867 * (sample_linear(qt_TexCoord0 + stepUv * 3.42905340) + sample_linear(qt_TexCoord0 - stepUv * 3.42905340));
        v += 0.06731104 * (sample_linear(qt_TexCoord0 + stepUv * 5.38960340) + sample_linear(qt_TexCoord0 - stepUv * 5.38960340));
        v += 0.02378969 * (sample_linear(qt_TexCoord0 + stepUv * 7.35154728) + sample_linear(qt_TexCoord0 - stepUv * 7.35154728));
        v += 0.00610264 * (sample_linear(qt_TexCoord0 + stepUv * 9.31528835) + sample_linear(qt_TexCoord0 - stepUv * 9.31528835));
        v += 0.00113588 * (sample_linear(qt_TexCoord0 + stepUv * 11.28114775) + sample_linear(qt_TexCoord0 - stepUv * 11.28114775));
        v += 0.00015335 * (sample_linear(qt_TexCoord0 + stepUv * 13.24935770) + sample_linear(qt_TexCoord0 - stepUv * 13.24935770));
    } else {
        v = 0.39894347 * sample_linear(qt_TexCoord0);
        v += 0.29596257 * (sample_linear(qt_TexCoord0 + stepUv * 1.18242552) + sample_linear(qt_TexCoord0 - stepUv * 1.18242552));
        v += 0.00456569 * (sample_linear(qt_TexCoord0 + stepUv * 3.02931223) + sample_linear(qt_TexCoord0 - stepUv * 3.02931223));
    }

    fragColor = vec4(v, 1.0);
}
