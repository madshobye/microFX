#ifndef MICROFX_GPU_MATH_H
#define MICROFX_GPU_MATH_H

// Reusable GLES2 shader fragments. These operations remain in GLSL so retained
// JavaScript objects select effects without moving per-pixel work onto the CPU.
#define MICROFX_GLSL_LENGTH2 \
    "float microfxLength2(vec2 value){return sqrt(dot(value,value));}"

#define MICROFX_GLSL_NOISE2 \
    "float microfxHash21(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}" \
    "float microfxNoise2(vec2 p){vec2 i=floor(p),f=fract(p);f=f*f*(3.0-2.0*f);" \
    "return mix(mix(microfxHash21(i),microfxHash21(i+vec2(1,0)),f.x)," \
    "mix(microfxHash21(i+vec2(0,1)),microfxHash21(i+vec2(1)),f.x),f.y);}"

#define MICROFX_GLSL_MATH MICROFX_GLSL_LENGTH2 MICROFX_GLSL_NOISE2

#endif
