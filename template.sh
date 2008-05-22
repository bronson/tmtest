echo START >&%(STATUSFD)

ABORT ()  { echo "ABORTED: $*" >&%(STATUSFD); exit 0; }
ABORT: () { ABORT $*; }

DISABLED  () { echo "DISABLED: $*" >&%(STATUSFD); exit 0; }
DISABLED: () { DISABLED $*; }
DISABLE   () { DISABLED $*; }
DISABLE:  () { DISABLED $*; }

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

echo 'RUNNING: %(TESTFILE)' >&%(STATUSFD)
MYDIR='%(TESTDIR)'
MYFILE='%(TESTFILE)'
exec >&%(OUTFD) 2>&%(ERRFD) %(OUTFD)>&- %(ERRFD)>&-
%(TESTEXEC)

echo DONE >&%(STATUSFD)
