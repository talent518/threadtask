#!/bin/bash

host=${host:-127.0.0.1}
port=${port:-5000}
file=${file:-Makefile}
times=${times:-0}

size=$(stat -c %s "$file")

function request() {
	echo -e "\e[33m$1\e[0m $host:$port" >&2

	# connect
	exec 8<>/dev/tcp/$host/$port
	if [ $? -ne 0 ]; then
		echo -e "\e[31mconnect to $host:$port failure\e[0m"
		exit 1
	fi

	# request
	echo -ne $2
	echo -ne $2 >&8
	if [ -n "$4" -a -f "$4" ]; then
		cat $4
		cat $4 >&8
	fi

	# response
	if [ -z "$3" ]; then
		cat <&8
		echo
	else
		sleep $3
	fi

	# close input and output
	exec 8<&-
	exec 8>&-
}

function test_cases() {
	request DISCON "GET / HTTP/1.0\r\n" 0
	request OPTROOT "OPTIONS / HTTP/1.0\r\n\r\n"
	request LISTHTML "GET / HTTP/1.0\r\n\r\n"
	request LISTJSON "GET /?json HTTP/1.0\r\n\r\n"
	request HELLO "GET /hello-json HTTP/1.0\r\n\r\n"
	request NULL "GET /null HTTP/1.0\r\n\r\n"
	request CHUNKED "GET /chunked HTTP/1.0\r\n\r\n"
	request GETREQ "GET /request-info?json HTTP/1.0\r\n\r\n"
	request PUTREQ "PUT /request-info?json HTTP/1.0\r\nContent-Length: $size\r\n\r\n" '' $file
	request OPTDAV "OPTIONS /dav HTTP/1.0\r\n\r\n"
	request PUTDAV "PUT /dav/file.txt HTTP/1.0\r\nContent-Length: $size\r\n\r\n" '' $file
	request DOWNLOAD "GET /dav/file.txt HTTP/1.0\r\n\r\n"
	request DELETE "DELETE /dav/file.txt HTTP/1.0\r\n\r\n"
}

if [ $times -gt 0 ]; then
	while [ $times -gt 0 ]; do
		times=$(expr $times - 1)
		test_cases
	done
else
	while true; do
		test_cases
	done
fi

