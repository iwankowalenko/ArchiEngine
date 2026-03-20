#version 330 core

in vec2 vUv;
in vec3 vWorldNormal;

uniform vec4 uColor;
uniform sampler2D uTexture;
uniform int uUseTexture;

out vec4 FragColor;

void main()
{
    vec4 baseColor = uColor;
    if (uUseTexture != 0)
        baseColor *= texture(uTexture, vUv);

    vec3 normal = normalize(vWorldNormal);
    vec3 lightDir = normalize(vec3(0.45, 0.70, 0.55));
    float diffuse = max(dot(normal, lightDir), 0.25);

    FragColor = vec4(baseColor.rgb * diffuse, baseColor.a);
}
