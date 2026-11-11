#version 120
varying vec3 world;
varying vec3 norm;
uniform float time_s;

float hash3(vec3 p) { return fract(sin(dot(p,vec3(127.1,311.7,74.7)))*43758.5453); }
float noise3(vec3 p) {
    vec3 i=floor(p),f=fract(p);f=f*f*(3.0-2.0*f);
    return mix(mix(mix(hash3(i),hash3(i+vec3(1,0,0)),f.x),
                   mix(hash3(i+vec3(0,1,0)),hash3(i+vec3(1,1,0)),f.x),f.y),
               mix(mix(hash3(i+vec3(0,0,1)),hash3(i+vec3(1,0,1)),f.x),
                   mix(hash3(i+vec3(0,1,1)),hash3(i+vec3(1,1,1)),f.x),f.y),f.z);
}
float hide(vec3 p) {
    float coarse=noise3(p*3.7);
    float creases=pow(abs(sin(p.y*52.0+noise3(p*7.0)*5.0)),12.0);
    return coarse*.55+noise3(p*22.0)*.18+noise3(p*95.0)*.035-creases*.04;
}
void main() {
    vec3 n=normalize(norm);
    float height=hide(world);
    // Screen-space surface gradients perturb the geometric normal without
    // replacing the sculpt's major muscle, tendon and fold forms.
    vec3 dx=dFdx(world),dy=dFdy(world);
    vec3 r1=cross(dy,n),r2=cross(n,dx);
    float det=dot(dx,r1);
    vec3 gradient=(dFdx(height)*r1+dFdy(height)*r2)*sign(det)/max(abs(det),0.00001);
    n=normalize(n-gradient*.012);
    vec3 light=normalize(vec3(-3.5,5.5,-3.0)-world);
    vec3 eye=normalize(-world);
    float diffuse=max(dot(n,light),0.0);
    float fill=max(dot(n,normalize(vec3(3.0,1.0,1.0))),0.0);
    float rim=pow(1.0-abs(dot(n,eye)),3.0);
    vec3 albedo=mix(vec3(.075,.085,.082),vec3(.19,.205,.19),height);
    vec3 skin=albedo*(.30+diffuse*1.55+fill*.22);
    skin+=pow(max(dot(reflect(-light,n),eye),0.0),22.0)*.075;
    skin+=rim*vec3(.085,.10,.11);
    // Opaque geometry with a real depth attachment. The compositor integrates
    // the fog along the camera ray in front of and behind this surface.
    gl_FragColor=vec4(skin,1.0);
}
