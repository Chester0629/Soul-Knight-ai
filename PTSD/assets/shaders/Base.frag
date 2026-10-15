#version 410 core

layout(location = 0) in vec2 uv;

layout(location = 0) out vec4 fragColor;

uniform sampler2D surface;
// P3.0c: per-draw colour modulation (Unity SpriteRenderer.m_Color: texel * colour, alpha included),
// stored as (colour - 1) so the default uniform value 0 is the identity for every program that
// never sets it (Text, FilledImage). Util::Image sets it on every draw.
uniform vec4 tintMinusOne;

void main() {
    vec4 texColor = texture(surface, uv);

    if (texColor.a < 0.01)
        discard;

    fragColor = texColor * (vec4(1.0) + tintMinusOne);
}
