#version 450

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inTexCoord;

layout(location = 0) out vec2 fragTexCoord;

void main() {
    gl_Position = vec4(inPosition, 0.0, 1.0);
    // Переворачиваем текстурную координату по Y
    fragTexCoord = vec2(inTexCoord.x, 1.0 - inTexCoord.y);
}
