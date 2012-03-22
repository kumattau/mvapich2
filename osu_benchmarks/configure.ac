#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.59])
AC_INIT([OSU-Micro-Benchmarks], [3.5.2], [mvapich-discuss@cse.ohio-state.edu])
AC_CONFIG_SRCDIR([osu_get_latency.c])

AM_INIT_AUTOMAKE([foreign])

# Checks for programs.
AC_PROG_CC

# Checks for libraries.
AC_SEARCH_LIBS([sqrt], [m])

# Checks for header files.
AC_CHECK_HEADERS([stdlib.h string.h sys/time.h unistd.h cuda.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_INLINE

# Checks for library functions.
AC_CHECK_FUNCS([getpagesize gettimeofday memset sqrt])

AS_IF([test "x$enable_cuda" == xyes],
		[AC_DEFINE([_ENABLE_CUDA_], [1], [Enable CUDA Buffers])])

AM_CONDITIONAL([CUDA], [test x$enable_cuda = xyes])

AC_DEFINE([FIELD_WIDTH], [20], [Width of field used to report numbers])
AC_DEFINE([FLOAT_PRECISION], [2], [Precision of reported numbers])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
