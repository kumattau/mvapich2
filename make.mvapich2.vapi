#!/bin/bash

# Most variables here can be overridden by exporting them in the environment
# before running this script.  Default values have been provided if the
# environment variable is not already set.

source ./make.mvapich2.def

# The target architecture.  If not exported outside of this script,
# it will be found automatically or prompted for if necessary.
# Supported: "_IA32_", "_IA64_", "_EM64T_", "_X86_64_"
if [ -z "$ARCH" ]; then
    arch
fi

if [ $ARCH = "SOLARIS" ]; then
    die_setup "MVAPICH2 VAPI is not supported on this architecture: $ARCH."
fi

# Mandatory variables.  All are checked except CXX and F90.
MTHOME=${MTHOME:-/usr/local/ibgd/driver/infinihost}
PREFIX=${PREFIX:-/usr/local/mvapich2}
export CC=${CC:-gcc}
export CXX=${CXX:-g++}
export F77=${F77:-g77}
export F90=${F90:-}

# To build on Mac OS X systems, the following settings could be used:
#MTHOME=/usr/SmallTree
#export CC=/usr/local/bin/gcc
#export CXX=/usr/local/bin/g++
#export F77=/usr/local/bin/g77
#export F90=/usr/local/bin/g77

if [ $ARCH = "MAC_OSX" ]; then
    export MAC_OSX=yes
    SUPPRESS="-multiply_defined suppress"
fi

# Check mandatory variable settings.
if [ -z "$MTHOME" ] || [ -z "$PREFIX" ] || [ -z "$CC" ] || [ -z "$F77" ]; then
    die_setup "Please set mandatory variables in this script."
elif [ ! -d $MTHOME ]; then
    die_setup "MTHOME directory $MTHOME does not exist."
elif [ -d $MTHOME/lib64 ]; then
    MTHOME_LIB=$MTHOME/lib64
elif [ -d $MTHOME/lib ]; then
    MTHOME_LIB=$MTHOME/lib
else
    die_setup "Could not find the MTHOME/lib64 or MTHOME/lib directory."
fi

# Set this to override automatic optimization setting (-O2).
OPT_FLAG=${OPT_FLAG:--O2}

# Cluster size.
# Supported: "_SMALL_CLUSTER", "_MEDIUM_CLUSTER", and "_LARGE_CLUSTER"
VCLUSTER=${VCLUSTER:-}

if [ -z "$VCLUSTER" ]; then
    prompt_vcluster
fi

# I/O Bus type.
# Supported: "_PCI_X_" and "_PCI_EX_"
IO_BUS=${IO_BUS:-}

if [ -z "$IO_BUS" ]; then
    prompt_io_bus
fi

# Link speed rate.
# Supported: "_DDR_" and "_SDR_" (PCI-Express)
#            "_SDR_" (PCI-X)
LINKS=${LINKS:-}

if [ -z "$LINKS" ]; then
    prompt_link
fi

# Whether to use an optimized queue pair exchange scheme.  Disabled by default.
# Enalbed with "yes".
HAVE_MPD_RING=${HAVE_MPD_RING:-}

if [ "$HAVE_MPD_RING" = "yes" ]; then
    HAVE_MPD_RING="-DUSE_MPD_RING"
else
    HAVE_MPD_RING=""
fi

# Whether or not to build with multithreaded support.  Building with this
# option will make MVAPICH2 thread-safe, but it may suffer slight
# performance penalty in single-threaded case.  Disabled by default.
# Enable with "yes".
MULTI_THREAD=${MULTI_THREAD:-}

if [ "$MULTI_THREAD" = "yes" ]; then
    MULTI_THREAD="--enable-threads=multiple"
else
    MULTI_THREAD=""
fi

# Whether or not to build with ROMIO MPI I/O support.  Disabled by default.
# Enable with "yes".
ROMIO=${ROMIO:-}

if [ "$ROMIO" = "yes" ]; then
    ROMIO="--enable-romio"
else
    ROMIO="--disable-romio"
fi

# Whether or not to build with shared library support.  Disabled by default.
# Enabled with "yes".
SHARED_LIBS=${SHARED_LIBS:-}

if [ "$SHARED_LIBS" = "yes" ]; then
    if [ $ARCH = "MAC_OSX" ]; then
	SHARED_LIBS="--enable-sharedlibs=osx-gcc"
    else
	SHARED_LIBS="--enable-sharedlibs=gcc"
    fi
else
    SHARED_LIBS=""
fi

export LD_LIBRARY_PATH=$MTHOME_LIB:$LD_LIBRARY_PATH
export LIBS=${LIBS:--L${MTHOME_LIB} -lmtl_common -lvapi -lpthread -lmosal -lmpga $SUPPRESS}
export FFLAGS=${FFLAGS:--L${MTHOME_LIB}}
export CFLAGS=${CFLAGS:--D${ARCH} -DONE_SIDED -DUSE_INLINE -DRDMA_FAST_PATH -DUSE_HEADER_CACHING -DLAZY_MEM_UNREGISTER -D_SMP_ -D${IO_BUS} -D${LINKS} -DMPID_USE_SEQUENCE_NUMBERS -D${VCLUSTER} ${HAVE_MPD_RING} -I${MTHOME}/include $OPT_FLAG $SUPPRESS}

# Prelogue
make distclean &>/dev/null
rm -rf *.cache *.log *.status lib bin
set -o pipefail

# Configure MVAPICH2
echo "Configuring MVAPICH2..."
./configure  --prefix=${PREFIX} ${MULTI_THREAD} \
    --with-device=osu_ch3:mrail --with-rdma=vapi --with-pm=mpd \
    ${ROMIO} ${SHARED_LIBS} --without-mpe 2>&1 |tee config-mine.log
ret=$?
tail config-mine.log
test $ret = 0 ||  die "configuration."

# Build MVAPICH2
echo "Building MVAPICH2..."
make 2>&1 |tee make-mine.log 
ret=$?
tail make-mine.log
test $ret = 0 ||  die "building MVAPICH2."

# Install MVAPICH2
echo "MVAPICH2 installation..."
rm -f install-mine.log 
make install 2>&1 |tee install-mine.log
ret=$?
tail install-mine.log
test $ret = 0 ||  die "installing MVAPICH2."

# Epilogue
echo "Congratulations on successfully building MVAPICH2. Please send your feedback to mvapich-discuss@cse.ohio-state.edu." 
