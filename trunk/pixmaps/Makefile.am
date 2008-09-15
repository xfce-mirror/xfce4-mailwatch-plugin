pngdir = $(datadir)/icons/hicolor/48x48/apps
png_DATA = xfce-newmail.png xfce-nomail.png

svgdir = $(datadir)/icons/hicolor/scalable/apps
svg_DATA = xfce-newmail.svg xfce-nomail.svg

EXTRA_DIST = $(png_DATA) $(svg_DATA)

gtk_update_icon_cache = gtk-update-icon-cache -f -t $(datadir)/icons/hicolor

install-data-hook:
	@-if test -z "$(DESTDIR)"; then \
		echo "Updating Gtk icon cache."; \
		$(gtk_update_icon_cache); \
	else \
		echo "*** Icon cache not updated.  Remember to run:"; \
		echo "***"; \
		echo "***   $(gtk_update_icon_cache)"; \
		echo "***"; \
	fi
