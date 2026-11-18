CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -Werror -D_POSIX_C_SOURCE=200809L

all: parent child
parent: parent.c parent_input.c mapping.c parent_input.h mapping.h status.h
	$(CC) $(CFLAGS) -o $@ parent.c parent_input.c mapping.c
child: child.c sum.c mapping.c sum.h mapping.h status.h
	$(CC) $(CFLAGS) -o $@ child.c sum.c mapping.c
clean:
	rm -f parent child *.o
.PHONY: all clean
