varying vec2 uv;
uniform sampler2D velocity_tex;
uniform sampler2D density_tex;
uniform sampler2D pressure_tex;
uniform sampler2D divergence_tex;
uniform sampler2D target_tex;
uniform sampler2D background_tex;
uniform sampler2D actor_tex;
uniform sampler2D model_depth_tex;
uniform sampler3D volume_noise_tex;
uniform sampler3D mist_cache_tex;
uniform sampler2D foreground_tex;
uniform sampler2D foreground_depth_tex;
uniform vec2 screen_size;
uniform vec2 sim_texel;
uniform float time_s;
uniform float phase_s;
uniform float step_s;
uniform float formation;
uniform float dispersion;
uniform float state_value;
uniform float state_age;
uniform float success_age;
uniform float exposure_value;
uniform vec4 emitter;
uniform float release_value;
uniform float mist_slice;
uniform vec4 impulses[8];

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}
float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1,0)), f.x),
               mix(hash(i + vec2(0,1)), hash(i + vec2(1,1)), f.x), f.y);
}
float fbm(vec2 p) {
    float value = 0.0, weight = 0.5;
    for (int octave = 0; octave < 5; ++octave) {
        value += noise(p) * weight;
        p = mat2(1.6, 1.2, -1.2, 1.6) * p + vec2(3.1, 7.9);
        weight *= 0.5;
    }
    return value;
}
float gaussian(vec2 p, vec2 center, float sharpness) {
    vec2 d = p - center;
    return exp(-dot(d, d) * sharpness);
}
vec2 curl_flow(vec2 p, float clock) {
    float e = 0.015;
    vec2 shift = vec2(clock * 0.11, -clock * 0.07);
    float x = fbm(p + vec2(e,0) + shift) - fbm(p - vec2(e,0) + shift);
    float y = fbm(p + vec2(0,e) + shift) - fbm(p - vec2(0,e) + shift);
    return vec2(y, -x) / (2.0 * e);
}
