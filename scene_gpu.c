/* Cached premultiplied SVG textures; no SVG or pixel uploads in the frame loop. */
#include "scene_gpu.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
struct scene_gpu {
    EGLDisplay display;
    EGLContext context;
    EGLSurface surface;
};
void scene_gpu_destroy(scene_gpu_t *g) {
    if (!g)
        return;
    if (g->display != EGL_NO_DISPLAY) {
        eglMakeCurrent(g->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (g->surface != EGL_NO_SURFACE)
            eglDestroySurface(g->display, g->surface);
        if (g->context != EGL_NO_CONTEXT)
            eglDestroyContext(g->display, g->context);
        eglTerminate(g->display);
    }
    free(g);
}
scene_gpu_t *scene_gpu_create(xcb_connection_t *conn, xcb_screen_t *screen, xcb_window_t win, char error[SCENE_ERROR_LEN]) {
    scene_gpu_t *g = calloc(1, sizeof(*g));
    if (!g) {
        snprintf(error, SCENE_ERROR_LEN, "could not allocate GPU renderer");
        return NULL;
    }
    PFNEGLGETPLATFORMDISPLAYEXTPROC get_display = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    PFNEGLCREATEPLATFORMWINDOWSURFACEEXTPROC create_surface = (PFNEGLCREATEPLATFORMWINDOWSURFACEEXTPROC)eglGetProcAddress("eglCreatePlatformWindowSurfaceEXT");
    if (!get_display || !create_surface)
        goto fail;
    g->display = get_display(EGL_PLATFORM_XCB_EXT, conn, NULL);
    if (g->display == EGL_NO_DISPLAY || !eglInitialize(g->display, NULL, NULL) || !eglBindAPI(EGL_OPENGL_API))
        goto fail;
    EGLint attrs[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE};
    EGLConfig configs[128], config = NULL;
    EGLint count = 0;
    if (!eglChooseConfig(g->display, attrs, configs, 128, &count))
        goto fail;
    for (int i = 0; i < count; ++i) {
        EGLint visual;
        if (eglGetConfigAttrib(g->display, configs[i], EGL_NATIVE_VISUAL_ID, &visual) &&
            (xcb_visualid_t)visual == screen->root_visual) {
            config = configs[i];
            break;
        }
    }
    if (!config)
        goto fail;
    g->context = eglCreateContext(g->display, config, EGL_NO_CONTEXT, NULL);
    g->surface = create_surface(g->display, config, &win, NULL);
    if (g->context == EGL_NO_CONTEXT || g->surface == EGL_NO_SURFACE ||
        !eglMakeCurrent(g->display, g->surface, g->surface, g->context))
        goto fail;
    const char *device = (const char *)glGetString(GL_RENDERER);
    if (!device)
        goto fail;
    if ((strstr(device, "llvmpipe") || strstr(device, "softpipe") || strstr(device, "Software")) && !getenv("I3LOCK_GPU_ALLOW_SOFTWARE")) {
        snprintf(error, SCENE_ERROR_LEN, "software OpenGL (%s); use hardware acceleration or Cairo", device);
        scene_gpu_destroy(g);
        return NULL;
    }
    fprintf(stderr, "[scene-gpu] OpenGL renderer: %s\n", device);
    eglSwapInterval(g->display, 0); /* The application timer owns pacing. */
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    return g;
fail:
    snprintf(error, SCENE_ERROR_LEN, "could not initialize EGL/XCB OpenGL (EGL 0x%x)", eglGetError());
    scene_gpu_destroy(g);
    return NULL;
}
unsigned scene_gpu_upload(scene_gpu_t *g, cairo_surface_t *surface) {
    (void)g;
    if (!surface)
        return 0;
    cairo_surface_flush(surface);
    int width = cairo_image_surface_get_width(surface);
    int height = cairo_image_surface_get_height(surface);
    if (!width || !height)
        return 0;
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, cairo_image_surface_get_stride(surface) / 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE,
                 cairo_image_surface_get_data(surface));
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    if (glGetError() != GL_NO_ERROR) {
        glDeleteTextures(1, &texture);
        return 0;
    }
    return texture;
}
void scene_gpu_begin(scene_gpu_t *g, unsigned w, unsigned h) {
    (void)g;
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, w, h, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
}
void scene_gpu_draw(scene_gpu_t *g, unsigned texture, double x, double y, double w, double h, const scene_layer_style_t *s, double sx, double sy, double fade) {
    (void)g;
    if (!texture || !s->visible || s->opacity <= 0)
        return;
    glBlendFunc(GL_ONE, s->blend == SCENE_BLEND_ADD ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
    glBindTexture(GL_TEXTURE_2D, texture);
    double alpha = s->opacity * fade;
    glColor4d(alpha, alpha, alpha, alpha);
    glPushMatrix();
    glTranslated(s->origin_x + s->translate_x * sx, s->origin_y + s->translate_y * sy, 0);
    glRotated(s->rotation_deg, 0, 0, 1);
    glScaled(s->scale_x, s->scale_y, 1);
    glTranslated(-s->origin_x, -s->origin_y, 0);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0);
    glVertex2d(x, y);
    glTexCoord2f(1, 0);
    glVertex2d(x + w, y);
    glTexCoord2f(1, 1);
    glVertex2d(x + w, y + h);
    glTexCoord2f(0, 1);
    glVertex2d(x, y + h);
    glEnd();
    glPopMatrix();
}
bool scene_gpu_present(scene_gpu_t *g) {
    return glGetError() == GL_NO_ERROR && eglSwapBuffers(g->display, g->surface);
}
