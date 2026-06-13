#ifndef SERVER_H
#define SERVER_H

#include "trie.h"

typedef struct {
    char category[64];
    int count;
} CategoryCount;

typedef struct {
    int year;
    int count;
} YearCount;

typedef struct {
    char author[128];
    int count;
} AuthorCount;

// Global references to mapped dataset
extern char* g_file_data;
extern size_t g_file_size;
extern size_t* g_line_offsets;
extern size_t g_line_count;
extern TrieNode* g_trie_root;

// Starts the HTTP server on the specified port
void start_server(int port);

#endif // SERVER_H
