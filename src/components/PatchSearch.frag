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
    
    // Simple version for Step 3: Find the 1 BEST match in a neighborhood
    // and encode it into RGBA. 
    // In a production BM3D we'd need more, but let's start by offloading the search.
    
    float min_ssd = 1000000.0;
    vec2 best_offset = vec2(0.0);
    
    float center_luma = texture(lumaTexture, center).r;
    
    int radius = ubuf.searchWindow / 2;
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            if (dx == 0 && dy == 0) continue;
            
            vec2 offset = vec2(float(dx), float(dy));
            float sample_luma = texture(lumaTexture, center + offset * texelSize).r;
            
            float diff = sample_luma - center_luma;
            float ssd = diff * diff;
            
            if (ssd < min_ssd) {
                min_ssd = ssd;
                best_offset = offset;
            }
        }
    }
    
    // Encode offset (-128 to 127) into 0-1 range
    // We add 128 and divide by 255
    fragColor = vec4((best_offset.x + 128.0) / 255.0, (best_offset.y + 128.0) / 255.0, min_ssd, 1.0);
}
