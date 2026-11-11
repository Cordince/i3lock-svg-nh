/* Non-locking GPU integration preview. No PAM, grabs, or password storage. */
#include "scene_renderer.h"
#include <xcb/xcb.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-x11.h>
#include <poll.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static int64_t millis(void) {
    struct timespec timestamp;
    clock_gettime(CLOCK_MONOTONIC, &timestamp);
    return timestamp.tv_sec * 1000LL + timestamp.tv_nsec / 1000000;
}
int main(int argc, char **argv) {
#ifdef CINEMA_PREVIEW_DEFAULT
    const bool native_cinema = true;
#else
    const bool native_cinema = false;
#endif
    if (argc < 2) {
        fprintf(stderr, "Usage: %s scene-file [--smoke|--cairo|--idle] [--seconds N] [--snapshot file.png]\n"
                        "Typing: react; Enter: verify; F: fail; S: success; Escape: quit\n", argv[0]);
        return 2;
    }
    bool smoke=false, gpu=true, idle=false;
    int seconds=0;
    const char *snapshot_path = NULL;
    int snapshot_at_ms = 5000;
    for(int i=2;i<argc;++i) {
        if (!strcmp(argv[i],"--smoke")) {smoke=true;seconds=3;}
        else if (!strcmp(argv[i],"--cairo")) gpu=false;
        else if (!strcmp(argv[i],"--idle")) idle=true;
        else if (!strcmp(argv[i],"--seconds") && i+1<argc) seconds=atoi(argv[++i]);
        else if (!strcmp(argv[i],"--snapshot") && i+1<argc) snapshot_path=argv[++i];
        else if (!strcmp(argv[i],"--snapshot-at-ms") && i+1<argc) snapshot_at_ms=atoi(argv[++i]);
        else return 2;
    }
    if (native_cinema && !gpu) {
        fprintf(stderr, "Native cinema requires the GPU renderer\n");
        return 2;
    }
    int screen_number;
    xcb_connection_t *c=xcb_connect(NULL,&screen_number);
    if(xcb_connection_has_error(c)) {fprintf(stderr,"XCB connection error %d\n",xcb_connection_has_error(c));return 2;}
    xcb_screen_iterator_t it=xcb_setup_roots_iterator(xcb_get_setup(c));
    for(int i=0;i<screen_number;++i) xcb_screen_next(&it);
    xcb_screen_t *screen=it.data;
    unsigned w=960,h=540;
    xcb_window_t win=xcb_generate_id(c);
    uint32_t values[]={screen->black_pixel,XCB_EVENT_MASK_EXPOSURE|XCB_EVENT_MASK_KEY_PRESS|XCB_EVENT_MASK_STRUCTURE_NOTIFY};
    xcb_create_window(c,screen->root_depth,win,screen->root,0,0,w,h,0,XCB_WINDOW_CLASS_INPUT_OUTPUT,screen->root_visual,XCB_CW_BACK_PIXEL|XCB_CW_EVENT_MASK,values);
    const char *title = native_cinema ? "Native cinema - Enter verify / F fail / S success / Esc quit" :
        "Arrival GPU preview - Enter verify / F fail / S success / Esc quit";
    xcb_change_property(c,XCB_PROP_MODE_REPLACE,win,XCB_ATOM_WM_NAME,XCB_ATOM_STRING,8,strlen(title),title);
    xcb_intern_atom_reply_t *protocols=xcb_intern_atom_reply(c,xcb_intern_atom(c,0,12,"WM_PROTOCOLS"),NULL);
    xcb_intern_atom_reply_t *delete=xcb_intern_atom_reply(c,xcb_intern_atom(c,0,16,"WM_DELETE_WINDOW"),NULL);
    if(protocols && delete) xcb_change_property(c,XCB_PROP_MODE_REPLACE,win,protocols->atom,XCB_ATOM_ATOM,32,1,&delete->atom);
    xcb_map_window(c,win);xcb_flush(c);
    char error[SCENE_ERROR_LEN];scene_renderer_t *r=NULL;
    bool created = native_cinema ?
        scene_renderer_create_cinema(argv[1], c, screen, w, h, &r, error) :
        scene_renderer_create_x11(argv[1], c, screen, w, h, &r, error);
    if (!created || (gpu && !scene_renderer_enable_gpu(r,error))) {fprintf(stderr,"%s\n",error);return 2;}
    scene_renderer_set_window(r,win);
    struct xkb_context *ctx=xkb_context_new(0);
    uint16_t major,minor;uint8_t base,err;
    if(!xkb_x11_setup_xkb_extension(c,1,0,0,&major,&minor,&base,&err)) {fprintf(stderr,"XKB extension unavailable\n");return 2;}
    int device=xkb_x11_get_core_keyboard_device_id(c);
    struct xkb_keymap *map=xkb_x11_keymap_new_from_device(ctx,c,device,0);
    struct xkb_state *keys=map?xkb_x11_state_new_from_device(map,c,device):NULL;
    if(!keys) {fprintf(stderr,"XKB device unavailable: %d\n",device);return 2;}
    int64_t start=millis(),busy=0,reset=0;
    bool done=false;int frames=0;int result=0;
    scene_renderer_set_state(r,SCENE_STATE_LOCKED,0);
    while(!done) {
        int64_t t=millis()-start;
        xcb_generic_event_t *e;
        while((e=xcb_poll_for_event(c))) {
            int type=e->response_type&127;
            if(type==XCB_CLIENT_MESSAGE && delete && ((xcb_client_message_event_t*)e)->data.data32[0]==delete->atom) done=true;
            if(type==XCB_CONFIGURE_NOTIFY) {
                xcb_configure_notify_event_t *ce=(void*)e;
                if (w != ce->width || h != ce->height) {
                    w = ce->width;
                    h = ce->height;
                    if (!scene_renderer_resize(r, w, h, error)) {
                        fprintf(stderr, "%s\n", error);
                        done = true;
                        result = 2;
                    }
                }
            }
            if(type==XCB_KEY_PRESS) {
                xkb_keysym_t sym=xkb_state_key_get_one_sym(keys,((xcb_key_press_event_t*)e)->detail);
                if(sym==XKB_KEY_Escape) done=true;
                else if (sym == XKB_KEY_Return) {
                    scene_renderer_set_state(r, SCENE_STATE_VERIFY, t);
                    reset = t + 1600;
                } else if (sym == XKB_KEY_f || sym == XKB_KEY_F) {
                    scene_renderer_set_state(r, SCENE_STATE_FAIL, t);
                    reset = t + 1200;
                } else if (sym == XKB_KEY_s || sym == XKB_KEY_S) {
                    if (native_cinema)
                        scene_renderer_set_state(r, SCENE_STATE_LOCKED, t);
                    scene_renderer_success(r, t);
                    reset = t + (native_cinema ? 1600 : 650);
                } else {
                    scene_renderer_set_state(r, SCENE_STATE_INPUT, t);
                    scene_renderer_trigger(r, sym == XKB_KEY_BackSpace ? SCENE_TRIGGER_BACKSPACE : SCENE_TRIGGER_KEY, t);
                    reset = t + 1500;
                }
                busy=t+900;
            }
            free(e);
        }
        if(smoke) {
            if(frames==8) scene_renderer_trigger(r,SCENE_TRIGGER_KEY,t);
            if(frames==18) scene_renderer_set_state(r,SCENE_STATE_VERIFY,t);
            if(frames==32) scene_renderer_set_state(r,SCENE_STATE_FAIL,t);
            if(frames==48) scene_renderer_success(r,t);
        }
        if (reset && t > reset) {
            scene_renderer_success(r, -1);
            scene_renderer_set_state(r, SCENE_STATE_LOCKED, t);
            reset = 0;
        }
        if(!scene_renderer_render(r,t,error)) {fprintf(stderr,"%s\n",error);result=2;break;}
        if (snapshot_path && t >= snapshot_at_ms) {
            if (!scene_renderer_gpu_snapshot(r, snapshot_path, t, error)) {
                fprintf(stderr, "%s\n", error);
                result = 2;
                break;
            }
            snapshot_path = NULL;
        }
        ++frames;
        if(seconds && t>=seconds*1000) break;
        if(xcb_connection_has_error(c)) {result=2;break;}
        int idle_fps = scene_renderer_document(r)->idle_fps;
        int interval = t < busy && !idle ? 17 : (1000 + idle_fps / 2) / idle_fps;
        int spent=(int)(millis()-start-t);
        if(interval>spent) poll(NULL,0,interval-spent);
    }
    fprintf(stderr,"preview: frames=%d gpu=%s elapsed-ms=%lld\n",frames,scene_renderer_gpu_active(r)?"yes":"no",(long long)(millis()-start));
    if(smoke && gpu && !scene_renderer_gpu_active(r)) result=3;
    scene_renderer_destroy(r);
    xkb_state_unref(keys);
    xkb_keymap_unref(map);
    xkb_context_unref(ctx);
    free(protocols);
    free(delete);
    xcb_destroy_window(c, win);
    xcb_disconnect(c);
    return result;
}
