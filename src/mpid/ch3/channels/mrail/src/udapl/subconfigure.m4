[#] start of __file__
dnl MPICH2_SUBCFG_AFTER=src/mpid/ch3/channels/mrail

AC_DEFUN([PAC_SUBCFG_PREREQ_]PAC_SUBCFG_AUTO_SUFFIX, [
    AS_IF([test "$build_mrail" = yes -a "x$with_rdma" = xudapl],
	  [build_mrail_udapl=yes],
	  [build_mrail_udapl=no])
    AM_CONDITIONAL([BUILD_MRAIL_UDAPL],
	           [test "$build_mrail_udapl" = yes])
    AM_COND_IF([BUILD_MRAIL_UDAPL], [
	AC_MSG_NOTICE([RUNNING PREREQ FOR ch3:mrail:udapl])
    ])dnl end AM_COND_IF(BUILD_MRAIL_UDAPL,...)
])dnl
dnl
dnl _BODY handles the former role of configure in the subsystem
AC_DEFUN([PAC_SUBCFG_BODY_]PAC_SUBCFG_AUTO_SUFFIX, [
    AM_COND_IF([BUILD_MRAIL_UDAPL], [
	AC_MSG_NOTICE([RUNNING CONFIGURE FOR ch3:mrail:udapl])
    ])dnl end AM_COND_IF(BUILD_MRAIL_UDAPL,...)
])dnl end _BODY
[#] end of __file__
