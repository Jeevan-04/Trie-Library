#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"

void unescape_string(char* str) {
    if (!str) return;
    char* src = str;
    char* dst = str;
    while (*src) {
        if (*src == '\\' && *(src + 1)) {
            src++;
            switch (*src) {
                case 'n': *dst++ = '\n'; break;
                case 't': *dst++ = '\t'; break;
                case 'r': *dst++ = '\r'; break;
                case 'b': *dst++ = '\b'; break;
                case 'f': *dst++ = '\f'; break;
                case '"': *dst++ = '"'; break;
                case '\\': *dst++ = '\\'; break;
                case '/': *dst++ = '/'; break;
                default: *dst++ = *src; break;
            }
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

static char* extract_field(const char* line, const char* key) {
    char key_pattern[128];
    snprintf(key_pattern, sizeof(key_pattern), "\"%s\"", key);
    const char* key_pos = strstr(line, key_pattern);
    if (!key_pos) return NULL;
    
    key_pos += strlen(key_pattern);
    
    // Skip colon and whitespaces
    while (*key_pos == ' ' || *key_pos == '\t' || *key_pos == ':') {
        key_pos++;
    }
    
    if (*key_pos == '"') {
        key_pos++; // Skip opening quote
        const char* end = key_pos;
        size_t len = 0;
        while (*end) {
            if (*end == '\\' && *(end + 1)) {
                end += 2;
                len += 2;
            } else if (*end == '"') {
                break;
            } else {
                end++;
                len++;
            }
        }
        
        char* val = (char*)malloc(len + 1);
        if (!val) return NULL;
        
        const char* src = key_pos;
        char* dst = val;
        while (src < end) {
            *dst++ = *src++;
        }
        *dst = '\0';
        unescape_string(val);
        return val;
    } else if (strncmp(key_pos, "null", 4) == 0) {
        return NULL;
    }
    return NULL;
}

PaperMetadata* parse_json_line(const char* line) {
    if (!line) return NULL;
    
    PaperMetadata* paper = (PaperMetadata*)malloc(sizeof(PaperMetadata));
    if (!paper) return NULL;
    
    memset(paper, 0, sizeof(PaperMetadata));
    
    char* id_str = extract_field(line, "id");
    if (id_str) {
        strncpy(paper->id, id_str, sizeof(paper->id) - 1);
        free(id_str);
    }
    
    paper->title = extract_field(line, "title");
    paper->authors = extract_field(line, "authors");
    paper->categories = extract_field(line, "categories");
    paper->abstract = extract_field(line, "abstract");
    
    char* date_str = extract_field(line, "update_date");
    if (date_str) {
        strncpy(paper->update_date, date_str, sizeof(paper->update_date) - 1);
        free(date_str);
    }
    
    // Standardize default strings if missing to avoid NULL pointer issues
    if (!paper->title) paper->title = strdup("");
    if (!paper->authors) paper->authors = strdup("");
    if (!paper->categories) paper->categories = strdup("");
    if (!paper->abstract) paper->abstract = strdup("");
    
    return paper;
}

void free_paper_metadata(PaperMetadata* paper) {
    if (!paper) return;
    if (paper->title) free(paper->title);
    if (paper->authors) free(paper->authors);
    if (paper->categories) free(paper->categories);
    if (paper->abstract) free(paper->abstract);
    free(paper);
}
