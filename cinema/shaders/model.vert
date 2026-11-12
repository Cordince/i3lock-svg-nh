#version 120
attribute vec3 position;
attribute vec3 normal;
uniform float aspect;
varying vec3 world;
varying vec3 norm;
void main() {
    world=position;
    norm=normal;
    float z=position.z;
    gl_Position=vec4(position.x*1.35/aspect,position.y*1.35,
                     (30.1/29.9)*z-6.0/29.9,z);
}
