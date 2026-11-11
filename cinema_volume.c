#define GL_GLEXT_PROTOTYPES
#include "cinema_volume.h"
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

enum { SIDE=64, CELLS=16 };
static float lattice(int x,int y,int z,uint32_t seed) {
    uint32_t h=((uint32_t)x&15u)*0x8da6b343u ^ ((uint32_t)y&15u)*0xd8163841u ^
               ((uint32_t)z&15u)*0xcb1ab31fu ^ seed;
    h^=h>>16;h*=0x7feb352du;h^=h>>15;h*=0x846ca68bu;h^=h>>16;
    return (h&0xffffffu)/16777215.0f;
}
static float fade(float t) {return t*t*t*(t*(t*6-15)+10);}
static float blend(float a,float b,float t) {return a+(b-a)*t;}
static float coherent(float x,float y,float z,uint32_t seed) {
    int ix=floorf(x),iy=floorf(y),iz=floorf(z);
    float fx=fade(x-ix),fy=fade(y-iy),fz=fade(z-iz),v[2];
    for(int k=0;k<2;k++) {
        float a=blend(lattice(ix,iy,iz+k,seed),lattice(ix+1,iy,iz+k,seed),fx);
        float b=blend(lattice(ix,iy+1,iz+k,seed),lattice(ix+1,iy+1,iz+k,seed),fx);
        v[k]=blend(a,b,fy);
    }
    return blend(v[0],v[1],fz);
}
static float cellular(float x,float y,float z) {
    int ix=floorf(x),iy=floorf(y),iz=floorf(z);float distance=4;
    for(int k=-1;k<=1;k++)for(int j=-1;j<=1;j++)for(int i=-1;i<=1;i++) {
        int cx=ix+i,cy=iy+j,cz=iz+k;
        float dx=cx+.12f+.76f*lattice(cx,cy,cz,0x91a1u)-x;
        float dy=cy+.12f+.76f*lattice(cx,cy,cz,0x71b3u)-y;
        float dz=cz+.12f+.76f*lattice(cx,cy,cz,0x53e9u)-z;
        distance=fminf(distance,dx*dx+dy*dy+dz*dz);
    }
    return 1-fminf(1,sqrtf(distance)/1.15f);
}
unsigned cinema_volume_create_noise(char error[SCENE_ERROR_LEN]) {
    GLint maximum=0;glGetIntegerv(GL_MAX_3D_TEXTURE_SIZE,&maximum);
    if(maximum<SIDE) {snprintf(error,SCENE_ERROR_LEN,"3D mist texture unsupported");return 0;}
    unsigned char *data=malloc(SIDE*SIDE*SIDE*4);
    if(!data) {snprintf(error,SCENE_ERROR_LEN,"cannot allocate 3D mist field");return 0;}
    for(int z=0;z<SIDE;z++)for(int y=0;y<SIDE;y++)for(int x=0;x<SIDE;x++) {
        float px=(x+.5f)*CELLS/SIDE,py=(y+.5f)*CELLS/SIDE,pz=(z+.5f)*CELLS/SIDE;
        unsigned char *p=data+((z*SIDE+y)*SIDE+x)*4;
        p[0]=coherent(px,py,pz,0x4131u)*255;
        p[1]=coherent(px,py,pz,0x63e9u)*255;
        p[2]=coherent(px,py,pz,0x9b17u)*255;
        p[3]=cellular(px,py,pz)*255;
    }
    GLuint texture=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_3D,texture);
    glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_WRAP_S,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_WRAP_T,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_WRAP_R,GL_REPEAT);
    glTexImage3D(GL_TEXTURE_3D,0,GL_RGBA8,SIDE,SIDE,SIDE,0,GL_RGBA,GL_UNSIGNED_BYTE,data);
    free(data);
    if(glGetError()!=GL_NO_ERROR) {
        glDeleteTextures(1,&texture);snprintf(error,SCENE_ERROR_LEN,"cannot upload 3D mist field");return 0;
    }
    return texture;
}
