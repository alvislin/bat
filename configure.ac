AC_PREREQ([2.69])

AC_INIT([bat],[m4_esyscmd(./git-version-gen .tarball-version)],[liam.r.girdwood (at) intel (dot) com])

AC_CONFIG_MACRO_DIR([m4])

# libtool versioning
BAT_MAJOR_VERSION=0
BAT_MINOR_VERSION=1
BAT_MICRO_VERSION=0
BAT_VERSION=$BAT_MAJOR_VERSION.$BAT_MINOR_VERSION.$BAT_MICRO_VERSION
AC_SUBST(BAT_MAJOR_VERSION)
AC_SUBST(BAT_MINOR_VERSION)
AC_SUBST(BAT_MICRO_VERSION)
AC_SUBST(BAT_VERSION)

LT_VERSION=$BAT_MICRO_VERSION
LT_RELEASE=$BAT_MAJOR_VERSION.$BAT_MINOR_VERSION
AC_SUBST(LT_VERSION)
AC_SUBST(LT_RELEASE)

VERSION=$BAT_VERSION
PACKAGE=BAT

AM_INIT_AUTOMAKE([foreign 1.11 -Wall -Wno-portability silent-rules tar-pax])

# Specify a configuration file
AC_CONFIG_SRCDIR([config.h.in])
AC_CONFIG_HEADERS([config.h])

dnl Initialize libtool
LT_PREREQ(2.2)
LT_INIT

dnl Initialize maintainer mode
AM_MAINTAINER_MODE([enable])

CFLAGS="-Wall"

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL

# Checks for libraries.

# Checks for header files.
AC_CHECK_HEADERS([stdint.h stdlib.h string.h unistd.h pthread.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_INLINE
AC_TYPE_SIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_CHECK_FUNCS([memset], , [AC_MSG_ERROR([Error: need memset])])
AC_CHECK_LIB([fftw3], [fftw_malloc], , [AC_MSG_ERROR([Error: need FFTW3 library])])
AC_CHECK_LIB([m], [sqrt], , [AC_MSG_ERROR([Error: Need sqrt])])
AC_CHECK_LIB([asound], [snd_pcm_open], , [AC_MSG_ERROR([Error: need ASOUND library])])
AC_CHECK_LIB([pthread], [pthread_create], , [AC_MSG_ERROR([Error: need PTHREAD library])])

# Debug symbol support
AC_ARG_ENABLE(debug, [AS_HELP_STRING([--enable-debug],[enable debug symbols])],
	have_debug=$enableval, have_debug=no)
if test "$have_debug" = "yes"; then
        AC_DEFINE(HAVE_DEBUG,1,[Define to enable debug symbols.])
        CFLAGS+=" -g"
else
	CFLAGS+=" -O3"
fi

AC_CONFIG_FILES([
Makefile
src/Makefile
])

AC_OUTPUT

