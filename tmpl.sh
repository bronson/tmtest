set -e

# PS4='TEST: '
# set -x

ADD_CWD_TO_PATH () { PATH="$(dirname ${BASH_SOURCE[1]}):$PATH"; }

TITLE='%(TESTFILE)'
AUTHOR='%(AUTHOR)'
DATE='%(DATE)'

echo 'start.' >&%(STATUSFD)

%(CONFIG_FILES)

echo 'ready.' >&%(STATUSFD)

STDOUT () { exit 0; }
STDOUT: () { exit 0; }
STDERR () { exit 0; }
STDERR: () { exit 0; }
RESULT () { exit 0; }
RESULT: () { exit 0; }
MODIFY () { exit 0; }
MODIFY: () { exit 0; }

echo 'running: %(TESTFILE)' >&%(STATUSFD)
exec >&%(OUTFD) 2>&%(ERRFD)
LINENO=0
%(TESTEXEC)
