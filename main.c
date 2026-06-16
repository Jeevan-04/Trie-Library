#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "trie.h"
#include "parser.h"
#include "server.h"

// Hash table size and structures for on-the-fly analytics
#define MAX_UNIQUE_CATEGORIES 1000
#define MAX_UNIQUE_YEARS 100
#define MAX_UNIQUE_AUTHORS 50000

CategoryCount g_top_categories[50];
int g_top_categories_count = 0;
static CategoryCount s_categories_hash[MAX_UNIQUE_CATEGORIES];

YearCount g_yearly_trends[50];
int g_yearly_trends_count = 0;
static YearCount s_years_hash[MAX_UNIQUE_YEARS];

AuthorCount g_top_authors[50];
int g_top_authors_count = 0;
static AuthorCount s_authors_hash[MAX_UNIQUE_AUTHORS];

static unsigned int hash_string(const char* str, int table_size) {
    unsigned int hash = 5381;
    int c;
    while ((c = (unsigned char)*str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash % table_size;
}

static void record_category(const char* cat) {
    if (!cat || cat[0] == '\0') return;
    unsigned int slot = hash_string(cat, MAX_UNIQUE_CATEGORIES);
    for (int i = 0; i < MAX_UNIQUE_CATEGORIES; i++) {
        int idx = (slot + i) % MAX_UNIQUE_CATEGORIES;
        if (s_categories_hash[idx].category[0] == '\0') {
            strncpy(s_categories_hash[idx].category, cat, sizeof(s_categories_hash[idx].category) - 1);
            s_categories_hash[idx].count = 1;
            break;
        }
        if (strcmp(s_categories_hash[idx].category, cat) == 0) {
            s_categories_hash[idx].count++;
            break;
        }
    }
}

static void record_year(int year) {
    if (year < 1900 || year > 2100) return;
    unsigned int slot = year % MAX_UNIQUE_YEARS;
    for (int i = 0; i < MAX_UNIQUE_YEARS; i++) {
        int idx = (slot + i) % MAX_UNIQUE_YEARS;
        if (s_years_hash[idx].year == 0) {
            s_years_hash[idx].year = year;
            s_years_hash[idx].count = 1;
            break;
        }
        if (s_years_hash[idx].year == year) {
            s_years_hash[idx].count++;
            break;
        }
    }
}

static int s_unique_authors_count = 0;
static void record_author(const char* author) {
    if (!author || author[0] == '\0') return;
    unsigned int slot = hash_string(author, MAX_UNIQUE_AUTHORS);
    for (int i = 0; i < MAX_UNIQUE_AUTHORS; i++) {
        int idx = (slot + i) % MAX_UNIQUE_AUTHORS;
        if (s_authors_hash[idx].author[0] == '\0') {
            if (s_unique_authors_count < MAX_UNIQUE_AUTHORS - 1000) {
                strncpy(s_authors_hash[idx].author, author, sizeof(s_authors_hash[idx].author) - 1);
                s_authors_hash[idx].count = 1;
                s_unique_authors_count++;
            }
            break;
        }
        if (strcmp(s_authors_hash[idx].author, author) == 0) {
            s_authors_hash[idx].count++;
            break;
        }
    }
}

// Comparison functions for sorting analytics
static int compare_categories(const void* a, const void* b) {
    return ((CategoryCount*)b)->count - ((CategoryCount*)a)->count;
}
static int compare_years(const void* a, const void* b) {
    return ((YearCount*)a)->year - ((YearCount*)b)->year; // Ascending years
}
static int compare_authors(const void* a, const void* b) {
    return ((AuthorCount*)b)->count - ((AuthorCount*)a)->count;
}

static void finalize_analytics() {
    // Sort and collect top 50 categories
    CategoryCount* all_cats = (CategoryCount*)malloc(MAX_UNIQUE_CATEGORIES * sizeof(CategoryCount));
    int cat_count = 0;
    for (int i = 0; i < MAX_UNIQUE_CATEGORIES; i++) {
        if (s_categories_hash[i].category[0] != '\0') {
            all_cats[cat_count++] = s_categories_hash[i];
        }
    }
    qsort(all_cats, cat_count, sizeof(CategoryCount), compare_categories);
    g_top_categories_count = cat_count > 50 ? 50 : cat_count;
    memcpy(g_top_categories, all_cats, g_top_categories_count * sizeof(CategoryCount));
    free(all_cats);
    
    // Sort and collect top 50 years
    YearCount* all_years = (YearCount*)malloc(MAX_UNIQUE_YEARS * sizeof(YearCount));
    int year_count = 0;
    for (int i = 0; i < MAX_UNIQUE_YEARS; i++) {
        if (s_years_hash[i].year != 0) {
            all_years[year_count++] = s_years_hash[i];
        }
    }
    qsort(all_years, year_count, sizeof(YearCount), compare_years);
    g_yearly_trends_count = year_count > 50 ? 50 : year_count;
    memcpy(g_yearly_trends, all_years, g_yearly_trends_count * sizeof(YearCount));
    free(all_years);
    
    // Sort and collect top 50 authors
    AuthorCount* all_authors = (AuthorCount*)malloc(MAX_UNIQUE_AUTHORS * sizeof(AuthorCount));
    int auth_count = 0;
    for (int i = 0; i < MAX_UNIQUE_AUTHORS; i++) {
        if (s_authors_hash[i].author[0] != '\0') {
            all_authors[auth_count++] = s_authors_hash[i];
        }
    }
    qsort(all_authors, auth_count, sizeof(AuthorCount), compare_authors);
    g_top_authors_count = auth_count > 50 ? 50 : auth_count;
    memcpy(g_top_authors, all_authors, g_top_authors_count * sizeof(AuthorCount));
    free(all_authors);
}

// Direct scanning helper from raw JSON line (to avoid allocating objects during indexing)
static void extract_and_index_fields(const char* line, size_t line_len, int paper_idx, TrieNode* root) {
    static char buf[262144]; // 256KB static buffer to eliminate dynamic allocation overhead
    if (line_len >= sizeof(buf)) {
        line_len = sizeof(buf) - 1;
    }
    memcpy(buf, line, line_len);
    buf[line_len] = '\0';
    
    // 1. Title
    char* title_pos = strstr(buf, "\"title\"");
    if (title_pos) {
        title_pos += 7;
        while (*title_pos == ' ' || *title_pos == '\t' || *title_pos == ':') title_pos++;
        if (*title_pos == '"') {
            title_pos++;
            char* end = title_pos;
            while (*end && (*end != '"' || *(end - 1) == '\\')) end++;
            char saved = *end;
            *end = '\0';
            
            // Unescape in-place and tokenize
            unescape_string(title_pos);
            
            // Tokenize and insert
            char word[128];
            int w_idx = 0;
            const char* p = title_pos;
            while (*p) {
                char c = *p;
                if (isalnum((unsigned char)c) || c == '-' || c == '.') {
                    if (w_idx < 127) word[w_idx++] = tolower((unsigned char)c);
                } else {
                    if (w_idx > 0) {
                        word[w_idx] = '\0';
                        if (!is_stop_word(word)) {
                            trie_insert(root, word, paper_idx);
                        }
                        w_idx = 0;
                    }
                }
                p++;
            }
            if (w_idx > 0) {
                word[w_idx] = '\0';
                if (!is_stop_word(word)) trie_insert(root, word, paper_idx);
            }
            
            *end = saved;
        }
    }
    
    // 2. Authors (For trie insertion and analytics)
    char* authors_pos = strstr(buf, "\"authors\"");
    if (authors_pos) {
        authors_pos += 9;
        while (*authors_pos == ' ' || *authors_pos == '\t' || *authors_pos == ':') authors_pos++;
        if (*authors_pos == '"') {
            authors_pos++;
            char* end = authors_pos;
            while (*end && (*end != '"' || *(end - 1) == '\\')) end++;
            char saved = *end;
            *end = '\0';
            
            unescape_string(authors_pos);
            
            // Tokenize authors by words to insert into Trie
            char word[128];
            int w_idx = 0;
            const char* p = authors_pos;
            while (*p) {
                char c = *p;
                if (isalnum((unsigned char)c) || c == '-' || c == '.') {
                    if (w_idx < 127) word[w_idx++] = tolower((unsigned char)c);
                } else {
                    if (w_idx > 0) {
                        word[w_idx] = '\0';
                        if (!is_stop_word(word)) {
                            trie_insert(root, word, paper_idx);
                        }
                        w_idx = 0;
                    }
                }
                p++;
            }
            if (w_idx > 0) {
                word[w_idx] = '\0';
                if (!is_stop_word(word)) trie_insert(root, word, paper_idx);
            }
            
            // Analytics: extract individual author names by split (comma)
            // e.g. "Ileana Streinu and Louis Theran" or "C. Balazs, E. L. Berger"
            char* auth_tok = strtok(authors_pos, ",");
            while (auth_tok) {
                // Trim leading/trailing whitespace
                while (*auth_tok == ' ') auth_tok++;
                size_t l = strlen(auth_tok);
                while (l > 0 && auth_tok[l-1] == ' ') {
                    auth_tok[l-1] = '\0';
                    l--;
                }
                if (strlen(auth_tok) > 0) {
                    record_author(auth_tok);
                }
                auth_tok = strtok(NULL, ",");
            }
            *end = saved;
        }
    }
    
    // 3. Categories (For trie insertion and analytics)
    char* cat_pos = strstr(buf, "\"categories\"");
    if (cat_pos) {
        cat_pos += 12;
        while (*cat_pos == ' ' || *cat_pos == '\t' || *cat_pos == ':') cat_pos++;
        if (*cat_pos == '"') {
            cat_pos++;
            char* end = cat_pos;
            while (*end && (*end != '"' || *(end - 1) == '\\')) end++;
            char saved = *end;
            *end = '\0';
            
            unescape_string(cat_pos);
            
            // Split categories by spaces (e.g. "math.CO cs.CG")
            char* cat_tok = strtok(cat_pos, " ");
            while (cat_tok) {
                if (strlen(cat_tok) > 0) {
                    trie_insert(root, cat_tok, paper_idx);
                    record_category(cat_tok);
                }
                cat_tok = strtok(NULL, " ");
            }
            *end = saved;
        }
    }
    
    // 4. Update Date (For year analytics)
    char* date_pos = strstr(buf, "\"update_date\"");
    if (date_pos) {
        date_pos += 13;
        while (*date_pos == ' ' || *date_pos == '\t' || *date_pos == ':') date_pos++;
        if (*date_pos == '"') {
            date_pos++;
            // Year is the first 4 characters
            char year_str[5] = { date_pos[0], date_pos[1], date_pos[2], date_pos[3], '\0' };
            int year = atoi(year_str);
            if (year > 1900 && year < 2100) {
                record_year(year);
            }
        }
    }

}

static void create_mock_dataset(const char* filename) {
    printf("Dataset file '%s' not found. Creating a minimal mock dataset for Demo Mode...\n", filename);
    FILE* f = fopen(filename, "w");
    if (!f) {
        perror("Failed to create mock dataset");
        return;
    }
    // Write some valid json papers (one per line)
    fprintf(f, "{\"id\":\"demo.0001\",\"submitter\":\"Demo Author\",\"authors\":\"Jeevan Lal, Alex Smith\",\"title\":\"An Introduction to Trie Data Structures for Fast Autocomplete\",\"comments\":\"Demo Paper\",\"journal-ref\":\"J. Trie Sci. 2026\",\"doi\":\"10.1101/trie.0001\",\"report-no\":null,\"categories\":\"cs.DS cs.IR\",\"license\":null,\"abstract\":\"  This paper presents a detailed study of Trie data structures, focusing on First-Child Next-Sibling (FCNS) memory optimization. We demonstrate high-performance prefix searches and live suggestion lookups on large vocabularies.\",\"versions\":[{\"version\":\"v1\",\"created\":\"Mon, 15 Jun 2026 12:00:00 GMT\"}],\"update_date\":\"2026-06-16\",\"authors_parsed\":[[\"Lal\",\"Jeevan\",\"\"],[\"Smith\",\"Alex\",\"\"]]}\n");
    fprintf(f, "{\"id\":\"demo.0002\",\"submitter\":\"Demo Author\",\"authors\":\"Quantum Explorer Group\",\"title\":\"Quantum Computing and Neural Network Architectures\",\"comments\":\"Demo Paper\",\"journal-ref\":\"Quant. Inf. 2026\",\"doi\":\"10.1101/quantum.0002\",\"report-no\":null,\"categories\":\"quant-ph cs.NE\",\"license\":null,\"abstract\":\"  We explore the intersection of quantum computing mechanics with deep neural networks. In this paper, we focus on quantum dot layouts and physical representations of qubits in semiconductor materials.\",\"versions\":[{\"version\":\"v1\",\"created\":\"Tue, 16 Jun 2026 09:00:00 GMT\"}],\"update_date\":\"2026-06-16\",\"authors_parsed\":[[\"Explorer Group\",\"Quantum\",\"\"]]}\n");
    fprintf(f, "{\"id\":\"demo.0003\",\"submitter\":\"Demo Author\",\"authors\":\"AI Research Labs\",\"title\":\"Deep Learning and Large Language Models for Agentic Workflows\",\"comments\":\"Demo Paper\",\"journal-ref\":\"AI Res. 2026\",\"doi\":\"10.1101/ai.0003\",\"report-no\":null,\"categories\":\"cs.CL cs.AI\",\"license\":null,\"abstract\":\"  Large language models have shown remarkable capabilities in reasoning and autonomous planning. This work analyzes multi-agent coordination frameworks and evaluates planning success rates in complex software environments.\",\"versions\":[{\"version\":\"v1\",\"created\":\"Tue, 16 Jun 2026 10:00:00 GMT\"}],\"update_date\":\"2026-06-16\",\"authors_parsed\":[[\"Research Labs\",\"AI\",\"\"]]}\n");
    fclose(f);
}

int main() {
    const char* filename = "arxiv-metadata-oai-snapshot.json";
    
    printf("Opening dataset file: %s...\n", filename);
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        create_mock_dataset(filename);
        fd = open(filename, O_RDONLY);
        if (fd < 0) {
            perror("Failed to open dataset file after creating mock");
            return EXIT_FAILURE;
        }
    }
    
    struct stat sb;
    if (fstat(fd, &sb) < 0) {
        perror("Failed to get file size");
        close(fd);
        return EXIT_FAILURE;
    }
    
    g_file_size = sb.st_size;
    printf("Dataset file size: %.2f GB\n", (double)g_file_size / (1024.0 * 1024.0 * 1024.0));
    
    g_file_data = mmap(NULL, g_file_size, PROT_READ, MAP_SHARED, fd, 0);
    if (g_file_data == MAP_FAILED) {
        perror("mmap failed");
        close(fd);
        return EXIT_FAILURE;
    }
    
    printf("Scanning dataset lines to map offsets...\n");
    size_t line_capacity = 3200000;
    g_line_offsets = (size_t*)malloc(line_capacity * sizeof(size_t));
    g_line_count = 0;
    
    // Fast newline scanning loop
    for (size_t i = 0; i < g_file_size; i++) {
        if (i == 0 || g_file_data[i - 1] == '\n') {
            if (g_line_count >= line_capacity) {
                line_capacity += 500000;
                g_line_offsets = (size_t*)realloc(g_line_offsets, line_capacity * sizeof(size_t));
            }
            g_line_offsets[g_line_count++] = i;
        }
    }
    
    printf("Found %zu papers in the snapshot.\n", g_line_count);
    
    // Initialize Trie
    g_trie_root = create_trie_node('\0');
    
    printf("Building Trie index & gathering analytics...\n");
    for (size_t i = 0; i < g_line_count; i++) {
        if (i % 250000 == 0 && i > 0) {
            printf("Indexed %zu papers... (Trie nodes: %d)\n", i, total_nodes_allocated);
        }
        
        size_t len = 0;
        size_t start = g_line_offsets[i];
        size_t end = (i + 1 < g_line_count) ? g_line_offsets[i + 1] - 1 : g_file_size;
        len = end - start;
        
        extract_and_index_fields(g_file_data + start, len, (int)i, g_trie_root);
    }
    
    printf("Finalizing analytics statistics...\n");
    finalize_analytics();
    
    printf("Indexing completed successfully!\n");
    printf("Total Trie nodes allocated: %d\n", total_nodes_allocated);
    printf("Simulated Heap space used: %d bytes\n", current_heap_address - 0x1000);
    
    // Start HTTP server on port 8080
    start_server(8080);
    
    // Cleanup
    munmap(g_file_data, g_file_size);
    close(fd);
    free(g_line_offsets);
    free_trie(g_trie_root);
    
    return EXIT_SUCCESS;
}
