# files.sh
# requires: trap.sh

# This file makes it easier to create temporary files.
# Note that you must include the "trap.sh" file as well.


# We want functions to:

# create a file with a random name in /tmp, pre-filled or not
# create a file with a given name in /tmp, pre-filled or not
# create a directory with a random name in /tmp, pre-filled or not
# create a directory with a given name in /tmp, pre-filled or not

# TODO: I can't figure out how to execute a non-blocking read in bash.
# For now, the caller will have to add "</dev/null" after each function
# call to prevent blocking when we try to fill the new file with data.


# Creates a file and ensures that it gets deleted when the script ends.
# Fills it in with the supplied data.  If you want to create an empty
# file you must supply the data from stdin.
#
# no arguments: auto-generates the filename in the appropriate temp dir.
# 	use this whenever possible.
# single argument: the name or full path of the file to make
#
# Example:
#     MKFILE <<EOL
#     Initial file contents.
#     EOL
#
#     MKFILE ("empty") < /dev/null


MKFILE ()
{
	local name
	name=${1-`mktemp -t tmtest.XXXXXX || ABORT could not mktemp`}
	cat > "$name"

#	trap "echo HAHAHAHHAAAAA!" EXIT
#	trap "rm '$fn'" EXIT
	echo "$name"

#	name=${1-DEFNAME}
#	if [ `expr index $name /` -eq 0 ]; then
#		name=${TMPDIR-/tmp}/$name
#	fi
}


MKDIR () { echo; }
