/* Articulated, tessellated 3D creatures. Coordinates are world-space metres;
 * the camera looks along +Z and the glass is in front of the near hand. */
#define GL_GLEXT_PROTOTYPES
#include "cinema_models.h"
#include "cinema_foreground.h"
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
    GLuint foreground_framebuffer,foreground_color,foreground_depth;
    unsigned width, height;
    vertex *vertices;
    size_t count;
    sculpt torso;
    cinema_foreground_t *foreground;
};
static vec3 add(vec3 a,vec3 b) { return (vec3){a.x+b.x,a.y+b.y,a.z+b.z}; }
static vec3 mul(vec3 a,float b) { return (vec3){a.x*b,a.y*b,a.z*b}; }
static vec3 cross(vec3 a,vec3 b) { return (vec3){a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
static vec3 unit(vec3 a) { return mul(a,1.0f/fmaxf(.00001f,sqrtf(a.x*a.x+a.y*a.y+a.z*a.z))); }
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
    // Derive normals from the deformed surface, including taper, flexor
    // bulges and skin folds. Pure radial normals made every bend look tubular.
    for(int i=0;i<=RINGS;i++) for(int j=0;j<=SIDES;j++) {
        int before=(j+SIDES-1)%SIDES,after=(j+1)%SIDES;
        vec3 along=add(rings[i<RINGS?i+1:i][j].position,mul(rings[i?i-1:i][j].position,-1));
        vec3 around=add(rings[i][after].position,mul(rings[i][before].position,-1));
        rings[i][j].normal=unit(cross(along,around));
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
    // Root/elbow/distal landmarks from the clear writing-room silhouette in
    // Paramount's contact clip (148 s). The seven limbs occupy different depth
    // planes; outside elbows are flexed while the middle limbs hang longer.
    static const vec3 rest[7][3]={
        {{-.37f,.14f,-.22f},{-1.08f,-.35f,-.42f},{-1.62f,-2.45f,-.58f}},
        {{-.22f,.04f,-.39f},{-.63f,-.72f,-.92f},{-.76f,-3.15f,-1.0f}},
        {{.08f,.01f,-.41f},{.17f,-.93f,-1.05f},{.30f,-3.36f,-1.16f}},
        {{.34f,.08f,-.25f},{1.12f,-.63f,-.48f},{1.43f,-2.81f,-.32f}},
        {{.34f,.08f,.24f},{.83f,-.54f,.73f},{.94f,-2.75f,1.03f}},
        {{.02f,.02f,.39f},{.12f,-.88f,1.0f},{-.08f,-3.12f,1.20f}},
        {{-.32f,.10f,.25f},{-.81f,-.50f,.67f},{-.97f,-2.84f,.98f}}
    };
    for(int leg=0;leg<7;leg++) {
        float move=sinf(t*.18f+leg*1.7f+seed)*.075f;
        vec3 a=add(body,rotate_y(rest[leg][0],yaw));
        vec3 elbow=add(body,rotate_y(add(rest[leg][1],(vec3){move,.025f*sinf(t*.2f+leg),0}),yaw));
        vec3 tip=add(body,rotate_y(add(rest[leg][2],(vec3){move*1.7f,0,.045f*sinf(t*.21f+leg)}),yaw));
        vec3 upper=add(elbow,mul(a,-1)),lower=add(tip,mul(elbow,-1));
        tube(m,a,add(a,mul(upper,.40f)),add(elbow,mul(upper,-.22f)),elbow,.27f,.165f,t*.15f+leg);
        tube(m,elbow,add(elbow,mul(lower,.22f)),add(tip,add(mul(lower,-.26f),(vec3){move,0,-.12f})),tip,.17f,.012f,t*.15f+leg);
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
    if(!load_sculpt(shader_directory,"heptapod-torso",&m->torso,error)) {cinema_models_destroy(m);return NULL;}
    m->foreground=cinema_foreground_create(shader_directory,error);
    if(!m->foreground) {cinema_models_destroy(m);return NULL;}
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
    glGenTextures(1,&m->color);glGenTextures(1,&m->depth);
    glGenFramebuffers(1,&m->foreground_framebuffer);
    glGenTextures(1,&m->foreground_color);glGenTextures(1,&m->foreground_depth);
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
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,m->color,0);
        glBindTexture(GL_TEXTURE_2D,m->depth);
        glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT24,width,height,0,GL_DEPTH_COMPONENT,GL_UNSIGNED_INT,NULL);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_COMPARE_MODE,GL_NONE);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,m->depth,0);
        glBindFramebuffer(GL_FRAMEBUFFER,m->foreground_framebuffer);
        glBindTexture(GL_TEXTURE_2D,m->foreground_color);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,m->foreground_color,0);
        glBindTexture(GL_TEXTURE_2D,m->foreground_depth);
        glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT24,width,height,0,GL_DEPTH_COMPONENT,GL_UNSIGNED_INT,NULL);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_COMPARE_MODE,GL_NONE);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,m->foreground_depth,0);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) {snprintf(error,SCENE_ERROR_LEN,"foreground framebuffer incomplete");return 0;}
        glBindFramebuffer(GL_FRAMEBUFFER,m->framebuffer);
    }
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) {snprintf(error,SCENE_ERROR_LEN,"3D model framebuffer incomplete");return 0;}
    glViewport(0,0,width,height);glClearColor(0,0,0,0);glClearDepth(1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LESS);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);
    m->count=0;heptapod(m,-3.0f,7.8f,seconds,.4f);heptapod(m,3.5f,9.5f,seconds,2.1f);
    glUseProgram(m->program);glUniform1f(glGetUniformLocation(m->program,"aspect"),(float)width/height);
    glUniform1f(glGetUniformLocation(m->program,"time_s"),seconds);
    glBindBuffer(GL_ARRAY_BUFFER,m->buffer);glBufferData(GL_ARRAY_BUFFER,m->count*sizeof(vertex),m->vertices,GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);glEnableVertexAttribArray(1);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(vertex),(void*)offsetof(vertex,position));
    glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,sizeof(vertex),(void*)offsetof(vertex,normal));
    glDrawArrays(GL_TRIANGLES,0,m->count);
    glBindFramebuffer(GL_FRAMEBUFFER,m->foreground_framebuffer);
    glClearColor(0,0,0,0);glClearDepth(1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    // Keep fractional coverage premultiplied in its own layer. Background
    // geometry remains available behind the foreground's optical footprint.
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
    if(!cinema_foreground_render(m->foreground,width,height,seconds,age,success_age,error))return 0;
    glDisable(GL_BLEND);
    glDisableVertexAttribArray(0);glDisableVertexAttribArray(1);glDisable(GL_DEPTH_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER,0);glUseProgram(0);
    return m->color;
}
void cinema_models_destroy(cinema_models_t *m) {
    if(!m)return;
    glDeleteProgram(m->program);glDeleteBuffers(1,&m->buffer);glDeleteTextures(1,&m->color);
    glDeleteTextures(1,&m->depth);glDeleteFramebuffers(1,&m->framebuffer);
    glDeleteTextures(1,&m->foreground_color);glDeleteTextures(1,&m->foreground_depth);glDeleteFramebuffers(1,&m->foreground_framebuffer);
    cinema_foreground_destroy(m->foreground);
    free(m->torso.vertices);free(m->vertices);free(m);
}
unsigned cinema_models_depth_texture(const cinema_models_t *m) {
    return m ? m->depth : 0;
}
unsigned cinema_models_foreground_texture(const cinema_models_t *m) {return m ? m->foreground_color : 0;}
unsigned cinema_models_foreground_depth(const cinema_models_t *m) {return m ? m->foreground_depth : 0;}
void cinema_models_emitter(double seconds,double failure_age,double success_age,float point[3],float *release) {
    cinema_foreground_emitter(seconds,failure_age,success_age,point,release);
}
