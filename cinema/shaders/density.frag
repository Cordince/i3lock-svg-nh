void main() {
    vec2 velocity = texture2D(velocity_tex, uv).xy;
    vec2 transported = uv - velocity * step_s * sim_texel;
    float density = texture2D(density_tex, transported).r;
    float detail = fbm(uv * 24.0 + vec2(time_s * 0.08, -time_s * 0.13));
    float goal = texture2D(target_tex, uv + curl_flow(uv * 7.0, time_s) * 0.0015).r;
    float arrival = smoothstep(0.0, 0.35, formation - detail * 0.28);
    float attraction = (2.0 + formation * 11.0) * (1.0 - dispersion);
    if (success_age >= 0.0) attraction = 0.0;
    density = mix(density, goal * arrival * 1.45, 1.0 - exp(-step_s * attraction));
    float emission = smoothstep(0.4, 0.9, phase_s) * (1.0 - smoothstep(2.0, 3.4, phase_s));
    if (success_age >= 0.0) emission = 0.0;
    density += gaussian(uv, vec2(0.20,0.69), 1200.0) * emission * step_s * 8.0;
    for (int i = 0; i < 8; ++i) {
        float age = time_s - impulses[i].w;
        float pulse = exp(-max(age, 0.0) * 6.0) * step(0.0, age);
        density += gaussian(uv, impulses[i].xy, 450.0) * impulses[i].z * pulse * step_s * 8.0;
    }
    density *= exp(-step_s * (0.12 + dispersion * 2.4));
    if (success_age >= 0.0)
        density *= exp(-step_s * (3.0 + success_age * 5.0));
    float edge = smoothstep(0.0, 0.035, uv.x) * smoothstep(0.0, 0.035, uv.y) *
                 smoothstep(0.0, 0.035, 1.0-uv.x) * smoothstep(0.0, 0.035, 1.0-uv.y);
    gl_FragColor = vec4(clamp(density, 0.0, 3.0) * edge, 0.0, 0.0, 1.0);
}
