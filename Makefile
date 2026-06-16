CC = clang
CFLAGS = -Wall -Wextra -O3 -std=c11 -D_GNU_SOURCE

research_explorer: main.o trie.o parser.o server.o
	$(CC) $(CFLAGS) -o research_explorer main.o trie.o parser.o server.o

main.o: main.c trie.h parser.h server.h
	$(CC) $(CFLAGS) -c main.c

trie.o: trie.c trie.h
	$(CC) $(CFLAGS) -c trie.c

parser.o: parser.c parser.h
	$(CC) $(CFLAGS) -c parser.c

server.o: server.c server.h trie.h parser.h
	$(CC) $(CFLAGS) -c server.c

clean:
	rm -f *.o research_explorer
