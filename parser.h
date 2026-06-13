#ifndef PARSER_H
#define PARSER_H

typedef struct {
    char id[32];
    char* title;
    char* authors;
    char* categories;
    char* abstract;
    char update_date[16];
} PaperMetadata;

// Parses a single JSON line of arXiv metadata and allocates strings for title, authors, categories, abstract.
// Returns NULL on failure.
PaperMetadata* parse_json_line(const char* line);

// Frees the dynamically allocated strings inside the PaperMetadata and the struct itself.
void free_paper_metadata(PaperMetadata* paper);

// Unescapes a JSON string in-place (handling \n, \t, \", etc.)
void unescape_string(char* str);

#endif // PARSER_H
