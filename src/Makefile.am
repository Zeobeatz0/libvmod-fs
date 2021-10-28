AM_CFLAGS = $(VARNISHAPI_CFLAGS) -Wall -Werror
AM_LDFLAGS = $(VARNISHAPI_LIBS) $(VMOD_LDFLAGS)

vmod_LTLIBRARIES = libvmod_fs.la

libvmod_fs_la_SOURCES = \
	vmod_fs.c

nodist_libvmod_fs_la_SOURCES = \
	vcc_fs_if.c \
	vcc_fs_if.h

dist_man_MANS = vmod_fs.3

@BUILD_VMOD_FS@

AM_TESTS_ENVIRONMENT = \
	PATH="$(VMOD_TEST_PATH)" \
	LD_LIBRARY_PATH="$(VARNISH_LIBRARY_PATH)"
TEST_EXTENSIONS = .vtc
VTC_LOG_COMPILER = varnishtest -v
AM_VTC_LOG_FLAGS = -Dvmod_fs="$(VMOD_FS)"

TESTS = @VMOD_TESTS@

EXTRA_DIST = \
	vmod_fs.vcc \
	$(VMOD_TESTS)
