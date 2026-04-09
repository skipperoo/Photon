#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D source;

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

    vec3 samples[4];
    samples[0] = texture(source, vTexCoords[0]).xyz;
    samples[1] = texture(source, vTexCoords[1]).xyz;
    samples[2] = texture(source, vTexCoords[2]).xyz;
    samples[3] = texture(source, vTexCoords[3]).xyz;

    float val0 = 0.25 * (samples[0].x + samples[1].x + samples[2].x + samples[3].x);
    float val1 = 0.25 * (samples[0].y + samples[1].y + samples[2].y + samples[3].y);
    fragColor = vec4(val0, val1, 0.0, 1.0);
}
