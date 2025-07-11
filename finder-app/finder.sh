#!/bin/bash

FILESDIR=$1
SEARCHSTR=$2

if [[ -z $FILESDIR || -z $SEARCHSTR ]];then
    echo "filesdir or searchstr argument is missing"
    exit 1
fi

if [[ ! -d $FILESDIR ]];then
    echo "$FILESDIR does not represent a directory on the filesystem"
    exit 1
fi

OUTPUT=$( find ${FILESDIR}/*)

X=0
Y=0

for file in $OUTPUT;do
    if [[ -f $file ]];then
        matching_lines=$(cat $file | grep $SEARCHSTR | wc -l)
        X=$(($X+1))
        Y=$(($Y+$matching_lines))
    fi
done

echo  "The number of files are $X and the number of matching lines are $Y"
exit 0