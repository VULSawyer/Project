#!/bin/bash

while read function ; do
    echo \#define bacnet_$function $function
done
