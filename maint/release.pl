#!/usr/bin/env perl
# Copyright (c) 2003-2011, The Ohio State University. All rights
# reserved.
#
# This file is part of the MVAPICH2 software package developed by the
# team members of The Ohio State University's Network-Based Computing
# Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
#
# For detailed copyright and licensing information, please refer to the
# copyright file COPYRIGHT in the top level MVAPICH2 directory.
#
# (C) 2008 by Argonne National Laboratory.
#     See COPYRIGHT in top-level directory.
#
#
# Known limitations:
#
#    1. This script assumes that it is the only client accessing the
#    svn server. Version number verifications, diffs for ABI
#    mismatches and other checks are run assuming atomicity.
#
#    2. ABI mismatch checks are run using an svn diff in mpi.h.in and
#    the binding directory. This can come up with false positives, and
#    is only meant to be a worst-case guess.
#

use strict;
use warnings;

use Cwd qw( realpath );
use Getopt::Long;

my $arg = 0;
my $source = "";
my $psource = "";
my $version = "";
my $append_svnrev;
my $root = $ENV{PWD};
my $with_autoconf = "";
my $with_automake = "";

# Default to MPICH2
my $pack = "mpich2";

my $logfile = "release.log";

sub usage
{
# <_OSU_MVAPICH_>
#    print "Usage: $0 [--source source] {--package package} [version]\n";
    print "Usage: $0 [OPTIONS] [--source source] [version]\n";
# </_OSU_MVAPICH_>
    print "OPTIONS:\n";

    # Source svn repository from where the package needs to be downloaded from
    print "\t--source          source svn repository (required)\n";

    # svn repository for the previous source in this series to ensure ABI compliance
    print "\t--psource    source repo for the previous version for ABI compliance (required)\n";

    # what package we are creating
    print "\t--package         package to create (optional)\n";

    # version string associated with the tarball
    print "\t--version         tarball version (required)\n";

    # append svn revision
    print "\t--append-svnrev   append svn revision number (optional)\n";

    print "\n";

    exit;
}

sub check_package
{
    my $pack = shift;

    print "===> Checking for package $pack... ";
    if ($with_autoconf and ($pack eq "autoconf")) {
        # the user specified a dir where autoconf can be found
        if (not -x "$with_autoconf/$pack") {
            print "not found\n";
            exit;
        }
    }
    if ($with_automake and ($pack eq "automake")) {
        # the user specified a dir where automake can be found
        if (not -x "$with_automake/$pack") {
            print "not found\n";
            exit;
        }
    }
    else {
        if (`which $pack` eq "") {
            print "not found\n";
            exit;
        }
    }
    print "done\n";
}

sub run_cmd
{
    my $cmd = shift;

    # FIXME: Allow for verbose output
    system("$cmd >> $root/$logfile 2>&1");
    if ($?) {
        die "unable to execute ($cmd), \$?=$?.  Stopped";
    }
}

sub debug
{
    my $line = shift;

    print "$line";
}

sub create_docs
{
    my $pack = shift;

    if ($pack eq "romio") {
	chdir("romio/doc");
	run_cmd("make");
	run_cmd("rm -f users-guide.blg users-guide.toc users-guide.aux users-guide.bbl users-guide.log users-guide.dvi");
    }
    elsif ($pack eq "mpe") {
	chdir("mpe2/maint");
	run_cmd("make -f Makefile4man");
    }
}

sub create_mvapich2
{
    # Check out the appropriate source
    debug("===> Checking out mvapich2 SVN source... ");
# <_OSU_MVAPICH_>
#    run_cmd("rm -rf mpich2-${version}");
#    run_cmd("svn export -q ${source} mpich2-${version}");
    run_cmd("rm -rf ${version}");
    run_cmd("svn export -q ${source} ${version}");
# </_OSU_MVAPICH_>
    debug("done\n");

    # Remove packages that are not being released
    debug("===> Removing packages that are not being released... ");
# <_OSU_MVAPICH_>
#    chdir("${root}/mpich2-${version}");
    chdir("${root}/${version}");
    run_cmd("date +%F > maint/ReleaseDate");
# </_OSU_MVAPICH_>
    run_cmd("rm -rf src/mpid/globus doc/notes src/pm/mpd/Zeroconf.py src/mpid/ch3/channels/gasnet src/mpid/ch3/channels/sshm src/pmi/simple2");

# <_OSU_MVAPICH_>
#    chdir("${root}/mpich2-${version}/src/mpid/ch3/channels/nemesis/nemesis/net_mod");
    chdir("${root}/${version}/src/mpid/ch3/channels/nemesis/nemesis/net_mod");
# </_OSU_MVAPICH_>
    my @nem_modules = qw(elan mx newgm newtcp sctp ib psm);
    run_cmd("rm -rf ".join(' ', map({$_ . "_module/*"} @nem_modules)));
    for my $module (@nem_modules) {
	# system to avoid problems with shell redirect in run_cmd
	system(qq(echo "# Stub Makefile" > ${module}_module/Makefile.sm));
    }
    debug("done\n");

    # Create configure
    debug("===> Creating configure in the main package... ");
# <_OSU_MVAPICH_>
#    chdir("${root}/mpich2-${version}");
    chdir("${root}/${version}");
#    run_cmd("./maint/updatefiles --with-autoconf=/homes/chan/autoconf/2.62/bin");
    run_cmd("./maint/updatefiles");
# </_OSU_MVAPICH_>
    debug("done\n");

    # Remove unnecessary files
    debug("===> Removing unnecessary files in the main package... ");
# <_OSU_MVAPICH_>
#    chdir("${root}/mpich2-${version}");
    chdir("${root}/${version}");
#    run_cmd("rm -rf README.vin maint/config.log maint/config.status unusederr.txt autom4te.cache src/mpe2/src/slog2sdk/doc/jumpshot-4/tex");
    run_cmd("rm -rf maint/config.log maint/config.status unusederr.txt autom4te.cache src/mpe2/src/slog2sdk/doc/jumpshot-4/tex");
# </_OSU_MVAPICH_>
    debug("done\n");

    # Get docs
    debug("===> Creating secondary package for the docs... ");
    chdir("${root}");
# <_OSU_MVAPICH_>
#    run_cmd("cp -a mpich2-${version} mpich2-${version}-tmp");
    run_cmd("cp -a ${version} ${version}-tmp");
# </_OSU_MVAPICH_>
    debug("done\n");

    debug("===> Configuring and making the secondary package... ");
# <_OSU_MVAPICH_>
#    chdir("${root}/mpich2-${version}-tmp");
#    run_cmd("./maint/updatefiles --with-autoconf=/homes/chan/autoconf/2.62/bin");
#    run_cmd("./configure --without-mpe --disable-f90 --disable-f77 --disable-cxx");
    chdir("${root}/${version}-tmp");
    run_cmd("./maint/updatefiles");
    run_cmd("./configure --disable-f90 --disable-f77 --disable-cxx");
# </_OSU_MVAPICH_>
    run_cmd("(make mandoc && make htmldoc && make latexdoc)");
    debug("done\n");

    debug("===> Copying docs over... ");
# <_OSU_MVAPICH_>
#    chdir("${root}/mpich2-${version}-tmp");
#    run_cmd("cp -a man ${root}/mpich2-${version}");
#    run_cmd("cp -a www ${root}/mpich2-${version}");
#    run_cmd("cp -a doc/userguide/user.pdf ${root}/mpich2-${version}/doc/userguide");
#    run_cmd("cp -a doc/installguide/install.pdf ${root}/mpich2-${version}/doc/installguide");
#    run_cmd("cp -a doc/smpd/smpd_pmi.pdf ${root}/mpich2-${version}/doc/smpd");
#    run_cmd("cp -a doc/logging/logging.pdf ${root}/mpich2-${version}/doc/logging");
#    run_cmd("cp -a doc/windev/windev.pdf ${root}/mpich2-${version}/doc/windev");
    chdir("${root}/${version}-tmp");
    run_cmd("cp -a man ${root}/${version}");
    run_cmd("cp -a www ${root}/${version}");
# </_OSU_MVAPICH_>
    chdir("${root}");
# <_OSU_MVAPICH_>
#    run_cmd("rm -rf mpich2-${version}-tmp");
    run_cmd("rm -rf ${version}-tmp");
# </_OSU_MVAPICH_>
    debug("done\n");

    debug("===> Creating ROMIO docs... ");
# <_OSU_MVAPICH_>
#    chdir("${root}/mpich2-${version}/src/mpi");
    chdir("${root}/${version}/src/mpi");
# </_OSU_MVAPICH_>
    create_docs("romio");
    debug("done\n");

    debug( "===> Creating MPE docs... ");
# <_OSU_MVAPICH_>
#    chdir("${root}/mpich2-${version}/src");
    chdir("${root}/${version}/src");
# </_OSU_MVAPICH_>
    create_docs("mpe");
    debug("done\n");

    # Create the tarball
    debug("===> Creating the final mvapich2 tarball... ");
    chdir("${root}");
# <_OSU_MVAPICH_>
#    run_cmd("tar -czvf mpich2-${version}.tgz mpich2-${version}");
#    run_cmd("rm -rf mpich2-${version}");
    run_cmd("tar -czvf ${version}.tgz ${version}");
    run_cmd("rm -rf ${version}");
# </_OSU_MVAPICH_>
    debug("done\n\n");
}

sub create_romio
{
    # Check out the appropriate source
    debug("===> Checking out romio SVN source... ");
    run_cmd("rm -rf romio-${version} romio");
    run_cmd("svn export -q ${source}/src/mpi/romio");
    debug("done\n");

    debug("===> Creating configure... ");
    chdir("${root}/romio");
    run_cmd("autoreconf");
    debug("done\n");

    debug("===> Creating ROMIO docs... ");
    chdir("${root}");
    create_docs("romio");
    debug("done\n");

    # Create the tarball
    debug("===> Creating the final romio tarball... ");
    chdir("${root}");
    run_cmd("mv romio romio-${version}");
    run_cmd("tar -czvf romio-${version}.tar.gz romio-${version}");
    run_cmd("rm -rf romio-${version}");
    debug("done\n\n");
}

sub create_mpe
{
    # Check out the appropriate source
    debug("===> Checking out mpe2 SVN source... ");
    run_cmd("rm -rf mpe2-${version} mpe2");
    run_cmd("svn export -q ${source}/src/mpe2");
    debug("done\n");

    debug("===> Creating configure... ");
    chdir("${root}/mpe2");
    run_cmd("./maint/updatefiles --with-autoconf=/homes/chan/autoconf/2.62/bin");
    debug("done\n");

    debug("===> Creating MPE docs... ");
    chdir("${root}");
    create_docs("mpe");
    debug("done\n");

    # Create the tarball
    debug("===> Creating the final mpe2 tarball... ");
    chdir("${root}");
    run_cmd("mv mpe2 mpe2-${version}");
    run_cmd("tar -czvf mpe2-${version}.tar.gz mpe2-${version}");
    run_cmd("rm -rf mpe2-${version}");
    debug("done\n\n");
}

GetOptions(
    "source=s" => \$source,
# <_OSU_MVAPICH_>
#    "package:s"  => \$pack,
# </_OSU_MVAPICH_>
    "source=s" => \$source,
    "psource=s" => \$psource,
    "package:s"  => \$pack,
    "version=s" => \$version,
    "append-svnrev!" => \$append_svnrev,
    "with-autoconf" => \$with_autoconf,
    "with-automake" => \$with_automake,
    "help"     => \&usage,
) or die "unable to parse options, stopped";

if (scalar(@ARGV) != 0) {
    usage();
}

$version = $ARGV[0];

# <_OSU_MVAPICH_>
#if (!$pack) {
#    $pack = "mpich2";
#}
# </_OSU_MVAPICH_>

if (!$source || !$version || !$psource) {
    usage();
}

check_package("doctext");
check_package("txt2man");
check_package("svn");
check_package("latex");
check_package("autoconf");
check_package("automake");
print("\n");

my $current_ver = `svn cat ${source}/maint/Version | grep ^MPICH2_VERSION: | cut -f2 -d' '`;
if ("$current_ver" ne "$version\n") {
    print("\tWARNING: Version mismatch\n\n");
}

system("rm -f ${root}/$logfile");
# <_OSU_MVAPICH_>
#if ($pack eq "mpich2") {
    create_mvapich2();
#}
#elsif ($pack eq "romio") {
#    create_romio();
#}
#elsif ($pack eq "mpe") {
#    create_mpe();
#}
#elsif ($pack eq "all") {
#    create_mpich2();
#    create_romio();
##     create_mpe();
#}
#else {
#    die "Unknown package: $pack";
#}
# </_OSU_MVAPICH_>

if ($psource) {
    # Check diff
    my $d = `svn diff ${psource}/src/include/mpi.h.in ${source}/src/include/mpi.h.in`;
    $d .= `svn diff ${psource}/src/binding ${source}/src/binding`;
    if ("$d" ne "") {
	print("\tWARNING: ABI mismatch\n\n");
    }
}

if ($append_svnrev) {
    $version .= "-r";
    $version .= `svn info ${source} | grep ^Revision: | cut -f2 -d' ' | xargs echo -n`;
}

# Clean up the log file
system("rm -f ${root}/$logfile");

# Check out the appropriate source
print("===> Checking out $pack SVN source... ");
run_cmd("rm -rf ${pack}-${version}");
run_cmd("svn export -q ${source} ${pack}-${version}");
print("done\n");

# Remove packages that are not being released
print("===> Removing packages that are not being released... ");
chdir("${root}/${pack}-${version}");
run_cmd("rm -rf src/mpid/globus doc/notes src/pm/mpd/Zeroconf.py");

chdir("${root}/${pack}-${version}/src/mpid/ch3/channels/nemesis/nemesis/netmod");
my @nem_modules = qw(elan ib psm);
run_cmd("rm -rf ".join(' ', map({$_ . "/*"} @nem_modules)));
for my $module (@nem_modules) {
    # system to avoid problems with shell redirect in run_cmd
    system(qq(echo "# Stub Makefile" > ${module}/Makefile.sm));
}
print("done\n");

# Create configure
print("===> Creating configure in the main package... ");
chdir("${root}/${pack}-${version}");
{
    my $cmd = "./maint/updatefiles";
    $cmd .= " --with-autoconf=$with_autoconf" if $with_autoconf;
    $cmd .= " --with-automake=$with_automake" if $with_automake;
    run_cmd($cmd);
}
print("done\n");

# Remove unnecessary files
print("===> Removing unnecessary files in the main package... ");
chdir("${root}/${pack}-${version}");
run_cmd("rm -rf README.vin maint/config.log maint/config.status unusederr.txt src/mpe2/src/slog2sdk/doc/jumpshot-4/tex");
run_cmd("find . -name autom4te.cache | xargs rm -rf");
print("done\n");

# Get docs
print("===> Creating secondary package for the docs... ");
chdir("${root}");
run_cmd("cp -a ${pack}-${version} ${pack}-${version}-tmp");
print("done\n");

print("===> Configuring and making the secondary package... ");
chdir("${root}/${pack}-${version}-tmp");
{
    my $cmd = "./maint/updatefiles";
    $cmd .= " --with-autoconf=$with_autoconf" if $with_autoconf;
    $cmd .= " --with-automake=$with_automake" if $with_automake;
    run_cmd($cmd);
}
run_cmd("./configure --disable-mpe --disable-f90 --disable-f77 --disable-cxx");
run_cmd("(make mandoc && make htmldoc && make latexdoc)");
print("done\n");

print("===> Copying docs over... ");
chdir("${root}/${pack}-${version}-tmp");
run_cmd("cp -a man ${root}/${pack}-${version}");
run_cmd("cp -a www ${root}/${pack}-${version}");
run_cmd("cp -a doc/userguide/user.pdf ${root}/${pack}-${version}/doc/userguide");
run_cmd("cp -a doc/installguide/install.pdf ${root}/${pack}-${version}/doc/installguide");
run_cmd("cp -a doc/smpd/smpd_pmi.pdf ${root}/${pack}-${version}/doc/smpd");
run_cmd("cp -a doc/logging/logging.pdf ${root}/${pack}-${version}/doc/logging");
run_cmd("cp -a doc/windev/windev.pdf ${root}/${pack}-${version}/doc/windev");
chdir("${root}");
run_cmd("rm -rf ${pack}-${version}-tmp");
print("done\n");

print("===> Creating ROMIO docs... ");
chdir("${root}/${pack}-${version}/src/mpi");
chdir("romio/doc");
run_cmd("make");
run_cmd("rm -f users-guide.blg users-guide.toc users-guide.aux users-guide.bbl users-guide.log users-guide.dvi");
print("done\n");

print( "===> Creating MPE docs... ");
chdir("${root}/${pack}-${version}/src");
chdir("mpe2/maint");
run_cmd("make -f Makefile4man");
print("done\n");

# Create the tarball
print("===> Creating the final ${pack} tarball... ");
chdir("${root}");
run_cmd("tar -czvf ${pack}-${version}.tar.gz ${pack}-${version}");
run_cmd("rm -rf ${pack}-${version}");
print("done\n\n");

