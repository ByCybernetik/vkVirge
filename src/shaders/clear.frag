#version 450

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform ClearColor {
    vec4 color;
} clearData;

void main() {
    outColor = clearData.color;
}