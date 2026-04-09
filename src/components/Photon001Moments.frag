#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D source;
layout(binding = 2) uniform sampler2D minmaxmeanSource;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 sourceSize;
} ubuf;

void main() {
    vec2 texel = 1.0 / max(ubuf.sourceSize, vec2(1.0));
    vec2 vTexCoords[4];
    vTexCoords[0] = qt_TexCoord0 + vec2( texel.x,  texel.y);
    vTexCoords[1] = qt_TexCoord0 + vec2( texel.x, -texel.y);
    vTexCoords[2] = qt_TexCoord0 + vec2(-texel.x, -texel.y);
    vTexCoords[3] = qt_TexCoord0 + vec2(-texel.x,  texel.y);

    float meanVal = texture(minmaxmeanSource, qt_TexCoord0).z;

    float samples[4];
    samples[0] = texture(source, vTexCoords[0]).x;
    samples[1] = texture(source, vTexCoords[1]).x;
    samples[2] = texture(source, vTexCoords[2]).x;
    samples[3] = texture(source, vTexCoords[3]).x;

    vec4 diff = vec4(samples[0], samples[1], samples[2], samples[3]) - vec4(meanVal);
    vec4 diff2 = diff * diff;
    vec4 diff3 = diff2 * diff;
    float variance = 0.25 * dot(diff2, vec4(1.0));
    float moment3 = 0.25 * dot(diff3, vec4(1.0));
    fragColor = vec4(variance, moment3, 0.0, 1.0);
}
