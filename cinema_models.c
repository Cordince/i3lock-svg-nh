/* Articulated, tessellated 3D creatures. Coordinates are world-space metres;
 * the camera looks along +Z and the glass is in front of the near hand. */
#define GL_GLEXT_PROTOTYPES
#include "cinema_models.h"
#include <GL/gl.h>
#include <GL/glext.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <glib.h>

typedef struct { float x,y,z; } vec3;
typedef struct { vec3 position, normal; } vertex;
enum { RINGS=36, SIDES=24, CAPACITY=600000, SOURCE_CAPACITY=20000 };
typedef struct { vertex *vertices; size_t count; } sculpt;
struct cinema_models {
    GLuint program, buffer, framebuffer, color, depth;
    unsigned width, height;
    vertex *vertices;
    size_t count;
    sculpt torso, hand;
};
static vec3 add(vec3 a,vec3 b) { return (vec3){a.x+b.x,a.y+b.y,a.z+b.z}; }
static vec3 mul(vec3 a,float b) { return (vec3){a.x*b,a.y*b,a.z*b}; }
static vec3 cross(vec3 a,vec3 b) { return (vec3){a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
static vec3 unit(vec3 a) { return mul(a,1.0f/fmaxf(.00001f,sqrtf(a.x*a.x+a.y*a.y+a.z*a.z))); }
static float smooth(float a,float b,float x) { float t=fmaxf(0,fminf(1,(x-a)/(b-a)));return t*t*(3-2*t); }
static vec3 curve(vec3 a,vec3 b,vec3 c,vec3 d,float t) {
    float u=1-t;
    return add(add(mul(a,u*u*u),mul(b,3*u*u*t)),add(mul(c,3*u*t*t),mul(d,t*t*t)));
}
static void triangle(cinema_models_t *m,vertex a,vertex b,vertex c) {
    if(m->count+3>CAPACITY) return;
    m->vertices[m->count++]=a;m->vertices[m->count++]=b;m->vertices[m->count++]=c;
}
/* The OBJ assets hold authored cross sections and geometric chest/tendon folds,
 * independently inspectable in a modeller. Animation bends that rest geometry. */
static bool load_sculpt(const char *directory,const char *name,sculpt *mesh,char error[SCENE_ERROR_LEN]) {
    char path[4096];
    if(snprintf(path,sizeof(path),"%s/../models/%s.obj",directory,name)>=(int)sizeof(path)) {
        snprintf(error,SCENE_ERROR_LEN,"3D model path is too long");return false;
    }
    FILE *file=fopen(path,"r");
    if(!file) {snprintf(error,SCENE_ERROR_LEN,"cannot load 3D sculpt %.180s",path);return false;}
    vec3 *positions=calloc(SOURCE_CAPACITY,sizeof(vec3)),*normals=calloc(SOURCE_CAPACITY,sizeof(vec3));
    mesh->vertices=calloc(150000,sizeof(vertex));
    size_t pc=0,nc=0;bool ok=positions&&normals&&mesh->vertices;char line[512];
    while(ok&&fgets(line,sizeof(line),file)) {
        if(!strncmp(line,"v ",2)) {
            if(pc==SOURCE_CAPACITY||sscanf(line+2,"%f %f %f",&positions[pc].x,&positions[pc].y,&positions[pc].z)!=3) {ok=false;break;}
            pc++;
        } else if(!strncmp(line,"vn ",3)) {
            if(nc==SOURCE_CAPACITY||sscanf(line+3,"%f %f %f",&normals[nc].x,&normals[nc].y,&normals[nc].z)!=3) {ok=false;break;}
            nc++;
        } else if(!strncmp(line,"f ",2)) {
            unsigned p[3],n[3];
            if(mesh->count+3>150000||sscanf(line+2,"%u//%u %u//%u %u//%u",p,n,p+1,n+1,p+2,n+2)!=6) {ok=false;break;}
            for(int i=0;i<3;i++) {
                if(!p[i]||p[i]>pc||!n[i]||n[i]>nc) {ok=false;break;}
                vertex v={positions[p[i]-1],normals[n[i]-1]};
                if(!isfinite(v.position.x)||!isfinite(v.position.y)||!isfinite(v.position.z)||
                   !isfinite(v.normal.x)||!isfinite(v.normal.y)||!isfinite(v.normal.z)) {ok=false;break;}
                mesh->vertices[mesh->count++]=v;
            }
        }
    }
    if(ferror(file))ok=false;
    fclose(file);free(positions);free(normals);
    if(!ok||!mesh->count) {snprintf(error,SCENE_ERROR_LEN,"invalid triangulated 3D sculpt %.160s",path);return false;}
    return true;
}
static vec3 rotate_y(vec3 v,float angle) {
    return (vec3){v.x*cosf(angle)+v.z*sinf(angle),v.y,-v.x*sinf(angle)+v.z*cosf(angle)};
}
static void pose_sculpt(cinema_models_t *m,const sculpt *mesh,vec3 origin,float yaw,float breath,float sway) {
    for(size_t i=0;i<mesh->count;i++) {
        vertex v=mesh->vertices[i];
        if(mesh==&m->torso) { v.position.y*=1.40f; v.normal.y/=1.40f; }
        v.position.x*=breath;
        v.normal.x/=breath;
        v.position.x+=sway*v.position.y*v.position.y*.02f;
        v.normal.y-=sway*v.position.y*.04f*v.normal.x;
        v.position=add(rotate_y(v.position,yaw),origin);
        v.normal=unit(rotate_y(v.normal,yaw));
        if(m->count<CAPACITY)m->vertices[m->count++]=v;
    }
}
/* Parallel ring frames follow the cubic skeleton, preserving round cross
 * sections under bends. Each limb has actual front/back surfaces and normals. */
static void tube(cinema_models_t *m,vec3 a,vec3 b,vec3 c,vec3 d,float radius,float tip,float clock) {
    vertex rings[RINGS+1][SIDES+1];
    for(int i=0;i<=RINGS;i++) {
        float t=(float)i/RINGS;
        vec3 p=curve(a,b,c,d,t);
        vec3 tangent=unit(add(curve(a,b,c,d,fminf(1,t+.002)),mul(curve(a,b,c,d,fmaxf(0,t-.002)),-1)));
        vec3 side=unit(cross(tangent,(vec3){0,0,1}));
        if(fabsf(tangent.z)>.95f) side=unit(cross(tangent,(vec3){0,1,0}));
        vec3 up=unit(cross(side,tangent));
        // Elephantine root, distinct flexor bulge, a narrowed wrist, and
        // transverse skin folds rather than a uniformly shrinking cone.
        float r=radius*powf(1-t,.85f)+tip*t;
        r*=1+.17f*expf(-powf((t-.35f)*7,2))-.13f*expf(-powf((t-.78f)*12,2));
        r*=1+.006f*sinf(t*110+.5f*sinf(clock));
        for(int j=0;j<=SIDES;j++) {
            float angle=(float)j/SIDES*2*M_PI;
            vec3 normal=add(mul(side,cosf(angle)),mul(up,sinf(angle)));
            float tendon=1+.018f*cosf(angle*7+t*3);
            rings[i][j]=(vertex){add(p,mul(normal,r*tendon)),normal};
        }
    }
    for(int i=0;i<RINGS;i++) for(int j=0;j<SIDES;j++) {
        triangle(m,rings[i][j],rings[i+1][j],rings[i][j+1]);
        triangle(m,rings[i][j+1],rings[i+1][j],rings[i+1][j+1]);
    }
    /* Close the attachment and rounded distal tip. */
    for(int j=0;j<SIDES;j++) {
        triangle(m,(vertex){a,unit(add(a,mul(b,-1)))},rings[0][j+1],rings[0][j]);
        triangle(m,(vertex){d,unit(add(d,mul(c,-1)))},rings[RINGS][j],rings[RINGS][j+1]);
    }
}
static void heptapod(cinema_models_t *m,float x,float z,float t,float seed) {
    float sway=sinf(t*.16f+seed)*.16f,breath=1+.018f*sinf(t*.33f+seed);
    vec3 body={x+sway,.12f+sinf(t*.22f+seed)*.07f,z};
    float yaw=.16f*sinf(t*.09f+seed)+(seed>1 ? -.30f : .22f);
    pose_sculpt(m,&m->torso,body,yaw,breath,sway);
    for(int leg=0;leg<7;leg++) {
        float angle=leg*2*M_PI/7+yaw;
        float dx=cosf(angle),dz=sinf(angle),move=sinf(t*.23f+leg+seed)*.13f;
        float length=3.0f+.22f*sinf(leg*1.7f+seed);
        vec3 a=add(body,(vec3){dx*.32f,.15f,dz*.32f});
        vec3 b=add(body,(vec3){dx*.76f,-.45f,dz*.72f});
        vec3 c=add(body,(vec3){dx*1.35f+move,-1.55f,dz*1.10f});
        vec3 d=add(body,(vec3){dx*1.20f+move,-length,dz*1.24f});
        tube(m,a,b,c,d,.25f,.018f,t*.2f+leg);
    }
}
static void hand(cinema_models_t *m,float t,float age,float success_age) {
    /* Wind-up, fast forward strike, contact, then a damped recoil. */
    float hit=0,wind=0;
    if(age>=0) {
        wind=smooth(0,.18f,age)*(1-smooth(.18f,.29f,age));
        hit=smooth(.18f,.32f,age)*(1-smooth(.43f,1.35f,age));
    }
    float sway=.075f*sinf(t*.31f);
    vec3 palm={-.72f+sway,.33f+.06f*sinf(t*.27f),3.2f+.38f*wind-1.28f*hit};
    float retreat=success_age < 0 ? 0 : smooth(0,.95f,success_age);
    palm.y += retreat*2.5f;
    palm.z += retreat*2.0f;
    pose_sculpt(m,&m->hand,palm,-.12f+.06f*sinf(t*.21f),1,0);
    float opening=.27f+.13f*sinf(t*.22f)+hit*.60f;
    for(int finger=0;finger<7;finger++) {
        float angle=2*M_PI*finger/7+.16f;
        vec3 radial={cosf(angle),sinf(angle),0};
        float bend=.065f*sinf(t*.37f+finger*1.2f);
        float length=1.05f+.16f*sinf(finger*2.7f);
        float spread=radial.x*opening;
        vec3 a=add(palm,mul(radial,.27f));
        vec3 b=add(palm,(vec3){spread*.55f,-.40f*(1-opening)+radial.y*.48f*opening,-.07f});
        vec3 joint=add(palm,(vec3){spread*.94f+bend,(-.95f*(1-opening)+radial.y*.94f*opening)*length,-.16f+.10f*(1-hit)});
        tube(m,a,b,add(joint,(vec3){-spread*.1f,.13f,-.03f}),joint,.095f,.045f,t*.2f+finger);
        vec3 end=add(palm,(vec3){spread*1.05f+bend,(-1.47f*(1-opening)+radial.y*1.12f*opening)*length,.05f+.31f*(1-hit)});
        tube(m,joint,add(joint,(vec3){spread*.10f,-.12f,-.05f}),
             add(end,(vec3){spread*.05f,.12f,-.10f}),end,.046f,.006f,t*.2f+finger);
    }
}
static GLuint shader(GLenum kind,const char *source,char error[SCENE_ERROR_LEN]) {
    GLuint s=glCreateShader(kind);glShaderSource(s,1,&source,NULL);glCompileShader(s);
    GLint ok=0;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok) {glGetShaderInfoLog(s,SCENE_ERROR_LEN,NULL,error);glDeleteShader(s);return 0;}
    return s;
}
cinema_models_t *cinema_models_create(const char *shader_directory,char error[SCENE_ERROR_LEN]) {
    cinema_models_t *m=calloc(1,sizeof(*m));
    if(!m) {snprintf(error,SCENE_ERROR_LEN,"cannot allocate 3D models");return NULL;}
    m->vertices=calloc(CAPACITY,sizeof(vertex));
    if(!m->vertices) {cinema_models_destroy(m);snprintf(error,SCENE_ERROR_LEN,"cannot allocate 3D mesh");return NULL;}
    if(!load_sculpt(shader_directory,"heptapod-torso",&m->torso,error)||
       !load_sculpt(shader_directory,"heptapod-hand",&m->hand,error)) {cinema_models_destroy(m);return NULL;}
    char *vp=g_build_filename(shader_directory,"model.vert",NULL),*fp=g_build_filename(shader_directory,"model.frag",NULL);
    char *vs=NULL,*fs=NULL;
    bool loaded=g_file_get_contents(vp,&vs,NULL,NULL)&&g_file_get_contents(fp,&fs,NULL,NULL);
    g_free(vp);g_free(fp);
    if(!loaded) {g_free(vs);g_free(fs);cinema_models_destroy(m);snprintf(error,SCENE_ERROR_LEN,"cannot load model shaders");return NULL;}
    GLuint v=shader(GL_VERTEX_SHADER,vs,error),f=shader(GL_FRAGMENT_SHADER,fs,error);
    g_free(vs);g_free(fs);
    if(!v||!f) {if(v)glDeleteShader(v);if(f)glDeleteShader(f);cinema_models_destroy(m);return NULL;}
    m->program=glCreateProgram();glAttachShader(m->program,v);glAttachShader(m->program,f);
    glBindAttribLocation(m->program,0,"position");glBindAttribLocation(m->program,1,"normal");
    glLinkProgram(m->program);glDeleteShader(v);glDeleteShader(f);
    GLint ok=0;glGetProgramiv(m->program,GL_LINK_STATUS,&ok);
    if(!ok) {glGetProgramInfoLog(m->program,SCENE_ERROR_LEN,NULL,error);cinema_models_destroy(m);return NULL;}
    glGenBuffers(1,&m->buffer);glGenFramebuffers(1,&m->framebuffer);
    glGenTextures(1,&m->color);glGenRenderbuffers(1,&m->depth);
    return m;
}
unsigned cinema_models_render(cinema_models_t *m,unsigned width,unsigned height,double seconds,double age,double success_age,char error[SCENE_ERROR_LEN]) {
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,m->color);
    glBindFramebuffer(GL_FRAMEBUFFER,m->framebuffer);
    if(width!=m->width||height!=m->height) {
        m->width=width;m->height=height;
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glBindRenderbuffer(GL_RENDERBUFFER,m->depth);glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,width,height);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,m->color,0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,m->depth);
    }
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) {snprintf(error,SCENE_ERROR_LEN,"3D model framebuffer incomplete");return 0;}
    glViewport(0,0,width,height);glClearColor(0,0,0,0);glClearDepth(1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LESS);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);
    m->count=0;heptapod(m,-3.0f,7.8f,seconds,.4f);heptapod(m,3.5f,9.5f,seconds,2.1f);hand(m,seconds,age,success_age);
    glUseProgram(m->program);glUniform1f(glGetUniformLocation(m->program,"aspect"),(float)width/height);
    glBindBuffer(GL_ARRAY_BUFFER,m->buffer);glBufferData(GL_ARRAY_BUFFER,m->count*sizeof(vertex),m->vertices,GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);glEnableVertexAttribArray(1);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(vertex),(void*)offsetof(vertex,position));
    glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,sizeof(vertex),(void*)offsetof(vertex,normal));
    glDrawArrays(GL_TRIANGLES,0,m->count);
    glDisableVertexAttribArray(0);glDisableVertexAttribArray(1);glDisable(GL_DEPTH_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER,0);glUseProgram(0);
    return m->color;
}
void cinema_models_destroy(cinema_models_t *m) {
    if(!m)return;
    glDeleteProgram(m->program);glDeleteBuffers(1,&m->buffer);glDeleteTextures(1,&m->color);
    glDeleteRenderbuffers(1,&m->depth);glDeleteFramebuffers(1,&m->framebuffer);
    free(m->torso.vertices);free(m->hand.vertices);free(m->vertices);free(m);
}
