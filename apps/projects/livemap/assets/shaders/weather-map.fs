#version 100
precision mediump float;

varying mediump vec2 vUv;
uniform sampler2D uTexture;
uniform sampler2D uTexture2;
uniform sampler2D uTexture3;

void main() {
    lowp vec3 dark = texture2D(uTexture, vUv).rgb;
    lowp vec3 day = texture2D(uTexture2, vUv).rgb;
    lowp vec3 nightSource = texture2D(uTexture3, vUv).rgb;
    lowp float dayBrightness = dot(day, vec3(0.299, 0.587, 0.114));
    lowp float dayMaximum = max(day.r, max(day.g, day.b));
    lowp float dayMinimum = min(day.r, min(day.g, day.b));
    lowp float daySaturation = dayMaximum - dayMinimum;
    lowp float paleSurface = smoothstep(0.34, 0.72, dayBrightness) *
        (1.0 - smoothstep(0.055, 0.18, daySaturation));

    // This is the proven archived satellite/night intersection. The daytime
    // satellite contributes only its pale fine structure; Earth at Night only
    // decides which of those pixels may glow. Dark Matter remains the base.
    lowp float warmSignal = max(0.0,
        min(nightSource.r, nightSource.g) - nightSource.b * 0.45);
    lowp float lightSignal = smoothstep(0.035, 0.36, warmSignal);
    lowp float light = lightSignal * lightSignal * paleSurface * 1.22;
    lowp vec3 darkBase = max((dark - vec3(0.035)) * 0.90, vec3(0.0));
    lowp vec3 night = darkBase + vec3(1.00, 0.62, 0.18) * light;
    gl_FragColor = vec4(night, 1.0);
}
