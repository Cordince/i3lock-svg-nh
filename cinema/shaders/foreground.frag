#version 120
uniform sampler3D folded_sculpt;
uniform sampler3D writing_sculpt;
uniform sampler3D contact_sculpt;
uniform sampler3D skin_noise;
uniform vec2 screen_size;
uniform vec3 bounds_low,bounds_high,origin;
uniform float yaw,writing,contact;

vec3 local_point(vec3 p) {
    p-=origin;
    float twist=yaw*(1.0-smoothstep(.55,2.3,p.y));
    float c=cos(twist),s=sin(twist);
    return vec3(c*p.x-s*p.z,p.y,s*p.x+c*p.z);
}
float surface(vec3 world) {
    vec3 p=local_point(world);
    vec3 outside=max(max(bounds_low-p,p-bounds_high),vec3(0));
    if(dot(outside,outside)>0.0) return length(outside)+.015;
    vec3 uv=(p-bounds_low)/(bounds_high-bounds_low);
    float a=texture3D(folded_sculpt,uv).r;
    float b=texture3D(writing_sculpt,uv).r;
    float c=texture3D(contact_sculpt,uv).r;
    return mix(mix(a,b,writing),c,contact);
}
bool trace(vec3 ray,out vec3 p,out float depth) {
    vec3 inverse=1.0/ray;
    float radius=length(max(abs(bounds_low.xz),abs(bounds_high.xz)));
    vec3 a=(origin+vec3(-radius,bounds_low.y,-radius))*inverse;
    vec3 b=(origin+vec3(radius,bounds_high.y,radius))*inverse;
    vec3 near=min(a,b),far=max(a,b);
    float start=max(.15,max(near.x,max(near.y,near.z)));
    float finish=min(14.0,min(far.x,min(far.y,far.z)));
    if(finish<=start)return false;
    float t=start;
    for(int i=0;i<112;i++) {
        p=ray*t;
        float d=surface(p);
        if(d<.0025) {depth=t;return true;}
        t+=max(.002,d*.62)/length(ray);
        if(t>finish)break;
    }
    return false;
}
vec3 surface_normal(vec3 p) {
    float e=.026;
    return normalize(vec3(surface(p+vec3(e,0,0))-surface(p-vec3(e,0,0)),
                          surface(p+vec3(0,e,0))-surface(p-vec3(0,e,0)),
                          surface(p+vec3(0,0,e))-surface(p-vec3(0,0,e))));
}
float skin_height(vec3 world) {
    vec3 p=local_point(world);
    vec4 coarse=texture3D(skin_noise,p*4.0/16.0);
    vec4 fine=texture3D(skin_noise,p*27.0/16.0);
    float pores=texture3D(skin_noise,p*110.0/16.0).b;
    float fold=pow(1.0-abs(sin(p.y*54.0+coarse.g*12.0)),10.0);
    float carpal=exp(-pow((p.y-.85)*2.0,2.0));
    return coarse.r*.40+fine.g*.16+pores*.035-fold*carpal*.035;
}
vec3 shade(vec3 p,vec3 n) {
    vec3 local=local_point(p);
    float e=.007;
    vec3 gradient=vec3(skin_height(p+vec3(e,0,0))-skin_height(p-vec3(e,0,0)),
                       skin_height(p+vec3(0,e,0))-skin_height(p-vec3(0,e,0)),
                       skin_height(p+vec3(0,0,e))-skin_height(p-vec3(0,0,e)))/(2.0*e);
    n=normalize(n-(gradient-n*dot(gradient,n))*.035);
    float grain=texture3D(skin_noise,local*41.0/16.0).g;
    float mottle=texture3D(skin_noise,local*5.0/16.0).r;
    vec3 light=normalize(vec3(-2.5,5.5,-1.5)-p);
    vec3 eye=normalize(-p);
    // Broad wrapped light and a soft backlight model the large diffused
    // source in the film; no hard point-light terminator or metallic rim.
    float wrapped=clamp((dot(n,light)+.45)/1.45,0.0,1.0);
    float sky=.5+.5*n.y;
    vec3 albedo=mix(vec3(.085,.102,.108),vec3(.205,.227,.235),mottle);
    albedo*=.86+.23*grain;
    float occlusion=clamp(surface(p+n*.11)/.11,.45,1.0);
    vec3 color=albedo*(.32+.82*wrapped+.22*sky)*occlusion;
    float spec=max(dot(n,normalize(light+eye)),0.0);
    color+=pow(spec,18.0)*.028;
    float grazing=pow(1.0-max(dot(n,eye),0.0),3.0);
    color+=grazing*vec3(.025,.036,.042)*sky;
    return color;
}
void main() {
    vec3 positions=vec3(0),normals=vec3(0);float coverage=0,depth=30;
    // Four subpixel rays produce fractional silhouette coverage. Normals
    // come from the continuous field, not from separately capped sections.
    for(int i=0;i<4;i++) {
        vec2 offset=vec2(mod(float(i),2.0),floor(float(i)/2.0))*.5-.25;
        vec2 ndc=(gl_FragCoord.xy+offset)/screen_size*2.0-1.0;
        vec3 ray=vec3(ndc.x*(screen_size.x/screen_size.y)/1.65,ndc.y/1.65,1.0),p;
        float t;
        if(trace(ray,p,t)) {positions+=p;normals+=surface_normal(p);coverage+=1.0;depth=min(depth,t);}
    }
    if(coverage<.5)discard;
    gl_FragColor=vec4(shade(positions/coverage,normalize(normals)),coverage*.25);
    gl_FragDepth=(30.0-3.0/depth)/29.9;
}
