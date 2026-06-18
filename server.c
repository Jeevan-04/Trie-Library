#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "server.h"
#include "parser.h"
#include "trie.h"

char* g_file_data = NULL;
size_t g_file_size = 0;
size_t* g_line_offsets = NULL;
size_t g_line_count = 0;
TrieNode* g_trie_root = NULL;

// Analytics variables (populated in main.c)
#define MAX_ANALYTICS_CATEGORIES 50
#define MAX_ANALYTICS_YEARS 50
#define MAX_ANALYTICS_AUTHORS 50

extern CategoryCount g_top_categories[MAX_ANALYTICS_CATEGORIES];
extern int g_top_categories_count;
extern YearCount g_yearly_trends[MAX_ANALYTICS_YEARS];
extern int g_yearly_trends_count;
extern AuthorCount g_top_authors[MAX_ANALYTICS_AUTHORS];
extern int g_top_authors_count;

// Stack simulation structure
#define MAX_STACK_DEPTH 16
typedef struct {
    char function_name[64];
    char parameters[128];
    char local_variables[128];
} StackFrame;

StackFrame g_stack[MAX_STACK_DEPTH];
int g_stack_depth = 0;

StackFrame g_peak_stack[MAX_STACK_DEPTH];
int g_peak_stack_depth = 0;

void push_stack_frame(const char* func, const char* params, const char* locals) {
    if (g_stack_depth < MAX_STACK_DEPTH) {
        strncpy(g_stack[g_stack_depth].function_name, func, 63);
        strncpy(g_stack[g_stack_depth].parameters, params, 127);
        strncpy(g_stack[g_stack_depth].local_variables, locals, 127);
        g_stack_depth++;
        
        if (g_stack_depth > g_peak_stack_depth) {
            g_peak_stack_depth = g_stack_depth;
            memcpy(g_peak_stack, g_stack, g_stack_depth * sizeof(StackFrame));
        }
    }
}

void pop_stack_frame() {
    if (g_stack_depth > 0) {
        g_stack_depth--;
    }
}

// Memory pool visualization support
typedef struct {
    uint32_t address;
    char ch;
    bool is_word;
    int children_count;
    int postings_count;
} VisualHeapNode;

static VisualHeapNode g_visited_nodes[128];
static int g_visited_nodes_count = 0;

void add_visited_node_to_heap_log(TrieNode* node) {
    if (!node || g_visited_nodes_count >= 128) return;
    // Check if already logged
    for (int i = 0; i < g_visited_nodes_count; i++) {
        if (g_visited_nodes[i].address == node->address) return;
    }
    
    int children_count = 0;
    TrieNode* child = node->first_child;
    while (child) {
        children_count++;
        child = child->next_sibling;
    }
    
    g_visited_nodes[g_visited_nodes_count].address = node->address;
    g_visited_nodes[g_visited_nodes_count].ch = node->ch;
    g_visited_nodes[g_visited_nodes_count].is_word = node->is_word;
    g_visited_nodes[g_visited_nodes_count].children_count = children_count;
    g_visited_nodes[g_visited_nodes_count].postings_count = node->paper_count;
    g_visited_nodes_count++;
}

static void url_decode(const char* src, char* dst, size_t dst_len) {
    size_t i = 0;
    while (*src && i < dst_len - 1) {
        if (*src == '%' && src[1] && src[2]) {
            char hex[3] = { src[1], src[2], '\0' };
            dst[i++] = (char)strtol(hex, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            dst[i++] = ' ';
            src++;
        } else {
            dst[i++] = *src++;
        }
    }
    dst[i] = '\0';
}

static const char* get_query_param(const char* query, const char* key, char* val_buf, size_t buf_len) {
    char key_pattern[128];
    snprintf(key_pattern, sizeof(key_pattern), "%s=", key);
    const char* start = strstr(query, key_pattern);
    if (!start) return NULL;
    start += strlen(key_pattern);
    const char* end = strchr(start, '&');
    size_t len = end ? (size_t)(end - start) : strlen(start);
    if (len >= buf_len) len = buf_len - 1;
    char temp[1024];
    strncpy(temp, start, len);
    temp[len] = '\0';
    url_decode(temp, val_buf, buf_len);
    return val_buf;
}

// In-place line extraction
static char* get_line_ptr(size_t line_idx, size_t* out_len) {
    if (line_idx >= g_line_count) return NULL;
    size_t start = g_line_offsets[line_idx];
    size_t end = (line_idx + 1 < g_line_count) ? g_line_offsets[line_idx + 1] - 1 : g_file_size;
    *out_len = end - start;
    return g_file_data + start;
}

// Postings list intersection (AND operation)
static int* intersect_postings(int* list1, int len1, int* list2, int len2, int* out_len) {
    push_stack_frame("intersect_postings", "list1, list2", "i=0, j=0, k=0");
    int* out = (int*)malloc(sizeof(int) * (len1 < len2 ? len1 : len2));
    int i = 0, j = 0, k = 0;
    while (i < len1 && j < len2) {
        if (list1[i] == list2[j]) {
            out[k++] = list1[i];
            i++;
            j++;
        } else if (list1[i] < list2[j]) {
            i++;
        } else {
            j++;
        }
    }
    *out_len = k;
    pop_stack_frame();
    return out;
}

// Helper sorting structures
typedef struct {
    int paper_idx;
    char date[16];
    char title[128];
} SortItem;

static int compare_sort_items_date(const void* a, const void* b) {
    return strcmp(((SortItem*)b)->date, ((SortItem*)a)->date); // Descending (newer first)
}

static int compare_sort_items_title(const void* a, const void* b) {
    return strcmp(((SortItem*)a)->title, ((SortItem*)b)->title); // Ascending
}

static void send_http_error(int client_fd, int code, const char* status, const char* msg) {
    char buf[512];
    snprintf(buf, sizeof(buf),
             "HTTP/1.1 %d %s\r\n"
             "Content-Type: text/plain\r\n"
             "Content-Length: %zu\r\n"
             "Access-Control-Allow-Origin: *\r\n"
             "\r\n"
             "%s",
             code, status, strlen(msg), msg);
    send(client_fd, buf, strlen(buf), 0);
}

static void serve_static_file(int client_fd, const char* filepath, const char* content_type) {
    int file_fd = open(filepath, O_RDONLY);
    if (file_fd < 0) {
        send_http_error(client_fd, 404, "Not Found", "File not found");
        return;
    }
    
    struct stat sb;
    fstat(file_fd, &sb);
    
    char header[512];
    snprintf(header, sizeof(header),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %lld\r\n"
             "Access-Control-Allow-Origin: *\r\n"
             "\r\n",
             content_type, sb.st_size);
    send(client_fd, header, strlen(header), 0);
    
    char buffer[8192];
    ssize_t bytes_read;
    while ((bytes_read = read(file_fd, buffer, sizeof(buffer))) > 0) {
        send(client_fd, buffer, bytes_read, 0);
    }
    close(file_fd);
}

static void handle_dataset_endpoint(int client_fd) {
    char json[256];
    snprintf(json, sizeof(json),
             "{\"total_papers\": %zu, \"trie_nodes\": %d, \"heap_used_bytes\": %u}",
             g_line_count, total_nodes_allocated, current_heap_address - 0x1000);
             
    char header[512];
    snprintf(header, sizeof(header),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "Access-Control-Allow-Origin: *\r\n"
             "\r\n",
             strlen(json));
    send(client_fd, header, strlen(header), 0);
    send(client_fd, json, strlen(json), 0);
}

static void handle_search_endpoint(int client_fd, const char* query_str) {
    struct timeval total_start, total_end;
    gettimeofday(&total_start, NULL);
    
    push_stack_frame("handle_search_endpoint", "query_str", "page=1, field='all'");
    g_visited_nodes_count = 0;
    g_peak_stack_depth = 0;
    
    char q[512] = {0};
    char field[64] = "all";
    char sort_by[64] = "default";
    char page_str[32] = "1";
    
    get_query_param(query_str, "q", q, sizeof(q));
    get_query_param(query_str, "field", field, sizeof(field));
    get_query_param(query_str, "sort", sort_by, sizeof(sort_by));
    get_query_param(query_str, "page", page_str, sizeof(page_str));
    
    int page = atoi(page_str);
    if (page < 1) page = 1;
    
    search_chars_traversed = 0;
    search_nodes_visited = 0;
    search_comparisons = 0;
    
    struct timeval t0, t1;
    
    // Phase 1: Input Parsing & Normalization
    gettimeofday(&t0, NULL);
    char temp_q[512];
    strncpy(temp_q, q, sizeof(temp_q) - 1);
    temp_q[sizeof(temp_q) - 1] = '\0';
    gettimeofday(&t1, NULL);
    double parse_time_us = (t1.tv_sec - t0.tv_sec) * 1000000.0 + (t1.tv_usec - t0.tv_usec);
    if (parse_time_us < 1) parse_time_us = 1.0;
    
    // Phase 2: Trie Traversal
    gettimeofday(&t0, NULL);
    int* final_postings = NULL;
    int final_count = 0;
    char* token = strtok(temp_q, " ");
    bool first_term = true;
    
    while (token) {
        char word[128] = {0};
        int w_idx = 0;
        for (int i = 0; token[i]; i++) {
            if (isalnum((unsigned char)token[i]) || token[i] == '-' || token[i] == '.') {
                word[w_idx++] = tolower((unsigned char)token[i]);
            }
        }
        word[w_idx] = '\0';
        
        if (strlen(word) > 0) {
            push_stack_frame("trie_search", "word", "curr=root");
            TrieNode* node = trie_search(g_trie_root, word);
            pop_stack_frame();
            
            if (node) {
                TrieNode* temp_node = g_trie_root;
                for (int i = 0; word[i]; i++) {
                    TrieNode* child = temp_node->first_child;
                    while (child) {
                        if (child->ch == tolower((unsigned char)word[i])) {
                            add_visited_node_to_heap_log(child);
                            temp_node = child;
                            break;
                        }
                        child = child->next_sibling;
                    }
                }
            }
            
            int term_count = node ? node->paper_count : 0;
            int* term_postings = node ? node->paper_indices : NULL;
            
            if (first_term) {
                if (term_count > 0) {
                    final_count = term_count;
                    final_postings = (int*)malloc(final_count * sizeof(int));
                    memcpy(final_postings, term_postings, final_count * sizeof(int));
                } else {
                    final_count = 0;
                    final_postings = NULL;
                }
                first_term = false;
            } else {
                if (final_count > 0 && term_count > 0) {
                    int intersect_count = 0;
                    int* intersected = intersect_postings(final_postings, final_count, term_postings, term_count, &intersect_count);
                    free(final_postings);
                    final_postings = intersected;
                    final_count = intersect_count;
                } else {
                    if (final_postings) free(final_postings);
                    final_postings = NULL;
                    final_count = 0;
                }
            }
        }
        token = strtok(NULL, " ");
    }
    gettimeofday(&t1, NULL);
    double trie_walk_time_us = (t1.tv_sec - t0.tv_sec) * 1000000.0 + (t1.tv_usec - t0.tv_usec);
    if (trie_walk_time_us < 2) trie_walk_time_us = 2.0;
    
    // Phase 3: Postings Intersection & Metadata fetch
    gettimeofday(&t0, NULL);
    int* filtered_postings = (int*)malloc(sizeof(int) * (final_count > 0 ? final_count : 1));
    int filtered_count = 0;
    
    for (int i = 0; i < final_count; i++) {
        size_t len;
        char* line_ptr = get_line_ptr(final_postings[i], &len);
        if (!line_ptr) continue;
        
        char* temp_line = (char*)malloc(len + 1);
        memcpy(temp_line, line_ptr, len);
        temp_line[len] = '\0';
        
        PaperMetadata* paper = parse_json_line(temp_line);
        free(temp_line);
        if (!paper) continue;
        
        bool match = false;
        if (strcmp(field, "title") == 0) {
            match = (strcasestr(paper->title, q) != NULL);
        } else if (strcmp(field, "authors") == 0) {
            match = (strcasestr(paper->authors, q) != NULL);
        } else if (strcmp(field, "categories") == 0) {
            match = (strcasestr(paper->categories, q) != NULL);
        } else {
            match = true;
        }
        
        if (match) {
            filtered_postings[filtered_count++] = final_postings[i];
        }
        free_paper_metadata(paper);
    }
    if (final_postings) free(final_postings);
    gettimeofday(&t1, NULL);
    double metadata_fetch_time_us = (t1.tv_sec - t0.tv_sec) * 1000000.0 + (t1.tv_usec - t0.tv_usec);
    if (metadata_fetch_time_us < 4) metadata_fetch_time_us = 4.0;
    
    // Phase 4: Sorting
    gettimeofday(&t0, NULL);
    if (strcmp(sort_by, "date") == 0 || strcmp(sort_by, "title") == 0) {
        SortItem* sort_items = (SortItem*)malloc(filtered_count * sizeof(SortItem));
        for (int i = 0; i < filtered_count; i++) {
            size_t len;
            char* line_ptr = get_line_ptr(filtered_postings[i], &len);
            char* temp_line = (char*)malloc(len + 1);
            memcpy(temp_line, line_ptr, len);
            temp_line[len] = '\0';
            PaperMetadata* paper = parse_json_line(temp_line);
            free(temp_line);
            
            sort_items[i].paper_idx = filtered_postings[i];
            if (paper) {
                strncpy(sort_items[i].date, paper->update_date, 15);
                strncpy(sort_items[i].title, paper->title, 127);
                free_paper_metadata(paper);
            } else {
                strcpy(sort_items[i].date, "");
                strcpy(sort_items[i].title, "");
            }
        }
        
        if (strcmp(sort_by, "date") == 0) {
            qsort(sort_items, filtered_count, sizeof(SortItem), compare_sort_items_date);
        } else {
            qsort(sort_items, filtered_count, sizeof(SortItem), compare_sort_items_title);
        }
        
        for (int i = 0; i < filtered_count; i++) {
            filtered_postings[i] = sort_items[i].paper_idx;
        }
        free(sort_items);
    }
    gettimeofday(&t1, NULL);
    double sorting_time_us = (t1.tv_sec - t0.tv_sec) * 1000000.0 + (t1.tv_usec - t0.tv_usec);
    if (sorting_time_us < 1) sorting_time_us = 1.0;
    
    // Paginate: 10 per page
    int limit = 10;
    int offset = (page - 1) * limit;
    int page_results_count = 0;
    if (offset < filtered_count) {
        page_results_count = filtered_count - offset;
        if (page_results_count > limit) page_results_count = limit;
    }
    
    // Start JSON generation
    char* json = (char*)malloc(1024 * 1024); // 1MB buffer
    strcpy(json, "{\n  \"total_matches\": ");
    char temp[4096];
    snprintf(temp, sizeof(temp), "%d,\n  \"page\": %d,\n  \"total_pages\": %d,\n  \"results\": [\n",
             filtered_count, page, (filtered_count + limit - 1) / limit);
    strcat(json, temp);
    
    for (int i = 0; i < page_results_count; i++) {
        int idx = filtered_postings[offset + i];
        size_t len;
        char* line_ptr = get_line_ptr(idx, &len);
        char* temp_line = (char*)malloc(len + 1);
        memcpy(temp_line, line_ptr, len);
        temp_line[len] = '\0';
        
        strcat(json, "    ");
        strcat(json, temp_line);
        if (i < page_results_count - 1) {
            strcat(json, ",\n");
        } else {
            strcat(json, "\n");
        }
        free(temp_line);
    }
    
    // Build postings preview JSON array (up to 30 elements)
    char postings_preview_buf[1024] = "[";
    int preview_limit = filtered_count < 30 ? filtered_count : 30;
    for (int i = 0; i < preview_limit; i++) {
        char id_buf[32];
        snprintf(id_buf, sizeof(id_buf), "%d", filtered_postings[i]);
        strcat(postings_preview_buf, id_buf);
        if (i < preview_limit - 1) {
            strcat(postings_preview_buf, ", ");
        }
    }
    strcat(postings_preview_buf, "]");
    
    gettimeofday(&total_end, NULL);
    double total_time_us = (total_end.tv_sec - total_start.tv_sec) * 1000000.0 + (total_end.tv_usec - total_start.tv_usec);
    
    snprintf(temp, sizeof(temp),
             "  ],\n  \"postings_preview\": %s,\n  \"stats\": {\n    \"chars_traversed\": %d,\n    \"nodes_visited\": %d,\n    \"comparisons\": %d,\n"
             "    \"timings\": {\n      \"parse_us\": %.0f,\n      \"trie_walk_us\": %.0f,\n      \"fetch_us\": %.0f,\n      \"sort_us\": %.0f,\n      \"total_us\": %.0f\n    }\n  }\n}",
             postings_preview_buf,
             search_chars_traversed, search_nodes_visited, search_comparisons,
             parse_time_us, trie_walk_time_us, metadata_fetch_time_us, sorting_time_us, total_time_us);
    strcat(json, temp);
    
    char header[512];
    snprintf(header, sizeof(header),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "Access-Control-Allow-Origin: *\r\n"
             "\r\n",
             strlen(json));
    send(client_fd, header, strlen(header), 0);
    send(client_fd, json, strlen(json), 0);
    
    free(json);
    free(filtered_postings);
    pop_stack_frame();
}

static void handle_suggestions_endpoint(int client_fd, const char* query_str) {
    char q[256] = {0};
    get_query_param(query_str, "q", q, sizeof(q));
    
    TrieNode* suggestions[15];
    int count = trie_collect_suggestions(g_trie_root, q, suggestions, 15);
    
    char json[4096] = "[\n";
    for (int i = 0; i < count; i++) {
        char word[256];
        get_node_word(g_trie_root, suggestions[i], word, sizeof(word));
        char line_buf[512];
        snprintf(line_buf, sizeof(line_buf), "  {\"word\": \"%s\", \"count\": %d}", word, suggestions[i]->paper_count);
        strcat(json, line_buf);
        if (i < count - 1) strcat(json, ",\n");
    }
    strcat(json, "\n]");
    
    char header[512];
    snprintf(header, sizeof(header),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "Access-Control-Allow-Origin: *\r\n"
             "\r\n",
             strlen(json));
    send(client_fd, header, strlen(header), 0);
    send(client_fd, json, strlen(json), 0);
}

static void handle_trie_endpoint(int client_fd, const char* query_str) {
    char q[256] = {0};
    get_query_param(query_str, "q", q, sizeof(q));
    
    // Allocate 32KB buffer for building subtree nodes list
    char* json = (char*)malloc(32768);
    strcpy(json, "{\n  \"query\": \"");
    strcat(json, q);
    strcat(json, "\",\n  \"nodes\": [\n");
    
    // 1. Root node
    strcat(json, "    {\"address\": \"0x1000\", \"char\": \"ROOT\", \"parent\": \"0x0\", \"highlighted\": true, \"is_word\": false}");
    
    TrieNode* curr = g_trie_root;
    
    // Split search query by space or treat as space-separated tokens
    // We walk character by character. Sibling nodes at each level represent branches!
    for (int i = 0; q[i]; i++) {
        char ch = tolower((unsigned char)q[i]);
        TrieNode* next_node = NULL;
        
        TrieNode* child = curr->first_child;
        while (child) {
            if (child->ch == ch) {
                next_node = child;
                break;
            }
            child = child->next_sibling;
        }
        
        // Output sibling branches (always prioritizing the matching highlighted node)
        int output_count = 0;
        if (next_node) {
            strcat(json, ",\n");
            char node_json[512];
            snprintf(node_json, sizeof(node_json),
                     "    {\"address\": \"0x%X\", \"char\": \"%c\", \"parent\": \"0x%X\", \"highlighted\": true, \"is_word\": %s}",
                     next_node->address, next_node->ch == ' ' ? '_' : next_node->ch, curr->address,
                     next_node->is_word ? "true" : "false");
            strcat(json, node_json);
            output_count++;
        }
        
        child = curr->first_child;
        while (child && output_count < 6) {
            if (child != next_node) {
                strcat(json, ",\n");
                char node_json[512];
                snprintf(node_json, sizeof(node_json),
                         "    {\"address\": \"0x%X\", \"char\": \"%c\", \"parent\": \"0x%X\", \"highlighted\": false, \"is_word\": %s}",
                         child->address, child->ch == ' ' ? '_' : child->ch, curr->address,
                         child->is_word ? "true" : "false");
                strcat(json, node_json);
                output_count++;
            }
            child = child->next_sibling;
        }
        
        if (!next_node) break;
        curr = next_node;
    }
    
    strcat(json, "\n  ]\n}");
    
    char header[512];
    snprintf(header, sizeof(header),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "Access-Control-Allow-Origin: *\r\n"
             "\r\n",
             strlen(json));
    send(client_fd, header, strlen(header), 0);
    send(client_fd, json, strlen(json), 0);
    free(json);
}

static void handle_memory_endpoint(int client_fd) {
    char json[16384] = "{\n  \"stack\": [\n";
    for (int i = 0; i < g_peak_stack_depth; i++) {
        char frame[512];
        snprintf(frame, sizeof(frame),
                 "    {\"function\": \"%s\", \"params\": \"%s\", \"locals\": \"%s\"}",
                 g_peak_stack[i].function_name, g_peak_stack[i].parameters, g_peak_stack[i].local_variables);
        strcat(json, frame);
        if (i < g_peak_stack_depth - 1) strcat(json, ",\n");
    }
    strcat(json, "\n  ],\n  \"heap\": [\n");
    
    // Output logged visited heap nodes
    for (int i = 0; i < g_visited_nodes_count; i++) {
        char hn[256];
        snprintf(hn, sizeof(hn),
                 "    {\"address\": \"0x%X\", \"char\": \"%c\", \"is_terminal\": %s, \"children\": %d, \"postings\": %d}",
                 g_visited_nodes[i].address, g_visited_nodes[i].ch,
                 g_visited_nodes[i].is_word ? "true" : "false",
                 g_visited_nodes[i].children_count, g_visited_nodes[i].postings_count);
        strcat(json, hn);
        if (i < g_visited_nodes_count - 1) strcat(json, ",\n");
    }
    strcat(json, "\n  ]\n}");
    
    char header[512];
    snprintf(header, sizeof(header),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "Access-Control-Allow-Origin: *\r\n"
             "\r\n",
             strlen(json));
    send(client_fd, header, strlen(header), 0);
    send(client_fd, json, strlen(json), 0);
}

static void handle_analytics_endpoint(int client_fd) {
    char* json = (char*)malloc(16384);
    strcpy(json, "{\n  \"top_categories\": [\n");
    for (int i = 0; i < g_top_categories_count; i++) {
        char line[256];
        snprintf(line, sizeof(line), "    {\"category\": \"%s\", \"count\": %d}",
                 g_top_categories[i].category, g_top_categories[i].count);
        strcat(json, line);
        if (i < g_top_categories_count - 1) strcat(json, ",\n");
    }
    strcat(json, "\n  ],\n  \"yearly_trends\": [\n");
    for (int i = 0; i < g_yearly_trends_count; i++) {
        char line[256];
        snprintf(line, sizeof(line), "    {\"year\": %d, \"count\": %d}",
                 g_yearly_trends[i].year, g_yearly_trends[i].count);
        strcat(json, line);
        if (i < g_yearly_trends_count - 1) strcat(json, ",\n");
    }
    strcat(json, "\n  ],\n  \"top_authors\": [\n");
    for (int i = 0; i < g_top_authors_count; i++) {
        char line[256];
        snprintf(line, sizeof(line), "    {\"author\": \"%s\", \"count\": %d}",
                 g_top_authors[i].author, g_top_authors[i].count);
        strcat(json, line);
        if (i < g_top_authors_count - 1) strcat(json, ",\n");
    }
    strcat(json, "\n  ]\n}");
    
    char header[512];
    snprintf(header, sizeof(header),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "Access-Control-Allow-Origin: *\r\n"
             "\r\n",
             strlen(json));
    send(client_fd, header, strlen(header), 0);
    send(client_fd, json, strlen(json), 0);
    free(json);
}

void start_server(int port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }
    
    if (listen(server_fd, 10) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }
    
    printf("Server listening on port %d...\n", port);
    
    while (1) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) continue;
        
        char buffer[2048] = {0};
        read(client_fd, buffer, sizeof(buffer) - 1);
        
        char method[16] = {0};
        char url[1024] = {0};
        sscanf(buffer, "%s %s", method, url);
        
        if (strcmp(method, "OPTIONS") == 0) {
            char options_response[] = 
                "HTTP/1.1 204 No Content\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                "Access-Control-Allow-Headers: *\r\n"
                "Access-Control-Max-Age: 86400\r\n"
                "Content-Length: 0\r\n"
                "\r\n";
            send(client_fd, options_response, strlen(options_response), 0);
            close(client_fd);
            continue;
        }
        
        char* query_str = strchr(url, '?');
        if (query_str) {
            *query_str = '\0';
            query_str++; // point past '?'
        }
        
        if (strcmp(url, "/") == 0 || strcmp(url, "/index.html") == 0) {
            serve_static_file(client_fd, "index.html", "text/html");
        } else if (strcmp(url, "/index.css") == 0) {
            serve_static_file(client_fd, "index.css", "text/css");
        } else if (strcmp(url, "/index.js") == 0) {
            serve_static_file(client_fd, "index.js", "application/javascript");
        } else if (strcmp(url, "/dataset") == 0) {
            handle_dataset_endpoint(client_fd);
        } else if (strcmp(url, "/search") == 0) {
            handle_search_endpoint(client_fd, query_str ? query_str : "");
        } else if (strcmp(url, "/suggestions") == 0) {
            handle_suggestions_endpoint(client_fd, query_str ? query_str : "");
        } else if (strcmp(url, "/trie") == 0) {
            handle_trie_endpoint(client_fd, query_str ? query_str : "");
        } else if (strcmp(url, "/memory") == 0) {
            handle_memory_endpoint(client_fd);
        } else if (strcmp(url, "/analytics") == 0) {
            handle_analytics_endpoint(client_fd);
        } else {
            send_http_error(client_fd, 404, "Not Found", "Not Found");
        }
        
        close(client_fd);
    }
}
