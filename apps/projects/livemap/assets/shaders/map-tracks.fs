#version 100
precision lowp float;

varying mediump vec2 vUv;
uniform sampler2D uTexture;
uniform sampler2D uOverlay;

void main() {
    lowp vec4 track = texture2D(uOverlay, vUv);
    gl_FragColor = track.a >= 0.4 ? vec4(track.rgb, 1.0) :
        texture2D(uTexture, vUv);
}
