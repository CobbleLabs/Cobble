#version 330 core

in vec2 vUV;
out vec4 FragColor;

uniform vec2  uCenter;
uniform vec2  uHalfSize;
uniform float uRound;
uniform vec3  uColor;
uniform float uIntensity;
uniform float uSoftness;

float rounded_box_sd(vec2 p, vec2 half_size, float round)
{
    vec2 q = abs(p) - half_size + round;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - round;
}

void main()
{
    vec2 p = vUV - uCenter;
    float dist = rounded_box_sd(p, uHalfSize, uRound);
    float edge = max(uSoftness, 0.001);
    float glow = exp(-(dist * dist) / (edge * edge));
    float alpha = glow * uIntensity;
    FragColor = vec4(uColor * alpha, alpha);
}
