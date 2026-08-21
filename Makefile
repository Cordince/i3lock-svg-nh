INSTALL=install
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

SCENE_OBJS:=scene.o scene_renderer.o
I3LOCK_OBJS:=i3lock.o xcb.o unlock_indicator.o xinerama.o $(SCENE_OBJS)
SCENE_HEADLESS_OBJS:=scene.o scene_renderer_headless.o
SCENE_PREVIEW_OBJS:=scene_preview.o $(SCENE_HEADLESS_OBJS)
SCENE_TEST_OBJS:=scene_tests.o $(SCENE_HEADLESS_OBJS)
SCENE_X11_TEST_OBJS:=scene_x11_tests.o xcb.o $(SCENE_OBJS)

VERSION:=$(shell git describe --tags --abbrev=0)
GIT_VERSION:="$(shell git describe --tags --always) ($(shell git log --pretty=format:%cd --date=short -n1))"
CPPFLAGS += -DVERSION=\"${GIT_VERSION}\"

.PHONY: install clean uninstall test xvfb-test

all: i3lock scene-preview

i3lock: ${I3LOCK_OBJS}
	$(CC) $(LDFLAGS) -o $@ $^ $(I3LOCK_LIBS)

scene-preview: ${SCENE_PREVIEW_OBJS}
	$(CC) $(LDFLAGS) -o $@ $^ $(SCENE_LIBS)

scene-tests: ${SCENE_TEST_OBJS}
	$(CC) $(LDFLAGS) -o $@ $^ $(SCENE_LIBS)

scene_renderer_headless.o: scene_renderer.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -DSCENE_RENDERER_NO_X11 -c -o $@ $<

test: scene-tests scene-preview
	./scene-tests

scene-x11-tests: ${SCENE_X11_TEST_OBJS}
	$(CC) $(LDFLAGS) -o $@ $^ $(I3LOCK_LIBS)

xvfb-test: scene-x11-tests
	xvfb-run -a ./scene-x11-tests

clean:
	rm -f i3lock scene-preview scene-tests scene-x11-tests *.o i3lock-${VERSION}.tar.gz

install: all
	$(INSTALL) -d $(DESTDIR)$(PREFIX)/bin
	$(INSTALL) -d $(DESTDIR)$(SYSCONFDIR)/pam.d
	$(INSTALL) -m 755 i3lock $(DESTDIR)$(PREFIX)/bin/i3lock
	$(INSTALL) -m 755 scene-preview $(DESTDIR)$(PREFIX)/bin/scene-preview
	$(INSTALL) -m 644 i3lock.pam $(DESTDIR)$(SYSCONFDIR)/pam.d/i3lock

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/i3lock
	rm -f $(DESTDIR)$(PREFIX)/bin/scene-preview

dist: clean
	[ ! -d i3lock-${VERSION} ] || rm -rf i3lock-${VERSION}
	[ ! -e i3lock-${VERSION}.tar.bz2 ] || rm i3lock-${VERSION}.tar.bz2
	mkdir i3lock-${VERSION}
	cp *.c *.h i3lock.1 i3lock.pam Makefile LICENSE README.md CHANGELOG i3lock-${VERSION}
	sed -e 's/^GIT_VERSION:=\(.*\)/GIT_VERSION:=$(shell /bin/echo '${GIT_VERSION}' | sed 's/\\/\\\\/g')/g;s/^VERSION:=\(.*\)/VERSION:=${VERSION}/g' Makefile > i3lock-${VERSION}/Makefile
	tar cfj i3lock-${VERSION}.tar.bz2 i3lock-${VERSION}
	rm -rf i3lock-${VERSION}
