AC_INIT([modbus_client], [0.1])

AC_CONFIG_AUX_DIR([config])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADERS([config.h])

AM_INIT_AUTOMAKE
AM_SILENT_RULES([yes])

LT_INIT

# Dependencies go here
AC_PROG_CC

AC_SEARCH_LIBS([pthread_create], [pthread], [],
    AC_MSG_ERROR([No pthreads implementation found]))

PKG_CHECK_MODULES([MODBUS], [libmodbus])
PKG_CHECK_MODULES([BACNET], [libbacnet])

AC_CONFIG_FILES([
    Makefile
    scripts/Makefile
    src/Makefile
])

AC_OUTPUT
