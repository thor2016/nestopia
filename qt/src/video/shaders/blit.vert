#version 450

// Draws a screen aligned quad as a 4 vertex triangle strip. rect holds the
// quad corners in normalized device coordinates (x0, y0, x1, y1).
layout(push_constant) uniform PushConstants {
    vec4 rect;
    vec2 uvScale;
} pc;

layout(location = 0) out vec2 uv;

void main()
{
    vec2 p = vec2(float(gl_VertexIndex & 1), float(gl_VertexIndex >> 1));
    uv = p * pc.uvScale;
    gl_Position = vec4(mix(pc.rect.xy, pc.rect.zw, p), 0.0, 1.0);
}
