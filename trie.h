#ifndef TRIE_H
#define TRIE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct TrieNode {
    char ch;
    bool is_word;
    uint32_t address; // Simulated heap address (e.g. 0x1000 + offset)
    
    struct TrieNode* first_child;
    struct TrieNode* next_sibling;
    
    int* paper_indices;
    int paper_count;
    int paper_capacity;
} TrieNode;

// Statistics for visualizer
extern int total_nodes_allocated;
extern uint32_t current_heap_address;
extern int search_chars_traversed;
extern int search_nodes_visited;
extern int search_comparisons;

TrieNode* create_trie_node(char ch);
void free_trie(TrieNode* root);

// Inserts a word with the associated paper index into the Trie
void trie_insert(TrieNode* root, const char* word, int paper_idx);

// Searches a word in the Trie and returns the node if found (NULL otherwise)
TrieNode* trie_search(TrieNode* root, const char* word);

// Collects autocomplete suggestions for a given prefix (returns array of matching node pointers)
int trie_collect_suggestions(TrieNode* root, const char* prefix, TrieNode** results, int max_results);

// Reconstructs the word string from a node by traversing upwards or using stack
void get_node_word(TrieNode* root, TrieNode* node, char* buffer, int max_len);

// Checks if a word is a stop word
bool is_stop_word(const char* word);

#endif // TRIE_H
