#version 100
precision mediump float;

varying lowp vec4 vColor;
varying vec3 vLocal;
varying vec3 vEffect;
uniform float uTime;

void main()
{
    float pulse=0.92+0.08*sin(uTime*0.7+vLocal.y*5.0);
    gl_FragColor=vec4(vColor.rgb*pulse,vColor.a);
}
