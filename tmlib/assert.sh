# assert.sh

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

