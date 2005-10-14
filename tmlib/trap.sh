# trap.sh

# This file makes bash's built-in "trap" a little more useful.
# It also supplies ATEXIT, a command that allows you to specify
# commands that need to be run when the script finishes.


# This command makes trap behave more like atexit(3), where you can
# install multiple commands for the same condition.
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


# This behaves just like atexit.  Supply a command to be executed
# when bash exits.  Commands are executed in the reverse order that
# they are supplied.
#
# Example:  (will produce "BA" on stdout when the test ends)
#
#     ATEXIT echo -n B
#     ATEXIT echo -n A

ATEXIT ()
{
	TRAP "$*" EXIT
}

