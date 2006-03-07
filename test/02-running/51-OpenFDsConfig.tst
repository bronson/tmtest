# Ensure we don't leak fds to the running test.
# Ideally, there would be NO open FDs.  This will be the case for tmtest 0.98.


# creates a temporary directory DD:
# /tmp/DD/tmtest.conf   -- stores the open fds in $openfds
# /tmp/DD/tt.test       -- prints $openfds as its test result


MKDIR dd

MKFILE tt1 "$dd/tmtest.conf" <<'EOL'
	openfds=$(
		for i in `seq 3 255`; do
			exec 2>/dev/null
			echo -n >&$i && echo open: $i
		done
	)
EOL


MKFILE tt2 "$dd/tt.test" <<'EOL'
	echo "$openfds"
EOL

while [ -f /tmp/s ]; do sleep 1; done

$tmtest $args -o "$dd" | INDENT


STDOUT:
    for i in `seq 3 255`; do
    exec 2>/dev/null
    echo -n >&$i && echo open: $i
    done
    STDOUT:
    open: 4
    open: 6
    open: 7
    open: 10
