void main() {
    float left = texture2D(pressure_tex, uv - vec2(sim_texel.x,0)).x;
    float right = texture2D(pressure_tex, uv + vec2(sim_texel.x,0)).x;
    float bottom = texture2D(pressure_tex, uv - vec2(0,sim_texel.y)).x;
    float top = texture2D(pressure_tex, uv + vec2(0,sim_texel.y)).x;
    vec2 velocity = texture2D(velocity_tex, uv).xy - vec2(right - left, top - bottom) * 0.5;
    gl_FragColor = vec4(velocity, 0.0, 1.0);
}
