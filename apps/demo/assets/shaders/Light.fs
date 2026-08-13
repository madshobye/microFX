#version 100
precision lowp float;

varying vec3 fragNormal;
varying vec4 fragColor;

uniform vec4 colDiffuse;
uniform vec3 lightDirection;
uniform vec3 lightColor;
uniform vec3 ambientColor;

void main()
{
    // The mesh normals are normalized in the vertex stage. Avoid normalize()
    // and pow() per fragment on this older Vivante GPU.
    vec3 normal = fragNormal;
    float diffuse = max(dot(normal, -lightDirection), 0.0);
    float skyFill = 0.12*max(normal.y, 0.0);
    vec3 lighting = ambientColor + lightColor*(diffuse*0.82 + skyFill);
    gl_FragColor = vec4(colDiffuse.rgb*fragColor.rgb*lighting,
                        colDiffuse.a*fragColor.a);
}
