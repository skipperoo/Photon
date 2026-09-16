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
    // Hardcoded: gaussian weights with sigma = 1.00 sampled at [-4,4]
    vec2 texelSize = 1.0 / max(ubuf.sourceSize, vec2(1.0));
    vec2 stepUv = ubuf.axis * texelSize;

    float v = 0.39894347 * sample_log(qt_TexCoord0);
    v += 0.29596257 * (sample_log(qt_TexCoord0 + stepUv * 1.18242552) +
                       sample_log(qt_TexCoord0 - stepUv * 1.18242552));
    v += 0.00456569 * (sample_log(qt_TexCoord0 + stepUv * 3.02931223) +
                       sample_log(qt_TexCoord0 - stepUv * 3.02931223));

    fragColor = vec4(v, v, v, 1.0);
}
