# Simple Makefile for building a request tester.


main:
	clang -g -fsanitize=address -fsanitize-undefined-trap-on-error -Wall -Werror -Wno-unused req.c -o req -lcurl

