// The camera and mesh pass share a +Z perspective camera. Integrate the
// moving medium in world space, inserting the sampled model at its true depth.
float model_z(vec2 coordinate) {
    float depth=texture2D(model_depth_tex,coordinate).r;
    return 3.0/max(.1,30.0-29.9*depth);
}
vec4 distant_model(vec2 coordinate,out float depth) {
    vec4 center=texture2D(actor_tex,coordinate);
    depth=model_z(coordinate);
    // Premultiplied filtering also samples outside the silhouette. The old
    // alpha-gated filter never touched those pixels and left a hard outline.
    vec4 sum=vec4(0.0);
    float depth_sum=0.0,coverage=0.0,weight_sum=0.0;
    for(int y=-2;y<=2;y++) for(int x=-2;x<=2;x++) {
        vec2 offset=vec2(float(x),float(y));
        float weight=exp(-dot(offset,offset)*.18);
        vec2 tap=coordinate+offset*vec2(4.25)/screen_size;
        vec4 sample_color=texture2D(actor_tex,tap);
        float z=model_z(tap);
        // Do not smear the near hand into the distant fog.
        float alpha=sample_color.a*step(5.0,z);
        sum.rgb+=sample_color.rgb*alpha*weight;
        coverage+=alpha*weight;
        depth_sum+=z*alpha*weight;
        weight_sum+=weight;
    }
    if(coverage<.00001) {depth=14.0;return vec4(0.0);}
    depth=depth_sum/coverage;
    return vec4(sum.rgb/coverage,coverage/weight_sum);
}
vec4 foreground_model(vec2 coordinate,out float depth) {
    vec3 pigment=vec3(0);float coverage=0.0,depth_sum=0.0,weight_sum=0.0;
    // Glass is the focal plane. The appendage behind it gets a small optical
    // footprint, including pixels outside its silhouette, while ink stays sharp.
    for(int y=-1;y<=1;y++) for(int x=-1;x<=1;x++) {
        vec2 offset=vec2(float(x),float(y));
        float weight=exp(-dot(offset,offset)*.55);
        vec2 tap=coordinate+offset*2.10/screen_size;
        vec4 color=texture2D(foreground_tex,tap);
        // Subpixel foreground edges are premultiplied by the model pass.
        if(color.a>.00001) color.rgb/=color.a;
        float z=3.0/max(.1,30.0-29.9*texture2D(foreground_depth_tex,tap).r);
        float alpha=color.a;
        pigment+=color.rgb*alpha*weight;
        depth_sum+=z*alpha*weight;
        coverage+=alpha*weight;
        weight_sum+=weight;
    }
    if(coverage>.00001) {
        depth=depth_sum/coverage;
        return vec4(pigment/coverage,coverage/weight_sum);
    }
    depth=14.0;return vec4(0.0);
}

void mist_segment(vec3 ray,float begin,float end,inout vec3 radiance,inout float transmission) {
    if(end<=begin || transmission<.003) return;
    vec3 position=ray*((begin+end)*.5);
    vec2 ndc=vec2(position.x*1.35/(screen_size.x/screen_size.y),position.y*1.35)/max(.0001,position.z);
    vec4 mist=texture3D(mist_cache_tex,vec3(ndc*.5+.5,position.z/14.0));
    float density=mist.a;
    if(density<.001)return;
    float opacity=1.0-exp(-density*(end-begin)*length(ray));
    radiance+=transmission*mist.rgb*opacity;
    transmission*=1.0-opacity;
}

vec3 chamber_volume(vec2 coordinate,vec2 view_uv) {
    float foreground_depth,background_depth;
    vec4 foreground=foreground_model(coordinate,foreground_depth);
    vec4 background=distant_model(coordinate,background_depth);
    // Both surfaces survive the optical footprint: fractional foreground
    // coverage reveals the actual background geometry, never a fog-colored hole.
    vec4 first=foreground,second=background;
    float first_depth=foreground_depth,second_depth=background_depth;
    if(second_depth<first_depth) {
        first=background;second=foreground;
        first_depth=background_depth;second_depth=foreground_depth;
    }
    vec2 ndc=coordinate*2.0-1.0;
    vec3 ray=vec3(ndc.x*(screen_size.x/screen_size.y)/1.35,ndc.y/1.35,1.0);
    vec3 radiance=vec3(0.0);float transmission=1.0;
    for(int sample_index=0;sample_index<64;sample_index++) {
        float begin=float(sample_index)*14.0/64.0;
        float end=float(sample_index+1)*14.0/64.0;
        float cursor=begin;
        if(first.a>0.0 && first_depth>=begin && first_depth<end) {
            mist_segment(ray,cursor,first_depth,radiance,transmission);
            radiance+=transmission*first.rgb*first.a;
            transmission*=1.0-first.a;cursor=first_depth;
        }
        if(second.a>0.0 && second_depth>=begin && second_depth<end) {
            mist_segment(ray,cursor,second_depth,radiance,transmission);
            radiance+=transmission*second.rgb*second.a;
            transmission*=1.0-second.a;cursor=second_depth;
        }
        mist_segment(ray,cursor,end,radiance,transmission);
    }
    vec3 rear_light=mix(vec3(.16,.22,.27),vec3(.81,.84,.85),smoothstep(.10,.95,view_uv.y));
    vec3 chamber=radiance+transmission*rear_light;
    // The camera stands inside the human chamber, behind the glass at z=3.2.
    // Ray/plane intersections reveal actual perspective ceiling and floor.
    float plane_y=ray.y>0.0 ? 1.68 : -1.52;
    float plane_z=plane_y/(abs(ray.y)<.0001 ? .0001 : ray.y);
    if(plane_z>0.0 && plane_z<3.2) {
        vec3 point=ray*plane_z;
        float ceiling=step(0.0,ray.y);
        float rough=noise(point.xz*32.0)*.010+noise(point.xz*4.0)*.016;
        float distance_light=pow(clamp(plane_z/3.2,0.0,1.0),3.0);
        vec3 concrete=mix(vec3(.060,.067,.070),vec3(.105,.112,.113),ceiling);
        concrete+=rough+distance_light*vec3(.075,.082,.084);
        float seam=1.0-smoothstep(.008,.023,abs(fract(point.x*.55+.5)-.5));
        concrete*=1.0-seam*.13;
        float glass_joint=exp(-pow((3.2-plane_z)*30.0,2.0));
        chamber=concrete*(1.0-glass_joint*.55);
    }
    return chamber;
}

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
    vec2 model_uv=(p*scale+offset)/screen_size;
    model_uv.y=1.0-model_uv.y;
    vec3 color=chamber_volume(model_uv,screen_uv);

    vec2 ink_uv = (p-vec2(480.0,30.0))/960.0;
    ink_uv.y = 1.0-ink_uv.y;
    if (ink_uv.x > 0.0 && ink_uv.x < 1.0 && ink_uv.y > 0.0 && ink_uv.y < 1.0) {
        vec2 distortion = curl_flow(ink_uv*6.0, time_s*0.5) * 0.0012;
        float density = texture2D(density_tex, ink_uv+distortion).r;
        float goal = ink_target(ink_uv+distortion*0.55);
        float hold = smoothstep(.75,1.0,formation)*(1.0-dispersion);
        if (success_age >= 0.0) hold *= exp(-success_age*5.0);
        density = mix(density, max(density*0.45,goal*1.35), hold*0.18);
        density *= 0.72+0.45*fbm(ink_uv*96.0+time_s*0.12);
        float accepted_ink = success_age < 0.0 ? 1.0 : 1.0-smoothstep(0.0,0.8,success_age);
        density *= accepted_ink*ink_envelope(ink_uv);
        float extinction = 1.0-exp(-density*2.8);
        float halo = texture2D(density_tex,ink_uv+sim_texel*vec2(2,0)).r +
                     texture2D(density_tex,ink_uv-sim_texel*vec2(2,0)).r +
                     texture2D(density_tex,ink_uv+sim_texel*vec2(0,2)).r +
                     texture2D(density_tex,ink_uv-sim_texel*vec2(0,2)).r;
        halo *= accepted_ink*ink_envelope(ink_uv);
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

    float vignette = 1.0-0.16*dot(screen_uv-vec2(0.5),screen_uv-vec2(0.5));
    color *= exposure_value*vec3(0.94,1.0,1.035)*vignette;

    gl_FragColor = vec4(clamp(color,0.0,1.0),1.0);
}
