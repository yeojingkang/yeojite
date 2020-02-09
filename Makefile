# Macros  =================================

CC=gcc
CFLAGS=-Wall -Wextra -pedantic -Werror -std=c99
ERASE=rm
NAME=editor

# Targets =================================
main: main.c estring.c estring.h
	$(CC) $(CFLAGS) -o $(NAME) main.c estring.c

clean:
	$(ERASE) $(NAME).exe
