#version 100
precision mediump float;

varying mediump vec2 vUv;
uniform sampler2D uTexture;
uniform sampler2D uTexture2;
uniform vec4 uParams[8];

void main() {
    lowp vec3 day = texture2D(uTexture, vUv).rgb;
    lowp vec3 nightSource = texture2D(uTexture2, vUv).rgb;
    lowp float dayBrightness = dot(day, vec3(0.299, 0.587, 0.114));
    lowp float dayMaximum = max(day.r, max(day.g, day.b));
    lowp float dayMinimum = min(day.r, min(day.g, day.b));
    lowp float daySaturation = dayMaximum - dayMinimum;
    lowp float base = 0.008 + dayBrightness * 0.030 +
        dayBrightness * dayBrightness * 0.12;
    lowp float paleSurface = smoothstep(0.34, 0.72, dayBrightness) *
        (1.0 - smoothstep(0.055, 0.18, daySaturation));

    // Earth at Night 2012 is a blue/yellow image rather than a monochrome
    // light mask. Extract its warm artificial-light signal and reject the blue
    // geographic background before applying it to fine day-image structures.
    lowp float warmSignal = max(0.0,
        min(nightSource.r, nightSource.g) - nightSource.b * 0.45);
    lowp float lightSignal = smoothstep(0.035, 0.36, warmSignal);
    lowp float light = lightSignal * lightSignal * paleSurface * 1.22;
    lowp vec3 night = vec3(base) + vec3(1.00, 0.62, 0.18) * light;
    lowp float amount = uParams[0].x;

    gl_FragColor = vec4(mix(day, night, amount), 1.0);
}
