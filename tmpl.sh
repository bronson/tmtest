set -e

# PS4='TEST: '
# set -x

TITLE='%(TESTFILE)'
AUTHOR='%(AUTHOR)'
DATE='%(DATE)'

for f in %(CONFIG_FILES)
do
    if [[ -f "$f" ]]; then
        . "$f"
    fi
done

STDOUT () { exit 0; }
STDERR () { exit 0; }
RESULT () { exit 0; }
MODIFY () { exit 0; }

. '%(TESTFILE)' >&%(OUTFD) 2>&%(ERRFD)
