void main() {
    vec2 previous = texture2D(velocity_tex, uv).xy;
    vec2 velocity = texture2D(velocity_tex, uv - previous * step_s * sim_texel).xy;
    vec2 radial = uv - vec2(0.5);
    vec2 tangent = vec2(-radial.y, radial.x);
    vec2 force = curl_flow(uv * 5.0, time_s) * 18.0;
    force += tangent * (4.0 + dispersion * 24.0);
    force += radial * dispersion * 45.0;
    float emission = smoothstep(0.4, 0.9, phase_s) * (1.0 - smoothstep(2.0, 3.6, phase_s));
    force += gaussian(uv, vec2(0.20, 0.69), 320.0) * vec2(120.0, -34.0) * emission;
    if (state_value > 1.5 && state_value < 2.5)
        force += tangent * 14.0;
    if (state_value > 2.5)
        force += radial * 110.0 * exp(-state_age * 2.0);
    if (success_age >= 0.0)
        force += radial * 180.0 * exp(-success_age * 1.5);
    for (int i = 0; i < 8; ++i) {
        float age = time_s - impulses[i].w;
        float pulse = exp(-max(age, 0.0) * 4.5) * step(0.0, age);
        vec2 delta = uv - impulses[i].xy;
        force += gaussian(uv, impulses[i].xy, 95.0) *
                 (vec2(-delta.y, delta.x) * 1200.0 + delta * 650.0) * impulses[i].z * pulse;
    }
    velocity = (velocity + force * step_s) * exp(-step_s * 0.5);
    if (uv.x < sim_texel.x || uv.x > 1.0 - sim_texel.x) velocity.x = 0.0;
    if (uv.y < sim_texel.y || uv.y > 1.0 - sim_texel.y) velocity.y = 0.0;
    gl_FragColor = vec4(clamp(velocity, -150.0, 150.0), 0.0, 1.0);
}
