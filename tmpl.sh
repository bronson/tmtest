echo START >&%(STATUSFD)

set -e
# PS4='TEST: '
# set -x

ADD_CWD_TO_PATH () { PATH="$(dirname ${BASH_SOURCE[1]}):$PATH"; }
DISABLED () { echo "DISABLED: $*" >&%(STATUSFD); exit 0; }
DISABLED: () { echo "DISABLED: $*" >&%(STATUSFD); exit 0; }

TITLE='%(TESTFILE)'
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

