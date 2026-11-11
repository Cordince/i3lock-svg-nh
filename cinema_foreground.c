#define GL_GLEXT_PROTOTYPES
#include "cinema_foreground.h"
#include "cinema_volume.h"
#include <GL/gl.h>
#include <GL/glext.h>
#include <glib.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct cinema_foreground { GLuint textures[3],skin,program,quad; float low[3],high[3]; };
static uint32_t integer(const char *p) {uint32_t v;memcpy(&v,p,4);return GUINT32_FROM_LE(v);}
static float scalar(const char *p) {uint32_t v=integer(p);float f;memcpy(&f,&v,4);return f;}
static GLuint compile(GLenum type,const char *source,char error[SCENE_ERROR_LEN]) {
    GLuint s=glCreateShader(type);glShaderSource(s,1,&source,NULL);glCompileShader(s);
    GLint ok;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok) {glGetShaderInfoLog(s,SCENE_ERROR_LEN,NULL,error);glDeleteShader(s);return 0;}
    return s;
}
cinema_foreground_t *cinema_foreground_create(const char *shaders,char error[SCENE_ERROR_LEN]) {
    cinema_foreground_t *f=calloc(1,sizeof(*f));if(!f) {snprintf(error,SCENE_ERROR_LEN,"cannot allocate foreground sculpt");return NULL;}
    const char *names[]={"foreground-folded.sdf","foreground-writing.sdf","foreground-contact.sdf"};
    for(int i=0;i<3;i++) {
        char *path=g_build_filename(shaders,"..","models",names[i],NULL),*bytes=NULL;gsize size=0;
        bool ok=g_file_get_contents(path,&bytes,&size,NULL);g_free(path);
        if(!ok||size<44||memcmp(bytes,"HPSDF01\0",8)) {g_free(bytes);snprintf(error,SCENE_ERROR_LEN,"missing or invalid foreground sculpt %s",names[i]);goto failed;}
        unsigned nx=integer(bytes+8),ny=integer(bytes+12),nz=integer(bytes+16);
        if(nx<8||ny<8||nz<8||nx>512||ny>512||nz>512||size!=44+(size_t)nx*ny*nz*2) {g_free(bytes);snprintf(error,SCENE_ERROR_LEN,"invalid foreground volume dimensions");goto failed;}
        for(int axis=0;axis<3;axis++) {
            float low=scalar(bytes+20+axis*4),high=scalar(bytes+32+axis*4);
            if(!isfinite(low)||!isfinite(high)||high<=low||(i&&(low!=f->low[axis]||high!=f->high[axis]))) {g_free(bytes);snprintf(error,SCENE_ERROR_LEN,"foreground sculpt bounds disagree");goto failed;}
            f->low[axis]=low;f->high[axis]=high;
        }
        glActiveTexture(GL_TEXTURE0+i);glGenTextures(1,f->textures+i);glBindTexture(GL_TEXTURE_3D,f->textures[i]);
        glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_WRAP_R,GL_CLAMP_TO_EDGE);
        glTexImage3D(GL_TEXTURE_3D,0,GL_R16F,nx,ny,nz,0,GL_RED,GL_HALF_FLOAT,bytes+44);g_free(bytes);
    }
    glActiveTexture(GL_TEXTURE3);
    f->skin=cinema_volume_create_noise(error);if(!f->skin)goto failed;
    char *vp=g_build_filename(shaders,"foreground.vert",NULL),*fp=g_build_filename(shaders,"foreground.frag",NULL),*vs=NULL,*fs=NULL;
    bool loaded=g_file_get_contents(vp,&vs,NULL,NULL)&&g_file_get_contents(fp,&fs,NULL,NULL);g_free(vp);g_free(fp);
    if(!loaded) {g_free(vs);g_free(fs);snprintf(error,SCENE_ERROR_LEN,"cannot load foreground shaders");goto failed;}
    GLuint v=compile(GL_VERTEX_SHADER,vs,error),s=compile(GL_FRAGMENT_SHADER,fs,error);g_free(vs);g_free(fs);
    if(!v||!s) {if(v)glDeleteShader(v);if(s)glDeleteShader(s);goto failed;}
    f->program=glCreateProgram();glAttachShader(f->program,v);glAttachShader(f->program,s);glBindAttribLocation(f->program,0,"position");glLinkProgram(f->program);glDeleteShader(v);glDeleteShader(s);
    GLint linked;glGetProgramiv(f->program,GL_LINK_STATUS,&linked);if(!linked) {glGetProgramInfoLog(f->program,SCENE_ERROR_LEN,NULL,error);goto failed;}
    const GLfloat quad[]={-1,-1,1,-1,-1,1,1,1};glGenBuffers(1,&f->quad);glBindBuffer(GL_ARRAY_BUFFER,f->quad);glBufferData(GL_ARRAY_BUFFER,sizeof(quad),quad,GL_STATIC_DRAW);
    if(glGetError()!=GL_NO_ERROR) {snprintf(error,SCENE_ERROR_LEN,"could not upload foreground 3D sculpts");goto failed;}
    return f;
failed: cinema_foreground_destroy(f);return NULL;
}
static float smooth(float a,float b,float value) {float x=fmaxf(0,fminf(1,(value-a)/(b-a)));return x*x*(3-2*x);}
bool cinema_foreground_render(cinema_foreground_t *f,unsigned width,unsigned height,double seconds,double failure_age,double success_age,char error[SCENE_ERROR_LEN]) {
    float hit=0,wind=0;
    if(failure_age>=0) {wind=smooth(0,.18f,failure_age)*(1-smooth(.18f,.29f,failure_age));hit=smooth(.18f,.32f,failure_age)*(1-smooth(.43f,1.35f,failure_age));}
    float phase=fmod(seconds,16),writing=smooth(.6f,2.4f,phase)*(1-smooth(6,11,phase));
    float unfurl=smooth(.8f,1.8f,phase)*(1-smooth(2.3f,4.5f,phase));
    float retreat=success_age<0 ? 0 : smooth(0,.95f,success_age);
    float origin[]={-.72f+.075f*sinf(seconds*.31f),.33f+.06f*sinf(seconds*.27f)+retreat*2.5f,3.2f+.38f*wind-1.28f*hit+retreat*2};
    float yaw=(-1.12f+.43f*writing+.055f*sinf(seconds*.21f))*(1-hit*.94f);
    glUseProgram(f->program);
    const char *samplers[]={"folded_sculpt","writing_sculpt","contact_sculpt"};
    for(int i=0;i<3;i++) {glActiveTexture(GL_TEXTURE0+i);glBindTexture(GL_TEXTURE_3D,f->textures[i]);glUniform1i(glGetUniformLocation(f->program,samplers[i]),i);}
    glActiveTexture(GL_TEXTURE3);glBindTexture(GL_TEXTURE_3D,f->skin);glUniform1i(glGetUniformLocation(f->program,"skin_noise"),3);
    glUniform2f(glGetUniformLocation(f->program,"screen_size"),width,height);
    glUniform3fv(glGetUniformLocation(f->program,"bounds_low"),1,f->low);glUniform3fv(glGetUniformLocation(f->program,"bounds_high"),1,f->high);
    glUniform3fv(glGetUniformLocation(f->program,"origin"),1,origin);glUniform1f(glGetUniformLocation(f->program,"yaw"),yaw);
    glUniform1f(glGetUniformLocation(f->program,"writing"),unfurl);glUniform1f(glGetUniformLocation(f->program,"contact"),hit);
    glBindBuffer(GL_ARRAY_BUFFER,f->quad);glEnableVertexAttribArray(0);glDisableVertexAttribArray(1);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,NULL);
    glDrawArrays(GL_TRIANGLE_STRIP,0,4);glDisableVertexAttribArray(0);glUseProgram(0);
    if(glGetError()!=GL_NO_ERROR) {snprintf(error,SCENE_ERROR_LEN,"foreground sculpt render failed");return false;}return true;
}
void cinema_foreground_destroy(cinema_foreground_t *f) {if(!f)return;glDeleteTextures(3,f->textures);glDeleteTextures(1,&f->skin);glDeleteProgram(f->program);glDeleteBuffers(1,&f->quad);free(f);}
