IF "%1" == "" GOTO HELP
IF "%1" == "--help" GOTO HELP
IF "%CVS_HOST%" == "" set CVS_HOST=harley.mcs.anl.gov
GOTO AFTERHELP
:HELP
REM
REM Usage:
REM    makewindist --help
REM      display this help message
REM    makewindist --with-checkout
REM      1) check out mpich2 from cvs
REM         the environment variable USERNAME is used to checkout mpich2
REM         If this variable is not set or is set to the wrong mcs user:
REM           set USERNAME=mymcsusername
REM      2) configure
REM      3) build the release
REM      this will create an mpich2 directory under the current directory
REM    makewindist --with-configure
REM      1) configure the current directory
REM      2) build the release
REM      mpich2 must be the current directory
REM    makewindist --with-curdir
REM      1) build the release
REM      mpich2 must be the current directory and it must have been configured
REM
REM
REM Prerequisites:
REM  Microsoft Developer Studio .NET 2003 or later
REM  Intel Fortran 8.0 or later
REM  cygwin with at least ssh, cvs, and perl
REM  cygwin must be in your path so commands like cvs can be executed
REM  This batch file should be run in a command prompt with the MS DevStudio environment variables set
GOTO END
:AFTERHELP
IF "%DevEnvDir%" == "" GOTO WARNING
GOTO AFTERWARNING
:WARNING
REM
REM Warning: It is recommended that you use the prompt started from the "Visual Studio Command Prompt" shortcut in order to set the paths to the tools.  You can also call vsvars32.bat to set the environment before running this script.
PAUSE
if %errorlevel% NEQ 0 goto END
REM
:AFTERWARNING
IF "%1" == "--with-checkout" GOTO CHECKOUT
GOTO AFTERCHECKOUT
:CHECKOUT
set CVS_RSH=ssh
IF "%2" == "" GOTO CHECKOUT_HEAD
cvs -d :ext:%USERNAME%@%CVS_HOST%:/home/MPI/cvsMaster export -r %2 mpich2allWithMPE
GOTO AFTER_CHECKOUT_HEAD
:CHECKOUT_HEAD
cvs -d :ext:%USERNAME%@%CVS_HOST%:/home/MPI/cvsMaster export -r HEAD mpich2allWithMPE
:AFTER_CHECKOUT_HEAD
if %errorlevel% NEQ 0 goto CVSERROR
pushd mpich2
GOTO CONFIGURE
:AFTERCHECKOUT
IF "%1" == "--with-configure" GOTO CONFIGURE
GOTO AFTERCONFIGURE
:CONFIGURE
REM bash -c "echo cd `pwd` && echo maint/updatefiles" > bashcmds.txt
REM bash --login < bashcmds.txt
echo cd /sandbox/%USERNAME% > sshcmds.txt
echo mkdir dotintmp >> sshcmds.txt
echo cd dotintmp >> sshcmds.txt
if "%2" == "" GOTO EXPORT_HEAD
echo cvs -d /home/MPI/cvsMaster export -r %2 mpich2allWithMPE >> sshcmds.txt
GOTO AFTER_EXPORT_HEAD
:EXPORT_HEAD
echo cvs -d /home/MPI/cvsMaster export -r HEAD mpich2allWithMPE >> sshcmds.txt
:AFTER_EXPORT_HEAD
echo cd mpich2 >> sshcmds.txt
echo maint/updatefiles >> sshcmds.txt
echo tar cvf dotin.tar `find . -name "*.h.in"` >> sshcmds.txt
echo gzip dotin.tar >> sshcmds.txt
echo exit >> sshcmds.txt
ssh -l %USERNAME% %CVS_HOST% < sshcmds.txt
scp %USERNAME%@%CVS_HOST%:/sandbox/%USERNAME%/dotintmp/mpich2/dotin.tar.gz .
ssh -l %USERNAME% %CVS_HOST% rm -rf /sandbox/%USERNAME%/dotintmp
del sshcmds.txt
tar xvfz dotin.tar.gz
del dotin.tar.gz
cscript winconfigure.wsf --cleancode --enable-timer-type=queryperformancecounter
GOTO BUILD_RELEASE
:AFTERCONFIGURE
IF "%1" == "--with-curdir" GOTO BUILD
REM
REM Unknown option: %1
REM
GOTO HELP
:BUILD
:BUILD_DEBUG
IF "%2" == "" GOTO BUILD_RELEASE
REM Building the Debug targets
devenv.com mpich2.sln /build ch3sockDebug
if %errorlevel% NEQ 0 goto BUILDERROR
devenv.com mpich2.sln /project mpich2s /build ch3sockDebug
if %errorlevel% NEQ 0 goto BUILDERROR
devenv.com mpich2.sln /build Debug
if %errorlevel% NEQ 0 goto BUILDERROR
devenv.com mpich2.sln /build fortDebug
if %errorlevel% NEQ 0 goto BUILDERROR
devenv.com mpich2.sln /build gfortDebug
if %errorlevel% NEQ 0 goto BUILDERROR
devenv.com mpich2.sln /build sfortDebug
if %errorlevel% NEQ 0 goto BUILDERROR
devenv.com mpich2.sln /build ch3shmDebug
REM if %errorlevel% NEQ 0 goto BUILDERROR
REM devenv.com mpich2.sln /build ch3sshmDebug
if %errorlevel% NEQ 0 goto BUILDERROR
devenv.com mpich2.sln /build ch3ssmDebug
REM if %errorlevel% NEQ 0 goto BUILDERROR
REM devenv.com mpich2.sln /build ch3ibIbalDebug
REM if %errorlevel% NEQ 0 goto BUILDERROR
REM devenv.com mpich2.sln /build ch3essmDebug
REM if %errorlevel% NEQ 0 goto BUILDERROR
devenv.com mpich2.sln /build ch3sockmtDebug
if %errorlevel% NEQ 0 goto BUILDERROR
devenv.com examples\examples.sln /project cpi /build Debug
if %errorlevel% NEQ 0 goto BUILDERROR
:BUILD_RELEASE
devenv.com mpich2.sln /build ch3sockRelease
if %errorlevel% NEQ 0 goto BUILDERROR
devenv.com mpich2.sln /project mpich2s /build ch3sockRelease
if %errorlevel% NEQ 0 goto BUILDERROR
devenv.com mpich2.sln /build ch3sockPRelease
if %errorlevel% NEQ 0 goto BUILDERROR
devenv.com mpich2.sln /project mpich2s /build ch3sockPRelease
if %errorlevel% NEQ 0 goto BUILDERROR
devenv.com mpich2.sln /build Release
if %errorlevel% NEQ 0 goto BUILDERROR
devenv.com mpich2.sln /build fortRelease
if %errorlevel% NEQ 0 goto BUILDERROR
devenv.com mpich2.sln /build gfortRelease
if %errorlevel% NEQ 0 goto BUILDERROR
devenv.com mpich2.sln /build sfortRelease
if %errorlevel% NEQ 0 goto BUILDERROR
devenv.com mpich2.sln /build ch3shmRelease
if %errorlevel% NEQ 0 goto BUILDERROR
devenv.com mpich2.sln /build ch3shmPRelease
REM if %errorlevel% NEQ 0 goto BUILDERROR
REM devenv.com mpich2.sln /build ch3sshmRelease
REM if %errorlevel% NEQ 0 goto BUILDERROR
REM devenv.com mpich2.sln /build ch3sshmPRelease
if %errorlevel% NEQ 0 goto BUILDERROR
devenv.com mpich2.sln /build ch3ssmRelease
if %errorlevel% NEQ 0 goto BUILDERROR
devenv.com mpich2.sln /build ch3ssmPRelease
REM if %errorlevel% NEQ 0 goto BUILDERROR
REM devenv.com mpich2.sln /build ch3ibIbalRelease
REM if %errorlevel% NEQ 0 goto BUILDERROR
REM devenv.com mpich2.sln /build ch3ibIbalPRelease
REM if %errorlevel% NEQ 0 goto BUILDERROR
REM devenv.com mpich2.sln /build ch3essmRelease
REM if %errorlevel% NEQ 0 goto BUILDERROR
REM devenv.com mpich2.sln /build ch3essmPRelease
REM if %errorlevel% NEQ 0 goto BUILDERROR
devenv.com mpich2.sln /build ch3sockmtRelease
if %errorlevel% NEQ 0 goto BUILDERROR
devenv.com mpich2.sln /build ch3sockmtPRelease
if %errorlevel% NEQ 0 goto BUILDERROR
devenv.com mpich2.sln /project cxx /build Debug
if %errorlevel% NEQ 0 goto BUILDERROR
devenv.com src\util\logging\rlog\rlogtools.sln /build Release
if %errorlevel% NEQ 0 goto BUILDERROR
devenv.com mpich2.sln /build fmpe
if %errorlevel% NEQ 0 goto BUILDERROR
cd maint
call makegcclibs.bat
cd ..
devenv.com examples\examples.sln /project cpi /build Release
if %errorlevel% NEQ 0 goto BUILDERROR
devenv.com mpich2.sln /build Installer
if %errorlevel% NEQ 0 goto BUILDERROR
GOTO END
:BUILDERROR
REM Build failed, exiting
GOTO END
:CVSERROR
REM cvs returned a non-zero exit code while attempting to check out mpich2
GOTO END
:END
IF "%1" == "--with-checkout" popd
