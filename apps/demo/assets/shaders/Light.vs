#version 100
attribute vec3 vertexPosition;
attribute vec2 vertexTexCoord;
attribute vec3 vertexNormal;
attribute vec4 vertexColor;

uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;

varying vec3 fragNormal;
varying vec4 fragColor;

void main()
{
    fragNormal = normalize(vec3(matNormal*vec4(vertexNormal, 0.0)));
    fragColor = vertexColor;
    gl_Position = mvp*vec4(vertexPosition, 1.0);
}
