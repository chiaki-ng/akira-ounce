#version 450

layout(set = 0, binding = 0) uniform sampler2D luma;
layout(set = 0, binding = 1) uniform sampler2D chroma;

layout(push_constant) uniform Scale
{
    vec2 uvScale;
    vec2 uvOffset;
} scale;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;

void main()
{
    vec2 coord = uv * scale.uvScale + scale.uvOffset;
    float y = (texture(luma, coord).r - 16.0 / 255.0) * (255.0 / 219.0);
    vec2 cbcr = (texture(chroma, coord).rg - 128.0 / 255.0) * (255.0 / 224.0);
    vec3 rgb = vec3(
        y + 1.5748 * cbcr.y,
        y - 0.1873 * cbcr.x - 0.4681 * cbcr.y,
        y + 1.8556 * cbcr.x);
    color = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}
