# TODO: write a function that allows you to disable the test.
# TODO: write a function to make it easy to create crfiles.


set -e  # exit at the very first command that returns nonzero

TITLE='%(TITLE)'
AUTHOR='%(AUTHOR)'
DATE='%(DATE)'

_TM_ACTUAL_STDOUT='%(STDOUT)'
_TM_ACTUAL_STDERR='%(STDERR)'
_TM_OUT_OF_BAND='%(OUT_OF_BAND)'
_TM_OOB_SEPARATOR='%(OOB_SEPARATOR)'


for f in %(CONFIG_FILES)
do
    if [[ -f $f ]]; then
        echo "reading config file: $f"
        . $f
    fi
done


. t.test > "$_TM_ACTUAL_STDOUT" 2> "$_TM_ACTUAL_STDERR"

rm -f "$_TM_OUT_OF_BAND"
for X in STDOUT STDERR EXITNO; do
    if [[ ${!X+exists} ]]; then
        echo "$_TM_OOB_SEPARATOR" >> "$_TM_OUT_OF_BAND"
        echo "$X: ${!X}" >> "$_TM_OUT_OF_BAND"
    fi
done

