#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D texSampler;
layout(binding = 1) uniform GammaData {
    float gamma;
} gammaData;

void main() {
    vec4 color = texture(texSampler, fragTexCoord);

    // Применяем гамма-коррекцию
    // Для корректной гамма-коррекции нужно сначала перейти в линейное пространство,
    // а потом применить гамму
    float invGamma = 1.0 / gammaData.gamma;

    // Предполагаем, что текстура в sRGB (как обычно для JPEG)
    // Переходим в линейное пространство (аппроксимация)
    vec3 linearColor = vec3(
        pow(color.r, 2.2),  // sRGB to linear (аппроксимация)
        pow(color.g, 2.2),
        pow(color.b, 2.2)
    );

    // Применяем пользовательскую гамму
    vec3 correctedColor = pow(linearColor, vec3(invGamma));

    // Обратно в sRGB для отображения (аппроксимация)
    vec3 outputColor = vec3(
        pow(correctedColor.r, 1.0/2.2),
        pow(correctedColor.g, 1.0/2.2),
        pow(correctedColor.b, 1.0/2.2)
    );

    outColor = vec4(outputColor, color.a);
}
