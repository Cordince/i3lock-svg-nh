void main() {
    vec2 previous = texture2D(velocity_tex, uv).xy;
    vec2 velocity = texture2D(velocity_tex, uv - previous * step_s * sim_texel).xy;
    vec2 radial = uv - vec2(0.5);
    vec2 tangent = vec2(-radial.y, radial.x);
    vec2 force = curl_flow(uv * 5.0, time_s) * mix(28.0,12.0,formation);
    force += tangent * (4.0 + dispersion * 24.0);
    force += radial * dispersion * 45.0;
    vec2 jet = uv-emitter.xy;
    float along = dot(jet,emitter.zw);
    float across = dot(jet,vec2(-emitter.w,emitter.z));
    float plume = exp(-across*across*260.0)*exp(-max(along,0.0)*7.0)*step(-.025,along);
    force += plume*emitter.zw*420.0*release_value;
    // Curl and radial gathering pull the free plume into the ring before the
    // target-image guidance settles its finer strokes.
    float radius=length(radial);
    force += radial/max(.01,radius)*(.2233-radius)*150.0*formation;
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
    velocity = (velocity + force * step_s) * exp(-step_s * (.5+formation*1.2));
    if (uv.x < sim_texel.x || uv.x > 1.0 - sim_texel.x) velocity.x = 0.0;
    if (uv.y < sim_texel.y || uv.y > 1.0 - sim_texel.y) velocity.y = 0.0;
    gl_FragColor = vec4(clamp(velocity, -150.0, 150.0), 0.0, 1.0);
}
