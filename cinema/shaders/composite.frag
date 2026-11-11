void main() {
    float scale = min(screen_size.x/1920.0, screen_size.y/1080.0);
    vec2 offset = (screen_size - vec2(1920.0,1080.0)*scale)*0.5;
    vec2 p = (vec2(gl_FragCoord.x, screen_size.y-gl_FragCoord.y)-offset)/scale;
    if (p.x < 0.0 || p.y < 0.0 || p.x > 1920.0 || p.y > 1080.0) {
        gl_FragColor = vec4(0.025,0.032,0.037,1.0);
        return;
    }
    // The palm reaches the glass at 0.32 seconds. A travelling reflection
    // ring and damped camera kick communicate contact, without a white flash.
    float impact_age = state_age-0.32;
    float impact = state_value > 2.5 && impact_age >= 0.0 ? exp(-impact_age*5.0) : 0.0;
    p += vec2(sin(impact_age*83.0),cos(impact_age*67.0))*impact*9.0;
    vec2 screen_uv = vec2(p.x/1920.0, 1.0-p.y/1080.0);
    vec2 drift = curl_flow(p*0.0016, time_s*0.3) * 0.0007;
    // An empty chamber behind the meshes: the reference plate contains baked
    // creature silhouettes, so reserve it for the glass/frame border only.
    vec3 color = mix(vec3(0.59,0.64,0.68),vec3(0.77,0.80,0.82),
                     exp(-dot(screen_uv-vec2(0.53,0.56),screen_uv-vec2(0.53,0.56))*3.0));
    color += (fbm(p*0.002+drift+time_s*0.009)-0.5)*0.10;
    float fog = 0.0;
    for (int layer = 0; layer < 12; ++layer) {
        float depth = float(layer)/12.0;
        vec2 position = p*(0.0014+depth*0.0007) + vec2(time_s*0.009,-time_s*0.006)*(0.5+depth);
        fog += fbm(position + vec2(depth*8.7,depth*3.1)) / 12.0;
    }
    color = mix(color, vec3(0.77,0.80,0.82), fog*0.18);
    vec2 model_uv = (p*scale+offset)/screen_size;
    model_uv.y = 1.0-model_uv.y;
    vec4 limb = texture2D(actor_tex, model_uv);
    color = mix(color, limb.rgb, limb.a);
    float near_mist = fbm(p*0.0026 + vec2(-time_s*0.018,time_s*0.004));
    color = mix(color, vec3(0.79,0.82,0.84), near_mist*0.12);

    vec2 ink_uv = (p-vec2(640.0,190.0))/640.0;
    ink_uv.y = 1.0-ink_uv.y;
    if (ink_uv.x > 0.0 && ink_uv.x < 1.0 && ink_uv.y > 0.0 && ink_uv.y < 1.0) {
        vec2 distortion = curl_flow(ink_uv*6.0, time_s*0.5) * 0.0012;
        float density = texture2D(density_tex, ink_uv+distortion).r;
        float goal = texture2D(target_tex, ink_uv+distortion*0.55).r;
        float hold = formation*(1.0-dispersion);
        if (success_age >= 0.0) hold *= exp(-success_age*5.0);
        density = mix(density, max(density*0.45,goal*1.35), hold*0.18);
        density *= 0.72+0.45*fbm(ink_uv*96.0+time_s*0.12);
        float accepted_ink = success_age < 0.0 ? 1.0 : 1.0-smoothstep(0.0,0.8,success_age);
        density *= accepted_ink;
        float extinction = 1.0-exp(-density*2.8);
        float halo = texture2D(density_tex,ink_uv+sim_texel*vec2(2,0)).r +
                     texture2D(density_tex,ink_uv-sim_texel*vec2(2,0)).r +
                     texture2D(density_tex,ink_uv+sim_texel*vec2(0,2)).r +
                     texture2D(density_tex,ink_uv-sim_texel*vec2(0,2)).r;
        halo *= accepted_ink;
        color = mix(color,vec3(0.20,0.24,0.26),(1.0-exp(-halo*0.16))*(1.0-extinction)*0.45);
        float granular = fbm(ink_uv*170.0 + time_s*0.07);
        vec3 pigment = vec3(0.055,0.065,0.07) + granular*0.025;
        if (state_value > 2.5)
            pigment += vec3(0.08,0.015,0.0)*exp(-state_age*2.0);
        color = mix(color, pigment, extinction);
    }
    float edge_reflection = pow(abs(p.x/960.0-1.0),8.0)*0.025;
    color += edge_reflection;
    float distance_from_palm = length((p-vec2(620.0,390.0))/1080.0);
    float ring = exp(-pow((distance_from_palm-impact_age*0.65)*95.0,2.0))*impact;
    color += ring*vec3(0.08,0.09,0.10);
    color *= 1.0-impact*0.055;
    float grain = hash(gl_FragCoord.xy + vec2(floor(time_s*24.0),0.0))-0.5;
    color += grain*0.007;
    if (p.y < 28.0 || p.y > 1046.0)
        color = texture2D(background_tex, screen_uv).rgb;
    float vignette = 1.0-0.16*dot(screen_uv-vec2(0.5),screen_uv-vec2(0.5));
    color *= exposure_value*vec3(0.94,1.0,1.035)*vignette;

    gl_FragColor = vec4(clamp(color,0.0,1.0),1.0);
}
