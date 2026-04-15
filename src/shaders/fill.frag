#version 450

layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform FillColor {
    vec4 color;
} fillData;

void main() {
    outColor = fillData.color;
}