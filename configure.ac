# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.61)
AC_INIT([xmount], [0.4.5], [bugs@pinguin.lu])
AM_INIT_AUTOMAKE(@PACKAGE_NAME@, @PACKAGE_VERSION@)
AC_CONFIG_SRCDIR([xmount.c])
AC_CONFIG_HEADER([config.h])

# Checks for programs.
AC_PROG_CC
AM_PROG_CC_C_O

# Checks for libraries.
#AC_CHECK_LIB([fuse],[fuse_main],,AC_MSG_ERROR([No FUSE library found! Get it from http://fuse.sourceforge.net/]))
#AC_CHECK_LIB([crypto],[SHA1_Init],,AC_MSG_ERROR([No CRYPTO library found! Get it from http://www.openssl.org/]))
AC_CHECK_LIB([ewf],[libewf_open],,AC_MSG_WARN([No EWF library found! EWF input support will be disabled.]))
AC_CHECK_LIB([afflib],[af_open],,AC_MSG_WARN([No AFF library found! AFF input support will be disabled.]))

# Checks for header files.
AC_CHECK_HEADER([stdio.h],,AC_MSG_ERROR([No stdio.h header file found!]))
AC_CHECK_HEADER([string.h],,AC_MSG_ERROR([No string.h header file found!]))
AC_CHECK_HEADER([fcntl.h],,AC_MSG_ERROR([No fcntl.h header file found!]))
AC_CHECK_HEADER([errno.h],,AC_MSG_ERROR([No errno.h header file found!]))
#AC_CHECK_HEADER([inttypes.h],,AC_MSG_ERROR([No inttypes.h header file found!]))
AC_CHECK_HEADER([stdint.h],,AC_MSG_ERROR([No stdint.h header file found!]))
AC_CHECK_HEADER([unistd.h],,AC_MSG_ERROR([No unistd.h header file found!]))
AC_CHECK_HEADER([sys/ioctl.h],,AC_MSG_ERROR([No sys/ioctl.h header file found!]))
AC_CHECK_HEADER([linux/fs.h],,AC_MSG_ERROR([No linux/fs.h header file found!]))
AC_CHECK_HEADER([pthread.h],,AC_MSG_ERROR([No pthread.h header file found!]))
#AC_CHECK_HEADER([fuse/fuse.h],,AC_MSG_WARN([No fuse.h header file found! This could be a false positive!]))
PKG_CHECK_MODULES([fuse],[fuse])
AC_CHECK_HEADER([libewf.h],,AC_MSG_WARN([No libewf.h header file found! EWF input support will be disabled.]))
AC_CHECK_HEADER([afflib/afflib.h],,AC_MSG_WARN([No afflib.h header file found! AFF input support will be disabled.]))

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_INT64_T
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_FUNC_FSEEKO
AC_PROG_GCC_TRADITIONAL
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_FUNC_VPRINTF
AC_CHECK_FUNCS([memset strrchr])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
