void main() {
    float left = texture2D(velocity_tex, uv - vec2(sim_texel.x,0)).x;
    float right = texture2D(velocity_tex, uv + vec2(sim_texel.x,0)).x;
    float bottom = texture2D(velocity_tex, uv - vec2(0,sim_texel.y)).y;
    float top = texture2D(velocity_tex, uv + vec2(0,sim_texel.y)).y;
    gl_FragColor = vec4((right - left + top - bottom) * 0.5, 0.0, 0.0, 1.0);
}
