#version 100
precision highp float;

attribute vec3 aPosition;
attribute vec3 aNormal;
attribute float aObject;

uniform mat4 uView;
uniform mat4 uProjection;
uniform vec4 uPositionScale[16];
uniform vec4 uRotation[16];
uniform vec4 uColor[16];
uniform vec4 uEffect[16];

varying lowp vec4 vColor;
varying vec3 vLocal;
varying vec3 vEffect;

void fetch(int i, out vec4 p, out vec4 r, out vec4 c, out vec4 e)
{
    if (i == 0) { p=uPositionScale[0]; r=uRotation[0]; c=uColor[0]; e=uEffect[0]; }
    else if (i == 1) { p=uPositionScale[1]; r=uRotation[1]; c=uColor[1]; e=uEffect[1]; }
    else if (i == 2) { p=uPositionScale[2]; r=uRotation[2]; c=uColor[2]; e=uEffect[2]; }
    else if (i == 3) { p=uPositionScale[3]; r=uRotation[3]; c=uColor[3]; e=uEffect[3]; }
    else if (i == 4) { p=uPositionScale[4]; r=uRotation[4]; c=uColor[4]; e=uEffect[4]; }
    else if (i == 5) { p=uPositionScale[5]; r=uRotation[5]; c=uColor[5]; e=uEffect[5]; }
    else if (i == 6) { p=uPositionScale[6]; r=uRotation[6]; c=uColor[6]; e=uEffect[6]; }
    else if (i == 7) { p=uPositionScale[7]; r=uRotation[7]; c=uColor[7]; e=uEffect[7]; }
    else if (i == 8) { p=uPositionScale[8]; r=uRotation[8]; c=uColor[8]; e=uEffect[8]; }
    else if (i == 9) { p=uPositionScale[9]; r=uRotation[9]; c=uColor[9]; e=uEffect[9]; }
    else if (i == 10) { p=uPositionScale[10]; r=uRotation[10]; c=uColor[10]; e=uEffect[10]; }
    else if (i == 11) { p=uPositionScale[11]; r=uRotation[11]; c=uColor[11]; e=uEffect[11]; }
    else if (i == 12) { p=uPositionScale[12]; r=uRotation[12]; c=uColor[12]; e=uEffect[12]; }
    else if (i == 13) { p=uPositionScale[13]; r=uRotation[13]; c=uColor[13]; e=uEffect[13]; }
    else if (i == 14) { p=uPositionScale[14]; r=uRotation[14]; c=uColor[14]; e=uEffect[14]; }
    else { p=uPositionScale[15]; r=uRotation[15]; c=uColor[15]; e=uEffect[15]; }
}

vec3 rotatePoint(vec3 p, vec3 r)
{
    vec3 c=cos(r), s=sin(r);
    p=vec3(p.x,c.x*p.y-s.x*p.z,s.x*p.y+c.x*p.z);
    p=vec3(c.y*p.x+s.y*p.z,p.y,-s.y*p.x+c.y*p.z);
    return vec3(c.z*p.x-s.z*p.y,s.z*p.x+c.z*p.y,p.z);
}

void main()
{
    vec4 positionScale, rotation, color, effect;
    fetch(int(aObject+0.5), positionScale, rotation, color, effect);
    vec3 p=rotatePoint(aPosition*positionScale.w,rotation.xyz)+positionScale.xyz;
    vec3 n=normalize(rotatePoint(aNormal,rotation.xyz));
    vec3 light=normalize(vec3(0.45,0.78,0.35));
    float diffuse=max(dot(n,light),0.0);
    vColor=vec4(color.rgb*(0.24+0.76*diffuse),color.a);
    vLocal=aPosition;
    vEffect=effect.xyz;
    gl_Position=uProjection*uView*vec4(p,1.0);
}
