echo START >&%(STATUSFD)

ABORT ()  { echo "ABORTED: $*" >&%(STATUSFD); exit 0; }
ABORT: () { ABORT $*; }

ADD_CWD_TO_PATH () { PATH="$(dirname ${BASH_SOURCE[1]}):$PATH"; }

ATEXIT ()  { echo "ATEXIT: $*" >&%(STATUSFD); }
ATEXIT: () { ATEXIT $*; }

DISABLED  () { echo "DISABLED: $*" >&%(STATUSFD); exit 0; }
DISABLED: () { DISABLED $*; }
DISABLE   () { DISABLED $*; }
DISABLE:  () { DISABLED $*; }

TESTFILE='%(TESTFILE)'
TITLE='%(TITLE)'
AUTHOR='%(AUTHOR)'
DATE='%(DATE)'

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
exec >&%(OUTFD) 2>&%(ERRFD)
LINENO=0
%(TESTEXEC)

echo DONE >&%(STATUSFD)

