INSTALL=install
.DEFAULT_GOAL := all
PREFIX=/usr
SYSCONFDIR=/etc
PKG_CONFIG=pkg-config

# Check if pkg-config is installed, we need it for building CFLAGS/LIBS
ifeq ($(shell which $(PKG_CONFIG) 2>/dev/null 1>/dev/null || echo 1),1)
$(error "$(PKG_CONFIG) was not found")
endif

CFLAGS += -std=c99
CFLAGS += -pipe
CFLAGS += -Wall
CPPFLAGS += -D_GNU_SOURCE
CFLAGS += $(shell $(PKG_CONFIG) --cflags cairo xcb-dpms xcb-xinerama xcb-atom xcb-image xcb-xkb xkbcommon xkbcommon-x11 librsvg-2.0 libxml-2.0 )
I3LOCK_LIBS += $(shell $(PKG_CONFIG) --libs cairo xcb-dpms xcb-xinerama xcb-atom xcb-image xcb-xkb xkbcommon xkbcommon-x11 librsvg-2.0 libxml-2.0)
I3LOCK_LIBS += -lpam
I3LOCK_LIBS += -lev
I3LOCK_LIBS += -lm
SCENE_LIBS += $(shell $(PKG_CONFIG) --libs cairo librsvg-2.0 glib-2.0 libxml-2.0)
SCENE_LIBS += -lm
SCENE_PREVIEW_LIBS += $(shell $(PKG_CONFIG) --libs cairo cairo-xcb xcb xkbcommon xkbcommon-x11 librsvg-2.0 glib-2.0 libxml-2.0)
SCENE_PREVIEW_LIBS += -lm

GPU_AVAILABLE := $(shell $(PKG_CONFIG) --exists egl gl && echo yes)
ifeq ($(GPU_AVAILABLE),yes)
CPPFLAGS += -DHAVE_SCENE_GPU
CFLAGS += $(shell $(PKG_CONFIG) --cflags egl gl)
I3LOCK_LIBS += $(shell $(PKG_CONFIG) --libs egl gl)
GPU_OBJS := scene_gpu.o cinema_renderer.o cinema_models.o cinema_volume.o cinema_foreground.o
endif

SCENE_OBJS:=scene.o scene_renderer.o $(GPU_OBJS)
I3LOCK_OBJS:=i3lock.o xcb.o unlock_indicator.o xinerama.o $(SCENE_OBJS)
SCENE_HEADLESS_OBJS:=scene.o scene_renderer_headless.o
SCENE_PACK_LIB_OBJS:=scene_pack_lib.o
SCENE_PREVIEW_OBJS:=scene_preview.o $(SCENE_PACK_LIB_OBJS) $(SCENE_HEADLESS_OBJS)
SCENE_PACK_OBJS:=scene_pack.o $(SCENE_PACK_LIB_OBJS) $(SCENE_HEADLESS_OBJS)
SCENE_TEST_OBJS:=scene_tests.o $(SCENE_PACK_LIB_OBJS) $(SCENE_HEADLESS_OBJS)
SCENE_X11_TEST_OBJS:=scene_x11_tests.o xcb.o $(SCENE_OBJS)

$(sort $(I3LOCK_OBJS) $(SCENE_HEADLESS_OBJS) $(SCENE_PREVIEW_OBJS) $(SCENE_TEST_OBJS) $(SCENE_X11_TEST_OBJS) scene_gpu_preview.o): scene.h scene_renderer.h
scene_gpu.o scene_renderer.o: scene_gpu.h
cinema_renderer.o scene_renderer.o: cinema_renderer.h
cinema_models.o cinema_renderer.o: cinema_models.h
cinema_volume.o cinema_renderer.o: cinema_volume.h
cinema_foreground.o cinema_models.o: cinema_foreground.h
cinema_foreground.o: cinema_volume.h
cinema_renderer.o cinema_preview.o: scene.h scene_renderer.h
cinema_tests.o: scene.h scene_renderer.h

VERSION:=$(shell git describe --tags --abbrev=0 2>/dev/null || printf '0.0.0-dev')
GIT_VERSION:="$(shell git describe --tags --always 2>/dev/null || printf 'unknown') ($(shell git log --pretty=format:%cd --date=short -n1 2>/dev/null || printf 'unknown-date'))"
CPPFLAGS += -DVERSION=\"${GIT_VERSION}\"

.PHONY: install clean uninstall test check xvfb-test container-sanitizer-test local-sanitizer-test

all: i3lock scene-preview scene-pack scene-gpu-preview cinema-preview

i3lock: ${I3LOCK_OBJS}
	$(CC) $(LDFLAGS) -o $@ $^ $(I3LOCK_LIBS)

scene-preview: ${SCENE_PREVIEW_OBJS}
	$(CC) $(LDFLAGS) -o $@ $^ $(SCENE_PREVIEW_LIBS)

scene-pack: ${SCENE_PACK_OBJS}
	$(CC) $(LDFLAGS) -o $@ $^ $(SCENE_LIBS)

scene-tests: ${SCENE_TEST_OBJS}
	$(CC) $(LDFLAGS) -o $@ $^ $(SCENE_LIBS)

scene_renderer_headless.o: scene_renderer.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -DSCENE_RENDERER_NO_X11 -c -o $@ $<

test: scene-tests scene-preview scene-pack
	./scene-tests

check: test
	$(MAKE) xvfb-test

scene-x11-tests: ${SCENE_X11_TEST_OBJS}
	$(CC) $(LDFLAGS) -o $@ $^ $(I3LOCK_LIBS)

xvfb-test: scene-x11-tests scene-preview
	xvfb-run --server-num=200 -a sh -c 'sleep 1; ./scene-x11-tests'
	sh tools/run-preview-window-smoke.sh
	xvfb-run --server-num=201 -a bash tools/run-live-reload-integration.sh

.PHONY: gpu-test
gpu-test: scene-x11-tests
	xvfb-run -a env SCENE_TEST_GPU=1 I3LOCK_GPU_ALLOW_SOFTWARE=1 ./scene-x11-tests

clean:
	rm -f i3lock scene-preview scene-pack scene-tests scene-x11-tests scene-gpu-preview cinema-preview cinema-tests *.o i3lock-${VERSION}.tar.gz

install: all
	$(INSTALL) -d $(DESTDIR)$(PREFIX)/bin
	$(INSTALL) -d $(DESTDIR)$(SYSCONFDIR)/pam.d
	$(INSTALL) -d $(DESTDIR)$(PREFIX)/share/man/man1
	$(INSTALL) -d $(DESTDIR)$(PREFIX)/share/doc/i3lock-svg-nh
	$(INSTALL) -m 755 i3lock $(DESTDIR)$(PREFIX)/bin/i3lock
	$(INSTALL) -m 755 scene-preview $(DESTDIR)$(PREFIX)/bin/scene-preview
	$(INSTALL) -m 755 scene-pack $(DESTDIR)$(PREFIX)/bin/scene-pack
	$(INSTALL) -m 755 cinema-preview $(DESTDIR)$(PREFIX)/bin/cinema-preview
	$(INSTALL) -m 644 i3lock.pam $(DESTDIR)$(SYSCONFDIR)/pam.d/i3lock
	$(INSTALL) -m 644 i3lock.1 $(DESTDIR)$(PREFIX)/share/man/man1/i3lock.1
	$(INSTALL) -m 644 scene-preview.1 $(DESTDIR)$(PREFIX)/share/man/man1/scene-preview.1
	$(INSTALL) -m 644 scene-pack.1 $(DESTDIR)$(PREFIX)/share/man/man1/scene-pack.1
	$(INSTALL) -m 644 docs/scene-v1.md $(DESTDIR)$(PREFIX)/share/doc/i3lock-svg-nh/scene-v1.md

container-sanitizer-test:
	sh tools/run-sanitizers-container.sh

local-sanitizer-test:
	bash tools/run-sanitizers-local.sh

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/i3lock
	rm -f $(DESTDIR)$(PREFIX)/bin/scene-preview
	rm -f $(DESTDIR)$(PREFIX)/bin/scene-pack
	rm -f $(DESTDIR)$(PREFIX)/bin/cinema-preview
	rm -f $(DESTDIR)$(PREFIX)/share/man/man1/i3lock.1
	rm -f $(DESTDIR)$(PREFIX)/share/man/man1/scene-preview.1
	rm -f $(DESTDIR)$(PREFIX)/share/man/man1/scene-pack.1
	rm -f $(DESTDIR)$(PREFIX)/share/doc/i3lock-svg-nh/scene-v1.md

dist: clean
	[ ! -d i3lock-${VERSION} ] || rm -rf i3lock-${VERSION}
	[ ! -e i3lock-${VERSION}.tar.bz2 ] || rm i3lock-${VERSION}.tar.bz2
	mkdir i3lock-${VERSION}
	cp *.c *.h i3lock.1 scene-preview.1 scene-pack.1 i3lock.pam Makefile LICENSE README.md CHANGELOG i3lock-${VERSION}
	mkdir -p i3lock-${VERSION}/docs
	cp docs/scene-v1.md i3lock-${VERSION}/docs/scene-v1.md
	sed -e 's/^GIT_VERSION:=\(.*\)/GIT_VERSION:=$(shell /bin/echo '${GIT_VERSION}' | sed 's/\\/\\\\/g')/g;s/^VERSION:=\(.*\)/VERSION:=${VERSION}/g' Makefile > i3lock-${VERSION}/Makefile
	tar cfj i3lock-${VERSION}.tar.bz2 i3lock-${VERSION}
	rm -rf i3lock-${VERSION}

scene-gpu-preview: scene_gpu_preview.o xcb.o $(SCENE_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(I3LOCK_LIBS)

cinema_preview.o: scene_gpu_preview.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -DCINEMA_PREVIEW_DEFAULT -c -o $@ $<

cinema-preview: cinema_preview.o xcb.o $(SCENE_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(I3LOCK_LIBS)

cinema-tests: cinema_tests.o xcb.o $(SCENE_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(I3LOCK_LIBS)

.PHONY: cinema-test
cinema-test: cinema-tests
	xvfb-run -a env I3LOCK_GPU_ALLOW_SOFTWARE=1 ./cinema-tests
