# files.sh

# This file makes it easier to create temporary files.
# requires: ATEXIT from trap.sh

# TODO: I can't figure out how to execute a non-blocking read in bash.
# For now, the caller will have to add "</dev/null" after each function
# call to prevent blocking when we try to fill the new file with data.



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
#     MKFILE fn <<EOL
#     Initial file contents.
#     EOL
#     cat "$fn"		<-- prints "Initial file contents."
#
# create a new empty file with the given name (open to attack, see above):
#
#     MKFILE ttf /tmp/tt1 < /dev/null
#     
#

MKFILE ()
{
	local name=${2-`mktemp -t tmtest.XXXXXX || ABORT MKFILE: could not mktemp`}
	eval "$1='$name'"
	cat > "$name"
	ATEXIT "rm '$name'"
}



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
#     MKFILE dn
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

