echo START >&%(STATUSFD)

ABORT ()  { echo "ABORTED: $*" >&%(STATUSFD); exit 0; }
ABORT: () { ABORT $*; }

ATEXIT ()  { echo "ATEXIT: $*" >&%(STATUSFD); }
ATEXIT: () { ATEXIT $*; }

DISABLED  () { echo "DISABLED: $*" >&%(STATUSFD); exit 0; }
DISABLED: () { DISABLED $*; }
DISABLE   () { DISABLED $*; }
DISABLE:  () { DISABLED $*; }

AUTHOR='%(AUTHOR)'
DATE='%(DATE)'
TESTDIR='%(TESTDIR)'
TESTFILE='%(TESTFILE)'

%(CONFIG_FILES)

echo PREPARE >&%(STATUSFD)

STDOUT () { exit 0; }
STDOUT: () { exit 0; }
STDERR () { exit 0; }
STDERR: () { exit 0; }
RESULT () { exit 0; }
RESULT: () { exit 0; }
MODIFY () { exit 0; }
MODIFY: () { exit 0; }

echo 'RUNNING: %(TESTFILE)' >&%(STATUSFD)
MYDIR='%(TESTDIR)'
MYFILE='%(TESTFILE)'
exec >&%(OUTFD) 2>&%(ERRFD)
exec %(OUTFD)>&-
exec %(ERRFD)>&-
LINENO=0
%(TESTEXEC)

echo DONE >&%(STATUSFD)

