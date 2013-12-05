
ACLOCAL_AMFLAGS = -I m4

EXTRA_DIST = \
	git-version-gen \
	README

SUBDIRS = src

# see git-version-gen
BUILT_SOURCES = $(top_srcdir)/.version
$(top_srcdir)/.version:
	echo $(VERSION) > $@-t && mv $@-t $@

dist-hook:
	echo $(VERSION) > $(distdir)/.tarball-version
	echo "VERSION " $(VERSION)
