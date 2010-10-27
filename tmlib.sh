# tmlib.sh

# Utilities useful for writing tmtest testfiles and other shell scripts.
# To use them, just include this file: . "$HOME/.tmlib.sh"
# This file is covered by the MIT License.

# DO NOT EDIT THIS FILE!  Edit /etc/tmtest.conf or ~/.tmtestrc instead.
# This file is replaced when you reinstall tmtest and your changes
# will be lost!


# tmlib functions:
#
# TRAP:     Execute a command when an exception happens
# ATEXIT:   Ensure a command runs even if the test fails (usually to clean up).
# MKFILE:   Creates a temporary file with the given contents.
# MKDIR:    create a temporary directory
# REPLACE:  replaces literal text with other literal text (no regexes).


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
#     ATEXIT "echo A"
#     ATEXIT "echo -n B"

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
# should never supply your own filename unless you know EXACTLY what you
# are doing.
#
# If a command produces the filename in its output (bad, because the
# filename always changes), you can fix it with the <REPLACE> function.
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
# Remove the file name from a command's output:
#     echo "$fn" | REPLACE "$fn" TMPFILE   <-- prints "TMPFILE"
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
# MKDIR
#
# Like MKFILE, but creates a directory instead of a file.  If you
# supply a directory name, and that directory already exists, then
# MKDIR ensures it is deleted when the script ends.
#
# If the directory still contains files when the test ends, it
# will not be deleted and the test will abort with an error.
#
# argument 1: varname, the name of the variable that will contain the new directory name.
# argument 2: dirname, (optional) the name/fullpath to give the directory.
#
#	NOTE: unless you really know what you are doing, specifying argument2
#   is a major security risk.  Always use the single argument version.
#   The one exception is if you're creating a directory inside another
#   directory that was created with the single arg.
#
# If a command produces the directory name in its output (bad, because the
# name always changes), you can fix it with the <REPLACE> function.
#
# Examples:
#
# create a new directory with a random name in $TMPDIR or /tmp:
#
#     MKDIR dn
#     cd "$dn"
#
# Remove the directory name from a command's output:
# 	svn co svn://repo "$dn/local" | REPLACE "$dn" TMPDIR
#
# TODO: should emulate mkdir -p too.  Right now tmtest forces you to
# call MKDIR for each dir you want to create.  Too wordy.
#

MKDIR ()
{
	local name=${2}
	if [ -z "$name" ]; then
		name=`mktemp -d -t tmtest.XXXXXX || ABORT MKDIR: could not mktemp`
	else
		[ -d $name ] || mkdir --mode 0700 $name || ABORT "MKDIR: could not 'mkdir \"$name\"'"
	fi

	eval "$1='$name'"
	ATEXIT "rmdir '$name' || ABORT 'MKDIR: '$name' was not empty, can't delete it!"
}


#
# REPLACE
#
# Replaces all occurrences of the first argument with the second argument.
# Takes any number of arguments:
#   echo "'a'" | REPLACE "'a'" "/\\a/\\"
# replaces all occurrences of 'a' with /\a/\
#   REPLACE abc ABC def DEF ghi GHI
# converts the first nine characters of the alphabet to upper case
# All non-control characters are safe: quotes, slashes, etc.
# You can of course use sed if you want to replace with regexes.
# Replace does not work if a newline is embedded in either argument.
#
# Three layers of escaping!  (bash, perlvar, perlre)  This is insane.
# I wish sed or awk would work with raw strings instead of regexes.
# Why isn't a replace utility a part of Gnu coreutils?
#

REPLACE()
{
    # unfortunately bash can't handle this substitution itself because it
    # must work on ' and \ simultaneously. Send it to perl for processing.

     ( while [ "$1" != "" ]; do echo "$1"; shift; done; echo; cat) | perl -e "my %ops; while(<>) { chomp; last if \$_ eq ''; \$_ = quotemeta(\$_); \$ops{\$_} = <>; chomp(\$ops{\$_}); warn 'odd number of arguments to REPLACE', last if \$ops{\$_} eq ''; } while(<>) { for my \$k (keys %ops) { s/\$k/\$ops{\$k}/g } print or die \"REPLACE: Could not print: \$!\\\\n\"; }"
}
