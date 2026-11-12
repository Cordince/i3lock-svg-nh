// Coherent cellular lobes are advected as volumes. Thin depth-local banks
// retain their structure along the ray instead of averaging into a flat fade.
float mist_density(vec3 position) {
    float middle=smoothstep(5.6,7.2,position.z);
    float rear=smoothstep(9.0,10.8,position.z);
    float frequency=mix(mix(.57,.39,middle),.27,rear);
    vec3 drift=mix(mix(vec3(.045,-.012,.019),vec3(-.028,.020,.015),middle),
                   vec3(.014,.010,-.008),rear);
    vec3 q=position*frequency+drift*time_s;
    vec3 warp=texture3D(volume_noise_tex,(q*.55+vec3(3.1,7.7,time_s*.009))/16.0).rgb-.5;
    q+=warp*1.35;
    // Divergence-free analytic cross-flow rolls the lobes while they translate.
    q+=.28*vec3(sin(q.y*1.4+time_s*.16)*cos(q.z*.8),
                sin(q.z*1.1-time_s*.12)*cos(q.x*.9),
                sin(q.x*1.2+time_s*.10)*cos(q.y*.7));
    vec4 base=texture3D(volume_noise_tex,q/16.0);
    vec4 detail=texture3D(volume_noise_tex,(q*2.7+vec3(6.7,-time_s*.012,1.9))/16.0);
    vec4 filaments=texture3D(volume_noise_tex,(q*7.1+vec3(-time_s*.021,2.3,5.8))/16.0);
    float shape=base.a*.50+base.r*.32+detail.g*.18;
    float lobes=smoothstep(.40,.64,shape);
    // Erode the cloud boundary, leaving translucent folds and channels.
    lobes=max(0.0,lobes-(1.0-detail.b)*.10-(1.0-filaments.a)*.055);
    lobes*=.80+.40*filaments.b;
    float front_bank=exp(-pow((position.z-4.4)/.82,2.0));
    float middle_bank=exp(-pow((position.z-7.5)/1.12,2.0));
    float rear_bank=exp(-pow((position.z-11.3)/1.45,2.0));
    float lower=1.0-smoothstep(-1.8,.6,position.y+warp.y*.9);
    float upper=smoothstep(1.3,4.2,position.y+warp.x);
    float density=lobes*(front_bank*.95+middle_bank*1.30+rear_bank*1.55);
    density*=.75+.65*lower+.40*upper;
    density+=.012+lower*.025;
    return density*smoothstep(1.7,3.8,position.z);
}
vec3 mist_lighting(vec3 position,float density,vec3 ray) {
    vec3 light_direction=normalize(vec3(-.35,.80,.50));
    // Two light-path samples shade the cloud's interior. Dense folds cast
    // shadows onto the volume behind them, making depth visible in a still.
    float nearby=mist_density(position+light_direction*.65);
    float behind=mist_density(position+light_direction*1.9);
    float light_transmission=exp(-nearby*.75-behind*1.25);
    float sky=smoothstep(-1.4,3.8,position.y);
    vec3 ambient=mix(vec3(.035,.055,.070),vec3(.16,.18,.19),sky);
    vec3 key=mix(vec3(.20,.25,.29),vec3(1.0,1.02,1.0),sky);
    float phase=.70+.45*pow(max(dot(light_direction,normalize(ray)),0.0),4.0);
    float powder=1.0-exp(-density*2.0);
    vec3 scattered=ambient+key*light_transmission*phase*(.85+.15*powder);
    return scattered;
}
