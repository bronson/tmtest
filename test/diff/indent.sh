# This function indents each line of its stdin with a tab.

INDENT ()
{
	while read LINE; do
		echo $'\t'"$LINE"
	done
}

