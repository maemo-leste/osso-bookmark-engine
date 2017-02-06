prefix = /usr
libdir = $(prefix)/lib
incdir = $(prefix)/include
pkgconfdir = $(libdir)/pkgconfig

PKGDEPS = glib-2.0 gconf-2.0 libxml-2.0
CFLAGS += `pkg-config --cflags $(PKGDEPS)` -fPIC -Wall -O2
LDFLAGS += `pkg-config --libs-only-L $(PKGDEPS)`
LDLIBS += `pkg-config --libs-only-l --libs-only-other $(PKGDEPS)`

LIBS=libbookmarkengine.la

%.lo: src/%.c
	libtool --mode=compile $(CC) $(CFLAGS) $(CPPFLAGS) -c $<

libbookmarkengine.la: bookmark_parser.lo
	libtool --mode=link --tag=CC $(CC) $(LDFLAGS) -rpath $(libdir) -o $@ $^ $(LDLIBS)

install/%.la: %.la
	install -d $(DESTDIR)$(libdir)
	libtool --mode=install install -c $(notdir $@) $(DESTDIR)$(libdir)/$(notdir $@)
install: $(addprefix install/,$(LIBS))
	libtool --mode=finish $(libdir)
	install -d $(DESTDIR)$(incdir)
	install -d $(DESTDIR)$(pkgconfdir)
	install src/osso_bookmark_parser.h $(DESTDIR)$(incdir)
	install osso-bookmark-engine.pc $(DESTDIR)$(pkgconfdir)

clean:
	rm -rf *.lo *.la .libs
