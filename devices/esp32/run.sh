#!/usr/bin/env bash
trap "trap - SIGTERM && kill -- -$$" SIGINT SIGTERM EXIT

PORT=$1
FILE=$2

FMT_CMD="\e[1m" # bold
FMT_ARG="\e[4m\e[92m" # light green, underline
FMT_RST="\e[0m" # reset

if [[ -z $PORT || -z $FILE ]]; then
	echo -e "Usage: ${FMD_CMD}$(basename $0)${FMT_RST} ${FMT_ARG}PORT${FMT_RST} ${FMT_ARG}FILE${FMT_RST}"
	exit 1
fi

if [[ ! -c $PORT ]]; then
	echo "$(basename $0): $PORT: No such file, or isn't a character special file"
	exit 2
fi

if [[ ! -f $FILE ]]; then
	echo "$(basename $0): $FILE: No such file"
	exit 2
fi

set -euo pipefail

# filesize in bytes, decimal
DEC_FILESIZE=$(wc -c < $FILE)
# convert to 4 escaped hex bytes, little-endian (e.g. 607 -> \x5f\x02\x00\x00)
ESC_FILESIZE=$(printf '%08x' $DEC_FILESIZE | fold -w2 | sed 's/../\\x&/;1!G;h;$!d;s/\n//g')

# setup port
stty -F $PORT speed 115200 -echo -icrnl -opost -onlcr > /dev/null

# print output from port
cat $PORT &

# send filesize and program
printf "$ESC_FILESIZE" > $PORT
cat $FILE > $PORT

wait
