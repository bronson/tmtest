echo START >&%(STATUSFD)

ABORT ()  { echo "ABORTED: $*" >&%(STATUSFD); exit 0; }

DISABLED  () { echo "DISABLED: $*" >&%(STATUSFD); exit 0; }
DISABLE   () { DISABLED $*; }

%(CONFIG_FILES)

echo PREPARE >&%(STATUSFD)

STDOUT () { exit 0; }
STDOUT: () { exit 0; }
STDERR () { exit 0; }
STDERR: () { exit 0; }

echo RUNNING >&%(STATUSFD)
exec >&%(OUTFD) 2>&%(ERRFD) %(OUTFD)>&- %(ERRFD)>&-
%(TESTEXEC)

echo DONE >&%(STATUSFD)
