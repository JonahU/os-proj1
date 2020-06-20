all:
	clang mysh.c exec.c strquote.c -O3 -o mysh
debug:
	gcc -DDEBUG=1 mysh.c exec.c strquote.c -g -o mysh
jit:
	clang -DDEBUG=1 mysh.c exec.c strquote.c -Wall -O3 -o mysh && ./mysh
clean:
	rm -f mysh
