#!/bin/bash

source ./make.mvapich2.def
arch

# Mandatory variables.  All are checked except CXX and F90.
MTHOME=/usr/local/ibgd/driver/infinihost
PREFIX=/usr/local/mvapich2
export CC=gcc
export CXX=g++
export F77=g77
export F90=

if [ $ARCH = "SOLARIS" ]; then
    die_setup "MVAPICH2 VAPI is not supported on Solaris."
elif [ $ARCH = "MAC_OSX" ]; then
    MTHOME=/usr/SmallTree
    export F77=/usr/local/bin/g77
    export F90=/usr/local/bin/g77
    export MAC_OSX=yes
    SUPPRESS="-multiply_defined suppress"
fi

# Check mandatory variable settings.
if [ -z $MTHOME ] || [ -z $PREFIX ] || [ -z $CC ] || [ -z $F77 ]; then
    die_setup "Please set mandatory variables in this script."
elif [ ! -d $MTHOME ]; then
    die_setup "MTHOME directory $MTHOME does not exist."
elif [ $ARCH = "_EM64T_" ] || [ $ARCH = "_X86_64_" ]; then
    if [ -d $MTHOME/lib64 ]; then
	MTHOME_LIB=$MTHOME/lib64
    elif [ -d $MTHOME/lib ]; then
	MTHOME_LIB=$MTHOME/lib
    else
	die_setup "Could not find the MTHOME/lib64 or MTHOME/lib directory."
    fi
elif [ -d $MTHOME/lib ]; then
    MTHOME_LIB=$MTHOME/lib
else
    die_setup "Could not find the MTHOME/lib directory."
fi

# Optional variables.  Most of these are prompted for if not set.
#
# Cluster size.
# Supported: "_SMALL_CLUSTER", "_MEDIUM_CLUSTER", and "_LARGE_CLUSTER"
VCLUSTER=

if [ -z "$VCLUSTER" ]; then
    prompt_vcluster
fi

# I/O Bus type.
# Supported: "_PCI_X_" and "_PCI_EX_"
IO_BUS=

if [ -z "$IO_BUS" ]; then
    prompt_io_bus
fi

# Link speed rate.
# Supported: "_DDR_" and "_SDR_" (PCI-Express)
#            "_SDR_" (PCI-X)
LINKS=

if [ -z "$LINKS" ]; then
    prompt_link
fi

# Whether to use an optimized queue pair exchange scheme.  This is not
# checked for a setting in in the script.  It must be set here explicitly.
# Supported: "-DUSE_MPD_RING" and "" (to disable)
HAVE_MPD_RING=""

# Whether or not to build with multi-thread support
# Building with this option the MVAPICH2 will be thread-safe but it may suffer
# slight performance penalty in single-threaded case
# This option is default to no
# Supported: "yes" or ""
MULTI_THREAD=""

if [ ! -z $MULTI_THREAD ]; then
        MULTI_THREAD="--enable-threads=multiple"
fi

# Set this to override automatic optimization setting (-03).
OPT_FLAG=

if [ -z $OPT_FLAG ]; then
    OPT_FLAG=-O2
fi

export LIBS="-L${MTHOME_LIB} -lmtl_common -lvapi -lpthread -lmosal -lmpga $SUPPRESS"
export FFLAGS="-L${MTHOME_LIB}"
export CFLAGS="-D${ARCH} -DONE_SIDED -DUSE_INLINE -DRDMA_FAST_PATH ${MULTI_THREAD} \
               -DUSE_HEADER_CACHING -DLAZY_MEM_UNREGISTER -D_SMP_ \
               $SUPPRESS -D${IO_BUS} -D${LINKS} -DMPID_USE_SEQUENCE_NUMBERS \
               -D${VCLUSTER} ${HAVE_MPD_RING} -I${MTHOME}/include $OPT_FLAG"

# Prelogue
make distclean &>/dev/null
rm -rf *.cache *.log *.status lib bin

# Configure MVAPICH2
echo "Configuring MVAPICH2..."
./configure  --prefix=${PREFIX} \
    --with-device=osu_ch3:mrail --with-rdma=vapi --with-pm=mpd \
    --disable-romio --without-mpe 2>&1 |tee config-mine.log
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
