#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D source;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 sourceSize;
} ubuf;

float sample_log(vec2 uv) {
    return texture(source, uv).x;
}

float gaussian_sigma1_axis(vec2 uv, vec2 texelSize, vec2 axis) {
    vec2 stepUv = axis * texelSize;
    float v = 0.39894347 * sample_log(uv);
    v += 0.29596257 * (sample_log(uv + stepUv * 1.18242552) + sample_log(uv - stepUv * 1.18242552));
    v += 0.00456569 * (sample_log(uv + stepUv * 3.02931223) + sample_log(uv - stepUv * 3.02931223));
    return v;
}

void main() {
    vec2 texelSize = 1.0 / max(ubuf.sourceSize, vec2(1.0));
    float h = gaussian_sigma1_axis(qt_TexCoord0, texelSize, vec2(1.0, 0.0));
    float v = gaussian_sigma1_axis(qt_TexCoord0, texelSize, vec2(0.0, 1.0));
    float outVal = 0.5 * (h + v);
    fragColor = vec4(outVal, outVal, outVal, 1.0);
}
