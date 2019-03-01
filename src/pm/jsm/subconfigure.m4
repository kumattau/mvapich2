[#] start of __file__

AC_DEFUN([PAC_SUBCFG_PREREQ_]PAC_SUBCFG_AUTO_SUFFIX,[
])

AC_DEFUN([PAC_SUBCFG_BODY_]PAC_SUBCFG_AUTO_SUFFIX,[

# the pm_names variable is set by the top level configure
build_pm_jsm=no
for pm_name in $pm_names ; do
    if test "X$pm_name" = "Xjsm" ; then
        build_pm_jsm=yes
    fi
done

AM_CONDITIONAL([BUILD_PM_JSM],[test "x$build_pm_jsm" = "xyes"])

AM_COND_IF([BUILD_PM_JSM],[

# with_pmi is set by the top level configure

if test "x$with_pmi" = "xpmi4pmix"; then
    AC_ARG_WITH([pmi4pmix],
        [AS_HELP_STRING([--with-pmi4pmix=@<:@Path to pmi4pmix library@:>@],
                           [Specify path to PMI4PMIX installation])
        ],
        [AS_CASE([$with_pmi4pmix],
            [yes|no], [AC_MSG_FAILURE([--with-pmi4pmix must be passed a path])],
            [CPPFLAGS="$CPPFLAGS -I$with_pmi4pmix/build"]
            [LDFLAGS="$LDFLAGS -Wl,-rpath,$with_pmi4pmix/lib -L$with_pmi4pmix/lib"])
        ],
        [with_pmi4pmix=no])
    
    if test "x$with_pmi4pmix" != "xno"; then
        AC_CHECK_FILE([$with_pmi4pmix/build/pmi.h], [found pmi.h for pmi4pmix], [AC_MSG_ERROR([could not find pmi.h for pmi4pmix. Please use --with-pmi4pmix to point to the correct location. Configure aborted])])
        AC_CHECK_LIB([pmi], [PMI_Init],
                     [PAC_PREPEND_FLAG([-lpmi],[LIBS])
                      PAC_PREPEND_FLAG([-Wl,-rpath,$with_pmi4pmix/lib -L$with_pmi4pmix/lib -lpmi], [WRAPPER_LIBS])],
                     [AC_MSG_ERROR([could not find the pmi4pmix libpmi. Please use --with-pmi4pmix to point to the correct location. Configure aborted])])
        AC_CHECK_FUNCS([PMI_Ibarrier], [AC_DEFINE([HAVE_PMI_IBARRIER], [1], [Define if pmi client supports PMI_Ibarrier])])
        AC_CHECK_FUNCS([PMI_Wait], [AC_DEFINE([HAVE_PMI_WAIT], [1], [Define if pmi client supports PMI_Wait])])
        AC_CHECK_FUNCS([PMI2_KVS_Ifence], [AC_DEFINE([HAVE_PMI2_KVS_IFENCE], [1], [Define if pmi client supports PMI2_KVS_Ifence])])
        AC_CHECK_FUNCS([PMI2_KVS_Wait], [AC_DEFINE([HAVE_PMI2_KVS_WAIT], [1], [Define if pmi client supports PMI2_KVS_Wait])])
        AC_CHECK_FUNCS([PMI2_Iallgather], [AC_DEFINE([HAVE_PMI2_SHMEM_IALLGATHER], [1], [Define if pmi client supports PMI2_Iallgather])])
        AC_CHECK_FUNCS([PMI2_Iallgather_wait], [AC_DEFINE([HAVE_PMI2_SHMEM_IALLGATHER_WAIT], [1], [Define if pmi client supports PMI2_Iallgather_wait])])
        AC_CHECK_FUNCS([PMI2_SHMEM_Iallgather], [AC_DEFINE([HAVE_PMI2_SHMEM_IALLGATHER], [1], [Define if pmi client supports PMI2_SHMEM_Iallgather])])
        AC_CHECK_FUNCS([PMI2_SHMEM_Iallgather_wait], [AC_DEFINE([HAVE_PMI2_SHMEM_IALLGATHER_WAIT], [1], [Define if pmi client supports PMI2_SHMEM_Iallgather_wait])])
    fi
else
    AC_MSG_ERROR([Selected PMI ($with_pmi) is not compatible with jsm. Please reconfigure after setting --with-pmi=pmi4pmix.])
fi
])dnl end COND_IF

])dnl end BODY macro

[#] end of __file__
