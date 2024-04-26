# Simple Makefile for building a request tester.
CC=clang

main:
	$(CC) -Wall -Werror -Wno-unused req.c -o req -lcurl

dev:
	clang -g -fsanitize=address -fsanitize-undefined-trap-on-error -Wall -Werror -Wno-unused req.c -o req -lcurl

