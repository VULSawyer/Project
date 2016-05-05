#!/bin/bash

func_list=$1

shift

file_list=`echo $@ | sed "s/$func_list//"`

while read function ; do
    grep -q "\<$function\>" $file_list && echo \*\*\* \
		Error: API name used without prefix: $function && exit 1
done

exit 0
