dnl
dnl record top-level directory (this one)
dnl A problem.  Some systems use an NFS automounter.  This can generate
dnl paths of the form /tmp_mnt/... . On SOME systems, that path is
dnl not recognized, and you need to strip off the /tmp_mnt. On others, 
dnl it IS recognized, so you need to leave it in.  Grumble.
dnl The real problem is that OTHER nodes on the same NFS system may not
dnl be able to find a directory based on a /tmp_mnt/... name.
dnl
dnl It is WRONG to use $PWD, since that is maintained only by the C shell,
dnl and if we use it, we may find the 'wrong' directory.  To test this, we
dnl try writing a file to the directory and then looking for it in the 
dnl current directory.  Life would be so much easier if the NFS automounter
dnl worked correctly.
dnl
dnl PAC_GETWD(varname [, filename ] )
dnl 
dnl Set varname to current directory.  Use filename (relative to current
dnl directory) if provided to double check.
dnl
dnl Need a way to use "automounter fix" for this.
dnl
define(PAC_GETWD,[
AC_MSG_CHECKING(for current directory name)
$1=$PWD
if test "${$1}" != "" -a -d "${$1}" ; then 
    if test -r ${$1}/.foo$$ ; then
        /bin/rm -f ${$1}/.foo$$
        /bin/rm -f .foo$$
    fi
    if test -r ${$1}/.foo$$ -o -r .foo$$ ; then
        $1=
    else
        echo "test" > ${$1}/.foo$$
        if test ! -r .foo$$ ; then
            /bin/rm -f ${$1}/.foo$$
            $1=
        else
             /bin/rm -f ${$1}/.foo$$
        fi
    fi
fi
if test "${$1}" = "" ; then
    $1=`pwd | sed -e 's%/tmp_mnt/%/%g'`
fi
dnl
dnl First, test the PWD is sensible
ifelse($2,,,
if test ! -r ${$1}/$2 ; then
    dnl PWD must be messed up
    $1=`pwd`
    if test ! -r ${$1}/$2 ; then
        AC_MSG_ERROR([Cannot determine the root directory!])
    fi
    $1=`pwd | sed -e 's%/tmp_mnt/%/%g'`
    if test ! -d ${$1} ; then 
        AC_MSG_WARN([Warning: your default path uses the automounter; this may
cause some problems if you use other NFS-connected systems.])
        $1=`pwd`
    fi
fi)
if test -z "${$1}" ; then
    $1=`pwd | sed -e 's%/tmp_mnt/%/%g'`
    if test ! -d ${$1} ; then 
        AC_MSG_WARN([Warning: your default path uses the automounter; this may
cause some problems if you use other NFS-connected systems.])
        $1=`pwd`
    fi
fi
AC_MSG_RESULT(${$1})
])dnl
dnl
dnl This version compiles an entire function; used to check for
dnl things like varargs done correctly
dnl
dnl PAC_COMPILE_CHECK_FUNC(msg,function,if_true,if_false)
dnl
define(PAC_COMPILE_CHECK_FUNC,
[AC_PROVIDE([$0])dnl
ifelse([$1], , , [AC_MSG_CHECKING(for $1)]
)dnl
if test ! -f confdefs.h ; then
    AC_MSG_RESULT("!! SHELL ERROR !!")
    echo "The file confdefs.h created by configure has been removed"
    echo "This may be a problem with your shell; some versions of LINUX"
    echo "have this problem.  See the Installation guide for more"
    echo "information."
    exit 1
fi
cat > conftest.c <<EOF
#include "confdefs.h"
[$2]
EOF
dnl Don't try to run the program, which would prevent cross-configuring.
if { (eval echo configure:__oline__: \"$ac_compile\") 1>&5; (eval $ac_compile) 2>&5; }; then
  ifelse([$1], , , [AC_MSG_RESULT(yes)])
  ifelse([$3], , :, [rm -rf conftest*
  $3
])
ifelse([$4], , , [else
  rm -rf conftest*
  $4
])dnl
   ifelse([$1], , , ifelse([$4], ,else) [AC_MSG_RESULT(no)])
fi
rm -f conftest*]
)dnl
dnl
dnl PAC_OUTPUT_EXEC(files[,mode]) - takes files (as shell script or others),
dnl and applies configure to the them.  Basically, this is what AC_OUTPUT
dnl should do, but without adding a comment line at the top.
dnl Must be used ONLY after AC_OUTPUT (it needs config.status, which 
dnl AC_OUTPUT creates).
dnl Optionally, set the mode (+x, a+x, etc)
dnl
define(PAC_OUTPUT_EXEC,[
CONFIG_FILES="$1"
export CONFIG_FILES
./config.status
CONFIG_FILES=""
for pac_file in $1 ; do 
    /bin/rm -f .pactmp
    sed -e '1d' $pac_file > .pactmp
    /bin/rm -f $pac_file
    mv .pactmp $pac_file
    ifelse($2,,,chmod $2 $pac_file)
done
])dnl
dnl
dnl This is a replacement for AC_PROG_CC that does not prefer gcc and
dnl that does not mess with CFLAGS.  See acspecific.m4 for the original defn.
dnl
dnl/*D
dnl PAC_PROG_CC - Find a working C compiler
dnl
dnl Synopsis:
dnl PAC_PROG_CC
dnl
dnl Output Effect:
dnl   Sets the variable CC if it is not already set
dnl
dnl Notes:
dnl   Unlike AC_PROG_CC, this does not prefer gcc and does not set CFLAGS.
dnl   It does check that the compiler can compile a simple C program.
dnl   It also sets the variable GCC to yes if the compiler is gcc.  It does
dnl   not yet check for some special options needed in particular for
dnl   parallel computers, such as -Tcray-t3e, or special options to get
dnl   full ANSI/ISO C, such as -Aa for HP.
dnl
dnlD*/
AC_DEFUN(PAC_PROG_CC,[
AC_PROVIDE([AC_PROG_CC])
AC_CHECK_PROGS(CC, cc xlC xlc pgcc icc gcc )
test -z "$CC" && AC_MSG_ERROR([no acceptable cc found in \$PATH])
PAC_PROG_CC_WORKS
AC_PROG_CC_GNU
if test "$ac_cv_prog_gcc" = yes; then
    GCC=yes
else
    GCC=
fi
])
dnl
dnl
dnl This is a replacement that checks that FAILURES are signaled as well
dnl (later configure macros look for the .o file, not just success from the
dnl compiler, but they should not HAVE to
dnl
dnl --- insert 2.52 compatibility here ---
dnl 2.52+ does not have AC_PROG_CC_GNU
ifdef([AC_PROG_CC_GNU],,[AC_DEFUN([AC_PROG_CC_GNU],)])
dnl 2.52 does not have AC_PROG_CC_WORKS
ifdef([AC_PROG_CC_WORKS],,[AC_DEFUN([AC_PROG_CC_WORKS],)])
dnl
dnl
AC_DEFUN(PAC_PROG_CC_WORKS,
[AC_PROG_CC_WORKS
AC_MSG_CHECKING([whether the C compiler sets its return status correctly])
AC_LANG_SAVE
AC_LANG_C
AC_TRY_COMPILE(,[int a = bzzzt;],notbroken=no,notbroken=yes)
AC_MSG_RESULT($notbroken)
if test "$notbroken" = "no" ; then
    AC_MSG_ERROR([installation or configuration problem: C compiler does not
correctly set error code when a fatal error occurs])
fi
])
dnl
dnl ***TAKEN FROM sowing/confdb/aclocal_cc.m4 IF YOU FIX THIS, FIX THAT
dnl VERSION AS WELL
dnl Check whether we need -fno-common to correctly compile the source code.
dnl This is necessary if global variables are defined without values in
dnl gcc.  Here is the test
dnl conftest1.c:
dnl extern int a; int a;
dnl conftest2.c:
dnl extern int a; int main(int argc; char *argv[] ){ return a; }
dnl Make a library out of conftest1.c and try to link with it.
dnl If that fails, recompile it with -fno-common and see if that works.
dnl If so, add -fno-common to CFLAGS
dnl An alternative is to use, on some systems, ranlib -c to force 
dnl the system to find common symbols.
dnl
dnl NOT TESTED
AC_DEFUN(PAC_PROG_C_BROKEN_COMMON,[
AC_MSG_CHECKING([whether global variables handled properly])
AC_REQUIRE([AC_PROG_RANLIB])
ac_cv_prog_cc_globals_work=no
echo 'extern int a; int a;' > conftest1.c
echo 'extern int a; int main( ){ return a; }' > conftest2.c
if ${CC-cc} $CFLAGS -c conftest1.c >conftest.out 2>&1 ; then
    if ${AR-ar} cr libconftest.a conftest1.o >/dev/null 2>&1 ; then
        if ${RANLIB-:} libconftest.a >/dev/null 2>&1 ; then
            if ${CC-cc} $CFLAGS -o conftest conftest2.c $LDFLAGS libconftest.a >> conftest.out 2>&1 ; then
                # Success!  C works
                ac_cv_prog_cc_globals_work=yes
            else
                # Failure!  Do we need -fno-common?
                ${CC-cc} $CFLAGS -fno-common -c conftest1.c >> conftest.out 2>&1
                rm -f libconftest.a
                ${AR-ar} cr libconftest.a conftest1.o >>conftest.out 2>&1
                ${RANLIB-:} libconftest.a >>conftest.out 2>&1
                if ${CC-cc} $CFLAGS -o conftest conftest2.c $LDFLAGS libconftest.a >> conftest.out 2>&1 ; then
                    ac_cv_prog_cc_globals_work="needs -fno-common"
                    CFLAGS="$CFLAGS -fno-common"
                fi
            fi
        fi
    fi
fi
rm -f conftest* libconftest*
AC_MSG_RESULT($ac_cv_prog_cc_globals_work)
])dnl
dnl
dnl PAC_MSG_ERROR($enable_softerr,ErrorMsg) -
dnl return AC_MSG_ERROR(ErrorMsg) if "$enable_softerr" = "yes"
dnl return AC_MSG_WARN(ErrorMsg) + exit 0 otherwise
dnl
define(PAC_MSG_ERROR,[
if test "$1" = "yes" ; then
    AC_MSG_WARN([ $2 ])
    exit 0
else
    AC_MSG_ERROR([ $2 ])
fi
])dnl
dnl
dnl Modified from mpich2/confdb/aclocal_cc.m4's PAC_CC_STRICT,
dnl remove all reference to enable_strict_done.  Also, make it
dnl more flexible by appending the content of $1 with the
dnl --enable-strict flags.
dnl
dnl PAC_GET_GCC_STRICT_CFLAGS([COPTIONS])
dnl COPTIONS    - returned variable with --enable-strict flags appended.
dnl
dnl Use the value of enable-strict to update input COPTIONS.
dnl be sure no space is inserted after "(" and before ")", otherwise invalid
dnl /bin/sh shell statement like 'COPTIONS  ="$COPTIONS ..."' will be resulted.
dnl
dnl AC_PROG_CC should be called before this macro function.
dnl
AC_DEFUN(PAC_GET_GCC_STRICT_FLAGS,[
AC_MSG_CHECKING( [whether to add strict compiler check flags to $1] )
# We must know the compiler type
if test -z "CC" ; then
    AC_CHECK_PROGS(CC,gcc)
fi
case "$enable_strict" in
    yes)
        if test "$ac_cv_prog_gcc" = "yes" ; then
            $1="[$]$1 -Wall -O2 -Wstrict-prototypes -Wmissing-prototypes -Wundef -Wpointer-arith -Wbad-function-cast -ansi -DGCC_WALL -std=c89"
            AC_MSG_RESULT([yes])
        else
            AC_MSG_WARN([no, strict support for gcc only!])
        fi
        ;;
    all)
        if test "$ac_cv_prog_gcc" = "yes" ; then
            $1="[$]$1 -Wall -O -Wstrict-prototypes -Wmissing-prototypes -Wundef -Wpointer-arith -Wbad-function-cast -ansi -DGCC_WALL -Wunused -Wshadow -Wmissing-declarations -Wno-long-long -std=c89"
            AC_MSG_RESULT([yes, all possible flags.])
        else
            AC_MSG_WARN([no, strict support for gcc only!])
        fi
        ;;
    posix)
        if test "$ac_cv_prog_gcc" = "yes" ; then
            $1="[$]$1 -Wall -O2 -Wstrict-prototypes -Wmissing-prototypes -Wundef -Wpointer-arith -Wbad-function-cast -ansi -DGCC_WALL -D_POSIX_C_SOURCE=199506L -std=c89"
            AC_MSG_RESULT([yes, POSIX flavored flags.])
        else
            AC_MSG_WARN([no, strict support for gcc only!])
        fi
        ;;
    noopt)
        if test "$ac_cv_prog_gcc" = "yes" ; then
            $1="[$]$1 -Wall -Wstrict-prototypes -Wmissing-prototypes -Wundef -Wpointer-arith -Wbad-function-cast -ansi -DGCC_WALL -std=c89"
            AC_MSG_RESULT([yes, non-optimized flags.])
        else
            AC_MSG_WARN([no, strict support for gcc only!])
        fi
        ;;
    no)
        # Accept and ignore this value
        :
        ;;
    *)
        if test -n "$enable_strict" ; then
            AC_MSG_WARN([Unrecognized value for enable-strict:$enable_strict])
        fi
        ;;
esac
])dnl
dnl/*D
dnl PAC_FUNC_NEEDS_DECL - Set NEEDS_<funcname>_DECL if a declaration is needed
dnl
dnl Synopsis:
dnl PAC_FUNC_NEEDS_DECL(headerfiles,funcname)
dnl
dnl Output Effect:
dnl Sets 'NEEDS_<funcname>_DECL' if 'funcname' is not declared by the
dnl headerfiles.
dnl
dnl Approach:
dnl Try to compile a program with the function, but passed with an incorrect
dnl calling sequence.  If the compilation fails, then the declaration
dnl is provided within the header files.  If the compilation succeeds,
dnl the declaration is required.
dnl
dnl We use a 'double' as the first argument to try and catch varargs
dnl routines that may use an int or pointer as the first argument.
dnl
dnl D*/
AC_DEFUN(PAC_FUNC_NEEDS_DECL,[
AC_CACHE_CHECK([whether $2 needs a declaration],
pac_cv_func_decl_$2,[
AC_TRY_COMPILE([$1],[int a=$2(1.0,27,1.0,"foo");],
pac_cv_func_decl_$2=yes,pac_cv_func_decl_$2=no)])
if test "$pac_cv_func_decl_$2" = "yes" ; then
changequote(<<,>>)dnl
define(<<PAC_FUNC_NAME>>, translit(NEEDS_$2_DECL, [a-z *], [A-Z__]))dnl
changequote([, ])dnl
    AC_DEFINE_UNQUOTED(PAC_FUNC_NAME,1,[Define if $2 needs a declaration])
undefine([PAC_FUNC_NAME])
fi
])dnl
