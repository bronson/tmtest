# assert.sh
# 27 Jan 2004


# If you include this file from either a config file or your
# test file (". assert.sh" or "source assert.sh"),
# you can then use asserts in your test scripts.

# usage:
#   . assert.sh
# 	assert 42 -eq $((0x2A))		# true, so test continues as normal
# 	assert 1 -eq 2				# false, so the test is aborted.


assert ()
{
	if [ ! $* ]; then
		line=${BASH_LINENO[0]}
		file=${BASH_SOURCE[1]}
		ABORT assertion failed on $file line $line: \"$*\"
	fi  
}    

