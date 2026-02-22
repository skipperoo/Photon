#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D lumaTexture;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    vec2 sourceSize;
    int searchWindow; // 19 or 21
} ubuf;

void main() {
    vec2 texelSize = 1.0 / ubuf.sourceSize;
    vec2 center = qt_TexCoord0;
    
    float min_ssd = 1000000.0;
    vec2 best_offset = vec2(0.0);
    
    int radius = ubuf.searchWindow / 2;
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            if (dx == 0 && dy == 0) continue;
            
            vec2 offset = vec2(float(dx), float(dy));
            
            // 3x3 Patch SSD
            float ssd = 0.0;
            for (int py = -1; py <= 1; py++) {
                for (int px = -1; px <= 1; px++) {
                    vec2 p_offset = vec2(float(px), float(py));
                    float center_sample = texture(lumaTexture, center + p_offset * texelSize).r;
                    float candidate_sample = texture(lumaTexture, center + (offset + p_offset) * texelSize).r;
                    float diff = candidate_sample - center_sample;
                    ssd += diff * diff;
                }
            }
            
            if (ssd < min_ssd) {
                min_ssd = ssd;
                best_offset = offset;
            }
        }
    }
    
    // Encode offset (-128 to 127) into 0-1 range
    fragColor = vec4((best_offset.x + 128.0) / 255.0, (best_offset.y + 128.0) / 255.0, min_ssd, 1.0);
}
