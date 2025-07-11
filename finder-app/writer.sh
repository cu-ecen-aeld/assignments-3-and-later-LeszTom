#!/bin/bash

FILE_PATH=$1
TEXT=$2

if [[ -z $FILE_PATH || -z $TEXT ]];then
    echo "file or text is missing"
    exit 1
fi

DIR=$( dirname ${FILE_PATH})
[[ ! -d $DIR ]] && mkdir -p ${DIR}

echo $TEXT > $FILE_PATH