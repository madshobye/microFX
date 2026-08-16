#version 100
precision mediump float;

varying mediump vec2 vUv;
uniform sampler2D uTexture;
uniform vec4 uParams[8];

void main() {
    vec4 source = texture2D(uTexture, vUv);
    if (uParams[0].x < 0.5) {
        gl_FragColor = vec4(source.rgb, 1.0);
    } else {
        gl_FragColor = vec4(source.rgb * vec3(0.64, 0.76, 0.88), 0.36);
    }
}
