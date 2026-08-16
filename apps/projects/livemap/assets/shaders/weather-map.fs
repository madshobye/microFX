#version 100
precision mediump float;

varying mediump vec2 vUv;
uniform sampler2D uTexture;
uniform sampler2D uTexture2;

void main() {
    lowp vec3 dark = texture2D(uTexture, vUv).rgb;
    lowp vec3 nightSource = texture2D(uTexture2, vUv).rgb;
    lowp float darkBrightness = dot(dark, vec3(0.299, 0.587, 0.114));

    // Dark Matter supplies stable street-scale structure. Earth at Night only
    // decides where that structure emits warm light; its blue background and
    // broad yellow blobs are never shown directly.
    lowp float warmSignal = max(0.0,
        min(nightSource.r, nightSource.g) - nightSource.b * 0.45);
    lowp float inhabited = smoothstep(0.025, 0.30, warmSignal);
    lowp float street = smoothstep(0.105, 0.38, darkBrightness);
    lowp float light = inhabited * (0.12 + street * street * 1.35);
    lowp vec3 night = dark * 0.88 + vec3(1.00, 0.58, 0.12) * light;
    gl_FragColor = vec4(night, 1.0);
}
