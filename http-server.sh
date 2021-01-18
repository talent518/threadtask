#!/bin/bash

host=${1:-127.0.0.1}
port=${2:-5000}

function request() {
		echo -e "\e[31m$1\e[0m"
	# connect
	exec 8<>/dev/tcp/$host/$port
	if [ $? -ne 0 ]; then
		echo connect to $ip:80 failure
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
request DISCON "GET / HTTP/1.0\r\n" 0
request OPTROOT "OPTIONS / HTTP/1.0\r\n\r\n"
request LISTHTML "GET / HTTP/1.0\r\n\r\n"
request LISTJSON "GET /?json HTTP/1.0\r\n\r\n"
request HELLO "GET /hello-json HTTP/1.0\r\n\r\n"
request NULL "GET /null HTTP/1.0\r\n\r\n"
request CHUNKED "GET /chunked HTTP/1.0\r\n\r\n"
request GETREQ "GET /request-info?json HTTP/1.0\r\n\r\n"
n=$(stat -c %s Makefile)
request PUTREQ "PUT /request-info?json HTTP/1.0\r\nContent-Length: $n\r\n\r\n" '' Makefile
request OPTDAV "OPTIONS /dav HTTP/1.0\r\n\r\n"
request PUTDAV "PUT /dav/file.txt HTTP/1.0\r\nContent-Length: $n\r\n\r\n" '' Makefile
request DOWNLOAD "GET /dav/file.txt HTTP/1.0\r\n\r\n"
request DELETE "DELETE /dav/file.txt HTTP/1.0\r\n\r\n"

