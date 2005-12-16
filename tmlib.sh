# tmlib.sh

# This file contains utilities useful for writing testfiles.

# TODO: should these routines be prefixed by "TM"?


# Current functions:
# ASSERT: Stop a test if a condition fails
# ATEXIT: Ensure something gets cleaned up even if the test fails.
# MKFILE MKFILE_EMPTY: create temporary files
# MKDIR: create a temporary directory



#
# ASSERT
#
# If you include this file from either a config file or your
# test file (". assert.sh" or "source assert.sh"),
# you can then use asserts in your test scripts.

# usage:
#   . assert.sh
# 	assert 42 -eq $((0x2A))		# true, so test continues as normal
# 	assert 1 -eq 2				# false, so the test is aborted.

ASSERT ()
{
	if [ ! $* ]; then
		msg=''
		if [ ${BASH_VERSINFO[0]} -ge 3 ]; then
			# bash2 doesn't provide BASH_SOURCE or BASH_LINENO
			msg=" on ${BASH_SOURCE[1]} line ${BASH_LINENO[0]}"
		fi
		ABORT assertion failed$msg: \"$*\"
	fi  
}    


#
# TRAP
#
# This function makes trap behave more like atexit(3), where you can
# install multiple commands to execute for the same condition.
# I'm surprised that Bash doesn't do this by default.
#
# NOTE: you must not mix use of TRAP and trap on the same conndition.
# The builtin trap will remove TRAP's condition, and all the commands
# installed using TRAP will not be run.
#
# Call this routine exactly like the trap builtin: "TRAP cmd cond"
#
# Example:
#
#     TRAP "echo debug!" DEBUG
#

TRAP ()
{
	# install the trap if this is the first time TRAP is called for
	# the given condition.  (Is there any way to get rid of "local var"??)

	local var=TRAP_$2
	if [ ! -n "${!var}" ]; then
		trap "eval \"\$TRAP_$2\"" $2
	fi


	# This just adds $1 to the front of the string given by TRAP_$2.
	# In Perl:		$TRAP{$2} = $1.($TRAP{$2} ? "; " : "").$TRAP{$2}

	eval TRAP_$2=\"$1'${'TRAP_$2':+; }$'TRAP_$2\"
}


#
# ATEXIT
#
# This behaves just like atexit(3) call.  Supply a command to be executed
# when the shell exits.  Commands are executed in the reverse order that
# they are supplied.
#
# Example:  (will produce "BA" on stdout when the test ends)
#
#     ATEXIT echo A
#     ATEXIT echo -n B

ATEXIT ()
{
	TRAP "$*" EXIT
}


#
# MKFILE
#
# Creates a file and assigns the new filename to the given variable.
# Fills in the new file with the supplied data.  Ensures that it is
# deleted when the test ends.
#
# argument 1: varname, the name of the variable that will contain the new filename.
# argument 2: filename, (optional) the name/fullpath to give the file.
#
# You need to be aware that if you supply an easily predictable filename
# (such as a PID), you are exposing your users to symlink attacks.  You
# should never supply a filename unless you know EXACTLY what you are doing.
#
# Examples:
#
# create a new file with a random name in $TMPDIR or /tmp:
#
#     MKFILE fn <<-EOL
#     	Initial file contents.
#     EOL
#     cat "$fn"		<-- prints "Initial file contents."
#
# create a new empty file with the given name (open to symlink attack,
# DO NOT USE UNLESS YOU ARE SURE WHAT YOU ARE DOING).
#
#     MKFILE ttf /tmp/$mydir/tt1 < /dev/null
#

MKFILE ()
{
	local name=${2-`mktemp -t tmtest.XXXXXX || ABORT MKFILE: could not mktemp`}
	eval "$1='$name'"
	cat > "$name"
	ATEXIT "rm '$name'"
}


#
# MKFILE_EMPTY
#
# I can't figure out how to get bash to bail instead of blocking.
# Therefore, if you just want to create an empty file, you either
# call MKFILE piped from /dev/null or just call MKFILE_EMPTY.
#

MKFILE_EMPTY ()
{
	MKFILE "$*" < /dev/null
}


#
# MKDIR
#
# Like MKFILE, but creates a directory instead of a file.  If you
# supply a directory name, and that directory already exists, then
# MKDIR ensures it is deleted when the script ends.  The directory
# will not be deleted if it still contains any files.
#
# argument 1: varname, the name of the variable that will contain the new directory name.
# argument 2: dirname, (optional) the name/fullpath to give the directory.
#
# Examples:
#
# create a new directory with a random name in $TMPDIR or /tmp:
#
#     MKDIR dn
#     cd "$dn"
#

MKDIR ()
{
	local name=${2}
	if [ -z "$name" ]; then
		name=`mktemp -d -t tmtest.XXXXXX || ABORT MKDIR: could not mktemp`
	else
		[ -d $name ] || mkdir --mode 0600 $name || ABORT "MKDIR: could not 'mkdir \"$name\"'"
	fi

	eval "$1='$name'"
	ATEXIT "rmdir '$name'"
}

