void main() {
    vec2 velocity = texture2D(velocity_tex, uv).xy;
    vec2 transported = uv - velocity * step_s * sim_texel;
    float density = texture2D(density_tex, transported).r;
    float detail = fbm(uv * 24.0 + vec2(time_s * 0.08, -time_s * 0.13));
    float goal = texture2D(target_tex, uv + curl_flow(uv * 7.0, time_s) * 0.0015).r;
    // Ink is born at the projected mouth. Only after the plume spreads does
    // the field gather into the predetermined film logogram.
    float gathering = smoothstep(0.0, 1.0, formation);
    float attraction = gathering * 7.0 * (1.0 - dispersion);
    if (success_age >= 0.0) attraction = 0.0;
    density = mix(density, goal * (1.35 + detail*.25), 1.0 - exp(-step_s * attraction));
    vec2 jet = uv-emitter.xy;
    float along = dot(jet,emitter.zw);
    float across = dot(jet,vec2(-emitter.w,emitter.z));
    float mouth = gaussian(uv,emitter.xy,1700.0);
    float plume = exp(-across*across*650.0) * exp(-max(along,0.0)*18.0) * step(0.0,along);
    density += (mouth*24.0 + plume*7.0) * release_value * step_s;
    // The emitted puff spreads off the hand before it settles. Its advected
    // density remains in the field while target guidance draws out the strokes.
    float growth=smoothstep(1.6,3.5,phase_s);
    vec2 cloud_center=mix(emitter.xy,vec2(.50,.52),growth);
    vec2 cloud_delta=uv-cloud_center+curl_flow(uv*6.0,time_s)*.008;
    float radius=mix(.035,.29,growth);
    float cloud=exp(-dot(cloud_delta,cloud_delta)/(radius*radius))*smoothstep(.20,.65,detail);
    float bloom=smoothstep(1.6,2.2,phase_s)*(1.0-smoothstep(3.2,4.0,phase_s));
    if(success_age>=0.0||state_value>2.5)bloom=0.0;
    density+=cloud*bloom*step_s*6.0;
    for (int i = 0; i < 8; ++i) {
        float age = time_s - impulses[i].w;
        float pulse = exp(-max(age, 0.0) * 6.0) * step(0.0, age);
        density += gaussian(uv, impulses[i].xy, 450.0) * impulses[i].z * pulse * step_s * 8.0;
    }
    density *= exp(-step_s * (0.07 + dispersion * 2.4));
    if (success_age >= 0.0)
        density *= exp(-step_s * (3.0 + success_age * 5.0));
    float edge = smoothstep(0.0, 0.035, uv.x) * smoothstep(0.0, 0.035, uv.y) *
                 smoothstep(0.0, 0.035, 1.0-uv.x) * smoothstep(0.0, 0.035, 1.0-uv.y);
    gl_FragColor = vec4(clamp(density, 0.0, 3.0) * edge, 0.0, 0.0, 1.0);
}
