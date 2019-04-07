#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.68])
AC_INIT([Edge], [0.0], [alefore@gmail.com])
LT_INIT
AM_INIT_AUTOMAKE
AC_CONFIG_SRCDIR([src/buffer.h])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC
AC_PROG_MAKE_SET

# Checks for libraries.
AC_CHECK_LIB([ncursesw], [addwstr])
AC_CHECK_LIB([pthread], [pthread_create])
AC_CHECK_LIB([ao], [ao_play])

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h limits.h stdlib.h string.h sys/ioctl.h sys/socket.h unistd.h ncursesw/curses.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T

# Checks for library functions.
AC_FUNC_FORK
AC_FUNC_MALLOC
AC_FUNC_MMAP
AC_FUNC_REALLOC
AC_FUNC_STRTOD
AC_CHECK_FUNCS([dup2 floor memset mkfifo munmap realpath regcomp setenv strchr strdup strerror strrchr strtol])

AC_CONFIG_FILES([Makefile])
AC_CONFIG_SUBDIRS([glog-0.4.0])
AC_OUTPUT
