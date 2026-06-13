#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "trie.h"

int total_nodes_allocated = 0;
uint32_t current_heap_address = 0x1000;

int search_chars_traversed = 0;
int search_nodes_visited = 0;
int search_comparisons = 0;

static const char* STOP_WORDS[] = {
    "the", "of", "and", "a", "to", "in", "is", "for", "on", "that",
    "by", "this", "with", "an", "as", "from", "at", "are", "be", "it",
    "we", "or", "which", "was", "but", "not", "have", "has", "can"
};
#define NUM_STOP_WORDS (sizeof(STOP_WORDS) / sizeof(STOP_WORDS[0]))

bool is_stop_word(const char* word) {
    if (strlen(word) <= 1) return true;
    for (size_t i = 0; i < NUM_STOP_WORDS; i++) {
        if (strcmp(word, STOP_WORDS[i]) == 0) return true;
    }
    return false;
}

TrieNode* create_trie_node(char ch) {
    TrieNode* node = (TrieNode*)malloc(sizeof(TrieNode));
    if (!node) return NULL;
    node->ch = ch;
    node->is_word = false;
    node->address = current_heap_address;
    current_heap_address += 32; // Simulating 32-byte alignment / size
    total_nodes_allocated++;
    
    node->first_child = NULL;
    node->next_sibling = NULL;
    node->paper_indices = NULL;
    node->paper_count = 0;
    node->paper_capacity = 0;
    return node;
}

void free_trie(TrieNode* root) {
    if (!root) return;
    free_trie(root->first_child);
    free_trie(root->next_sibling);
    if (root->paper_indices) {
        free(root->paper_indices);
    }
    free(root);
}

void trie_insert(TrieNode* root, const char* word, int paper_idx) {
    if (!word || *word == '\0') return;
    
    TrieNode* curr = root;
    const char* p = word;
    
    while (*p) {
        char ch = tolower((unsigned char)*p);
        
        // Find child
        TrieNode* child = NULL;
        TrieNode* prev = NULL;
        TrieNode* sibling = curr->first_child;
        while (sibling) {
            if (sibling->ch == ch) {
                child = sibling;
                break;
            }
            prev = sibling;
            sibling = sibling->next_sibling;
        }
        
        if (!child) {
            child = create_trie_node(ch);
            if (!child) return;
            if (prev) {
                prev->next_sibling = child;
            } else {
                curr->first_child = child;
            }
        }
        
        curr = child;
        p++;
    }
    
    curr->is_word = true;
    
    // De-duplication check: Since we insert papers sequentially,
    // the paper_idx will always be increasing. We only need to check the last element.
    if (curr->paper_count > 0 && curr->paper_indices[curr->paper_count - 1] == paper_idx) {
        return;
    }
    
    if (curr->paper_count >= curr->paper_capacity) {
        curr->paper_capacity = curr->paper_capacity == 0 ? 4 : curr->paper_capacity * 2;
        int* new_indices = (int*)realloc(curr->paper_indices, curr->paper_capacity * sizeof(int));
        if (new_indices) {
            curr->paper_indices = new_indices;
        }
    }
    
    if (curr->paper_indices && curr->paper_count < curr->paper_capacity) {
        curr->paper_indices[curr->paper_count++] = paper_idx;
    }
}

TrieNode* trie_search(TrieNode* root, const char* word) {
    if (!word) return NULL;
    
    TrieNode* curr = root;
    const char* p = word;
    
    while (*p) {
        char ch = tolower((unsigned char)*p);
        search_chars_traversed++;
        
        TrieNode* child = NULL;
        TrieNode* sibling = curr->first_child;
        while (sibling) {
            search_nodes_visited++;
            search_comparisons++;
            if (sibling->ch == ch) {
                child = sibling;
                break;
            }
            sibling = sibling->next_sibling;
        }
        
        if (!child) {
            return NULL; // Prefix not found
        }
        
        curr = child;
        p++;
    }
    
    return curr;
}

// Helper for collecting suggestions recursively
static void collect_suggestions_recursive(TrieNode* node, char* prefix_buffer, int depth, TrieNode** results, int* count, int max_results) {
    if (!node || *count >= max_results) return;
    
    prefix_buffer[depth] = node->ch;
    prefix_buffer[depth + 1] = '\0';
    
    if (node->is_word) {
        results[*count] = node;
        (*count)++;
    }
    
    // Traverse children
    TrieNode* child = node->first_child;
    while (child && *count < max_results) {
        collect_suggestions_recursive(child, prefix_buffer, depth + 1, results, count, max_results);
        child = child->next_sibling;
    }
}

int trie_collect_suggestions(TrieNode* root, const char* prefix, TrieNode** results, int max_results) {
    TrieNode* start_node = trie_search(root, prefix);
    if (!start_node) return 0;
    
    int count = 0;
    char buffer[256];
    strncpy(buffer, prefix, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';
    
    if (start_node->is_word) {
        results[count++] = start_node;
    }
    
    TrieNode* child = start_node->first_child;
    while (child && count < max_results) {
        collect_suggestions_recursive(child, buffer, strlen(prefix), results, &count, max_results);
        child = child->next_sibling;
    }
    
    return count;
}

// Walks down or traces recursively from root to find node's string word representation
static bool find_node_path(TrieNode* curr, TrieNode* target, char* buffer, int depth) {
    if (!curr) return false;
    if (curr == target) {
        buffer[depth] = '\0';
        return true;
    }
    
    // Try children
    TrieNode* child = curr->first_child;
    while (child) {
        buffer[depth] = child->ch;
        if (find_node_path(child, target, buffer, depth + 1)) {
            return true;
        }
        child = child->next_sibling;
    }
    return false;
}

void get_node_word(TrieNode* root, TrieNode* node, char* buffer, int max_len) {
    (void)max_len;
    buffer[0] = '\0';
    if (node == root) return;
    find_node_path(root, node, buffer, 0);
}
