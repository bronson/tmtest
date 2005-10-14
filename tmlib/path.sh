# path.sh
#
# It's probably more clear just to manually set PATH in your config
# file.  But, if you want it, here's how to automate it.  Simply
# call the function to add this config file's directory to the front
# of the PATH variable.
#
# usage:
#      ADD_CWD_TO_PATH
#

ADD_CWD_TO_PATH () { PATH="$MYDIR:$PATH"; }

