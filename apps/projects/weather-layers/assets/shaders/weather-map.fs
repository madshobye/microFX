#version 100
precision mediump float;

varying mediump vec2 vUv;
uniform sampler2D uTexture;
uniform sampler2D uTexture2;
uniform vec4 uParams[8];

void main() {
    lowp vec3 day = texture2D(uTexture, vUv).rgb;
    lowp float dayBrightness = dot(day, vec3(0.299, 0.587, 0.114));
    lowp float dayWhite = min(day.r, min(day.g, day.b));
    lowp float nightBrightness = texture2D(uTexture2, vUv).r;
    lowp float base = dayBrightness * dayBrightness * 0.24;
    lowp float light = nightBrightness * dayWhite;

    // The squared grayscale image is the dark, high-contrast geographic base.
    // Only neutral bright day pixels inside the VIIRS mask become gold, which
    // favors pale roofs and roads without lighting saturated vegetation.
    lowp vec3 night = vec3(base) + vec3(1.00, 0.62, 0.18) * light;
    lowp float amount = uParams[0].x;

    gl_FragColor = vec4(day + (night - day) * amount, 1.0);
}
