void main() {
    float left = texture2D(pressure_tex, uv - vec2(sim_texel.x,0)).x;
    float right = texture2D(pressure_tex, uv + vec2(sim_texel.x,0)).x;
    float bottom = texture2D(pressure_tex, uv - vec2(0,sim_texel.y)).x;
    float top = texture2D(pressure_tex, uv + vec2(0,sim_texel.y)).x;
    float divergence = texture2D(divergence_tex, uv).x;
    gl_FragColor = vec4((left + right + bottom + top - divergence) * 0.25, 0.0, 0.0, 1.0);
}
