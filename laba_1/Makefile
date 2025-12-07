CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -pedantic
TARGETS = parent child

all: $(TARGETS)

parent: parent.c
	$(CC) $(CFLAGS) -o parent parent.c

child: child.c
	$(CC) $(CFLAGS) -o child child.c

clean:
	rm -f $(TARGETS) *.txt

run: all
	./parent

strace-demo: all
	strace -f -e trace=fork,pipe,execve,waitpid ./parent 2>&1 | grep -E "fork|pipe|execve|waitpid|Enter"

.PHONY: all clean run strace-demo