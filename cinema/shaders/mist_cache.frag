void main() {
    float depth=(mist_slice+.5)*14.0/64.0;
    vec2 ndc=uv*2.0-1.0;
    vec3 ray=vec3(ndc.x*(screen_size.x/screen_size.y)/1.35,ndc.y/1.35,1.0);
    vec3 position=ray*depth;
    float density=mist_density(position);
    gl_FragColor=vec4(mist_lighting(position,density,ray),density);
}
