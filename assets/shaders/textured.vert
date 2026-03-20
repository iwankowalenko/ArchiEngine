#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

out vec2 vUv;
out vec3 vWorldNormal;

void main()
{
    vUv = aUv;
    vWorldNormal = normalize(mat3(transpose(inverse(uModel))) * aNormal);
    gl_Position = uProjection * uView * uModel * vec4(aPos, 1.0);
}
