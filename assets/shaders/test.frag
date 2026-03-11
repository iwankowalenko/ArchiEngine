#version 330 core
in vec2 vUv;

uniform vec4 uColor;
uniform sampler2D uTexture;
uniform int uUseTexture;
out vec4 FragColor;

void main()
{
    vec4 color = uColor;
    if (uUseTexture != 0)
        color *= texture(uTexture, vUv);
    FragColor = color;
}

