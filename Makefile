APP=cyber-glass-term
PREFIX=/usr/local

CFLAGS=`pkg-config --cflags gtk+-3.0 vte-2.91`
LIBS=`pkg-config --libs gtk+-3.0 vte-2.91 cairo gdk-3.0` -lm

all:
	gcc src/main.c -o $(APP) $(CFLAGS) $(LIBS)

install:
	install -Dm755 $(APP) $(DESTDIR)$(PREFIX)/bin/$(APP)
	install -Dm644 desktop/cyber-glass-term.desktop \
		$(DESTDIR)/usr/share/applications/cyber-glass-term.desktop
	install -Dm644 icons/cyber-glass-term.png \
		$(DESTDIR)/usr/share/icons/hicolor/256x256/apps/cyber-glass-term.png

uninstall:
	rm -f $(PREFIX)/bin/$(APP)
	rm -f /usr/share/applications/cyber-glass-term.desktop
	rm -f /usr/share/icons/hicolor/256x256/apps/cyber-glass-term.png

clean:
	rm -f $(APP)
