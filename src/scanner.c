#include <assert.h>
#include <stdio.h>
#include <tree_sitter/parser.h>
#include <wctype.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define VEC_RESIZE(vec, _cap)                                                  \
    {                                                                          \
        (vec)->data = realloc((vec)->data, (_cap) * sizeof((vec)->data[0]));   \
        assert((vec)->data != NULL);                                           \
        (vec)->cap = (_cap);                                                   \
    }

#define VEC_PUSH(vec, el)                                                      \
    {                                                                          \
        if ((vec)->cap == (vec)->len) {                                        \
            VEC_RESIZE((vec), MAX(16, (vec)->len * 2));                        \
        }                                                                      \
        (vec)->data[(vec)->len++] = (el);                                      \
    }

#define VEC_POP(vec) (vec)->len--;

#define VEC_BACK(vec) ((vec)->data[(vec)->len - 1])

#define VEC_FREE(vec)                                                          \
    {                                                                          \
        if ((vec)->data != NULL)                                               \
            free((vec)->data);                                                 \
    }

#define VEC_CLEAR(vec)                                                         \
    {                                                                          \
        (vec)->len = 0;                                                        \
    }

enum TokenType {
    LISTSTART,
    LISTEND,
    LISTITEMEND,
    BULLET,
    HLSTARS,
    SECTIONEND,
    ENDOFFILE,
    LINKOPEN,
    FOOTNOTEOPEN,
    FNDEFOPEN,
    LATEX_MATH_SINGLE_DOLLAR,
    TEXT_DOLLAR,
    BOLD_OPEN,
    BOLD_CLOSE,
    ITALIC_OPEN,
    ITALIC_CLOSE,
    UNDERLINE_OPEN,
    UNDERLINE_CLOSE,
    STRIKETHROUGH_OPEN,
    STRIKETHROUGH_CLOSE,
    CODE_OPEN,
    CODE_CLOSE,
    VERBATIM_OPEN,
    VERBATIM_CLOSE,
    ERROR_SENTINEL
};

typedef enum {
    MARKUP_NONE,
    MARKUP_BOLD,
    MARKUP_ITALIC,
    MARKUP_UNDERLINE,
    MARKUP_STRIKETHROUGH,
    MARKUP_CODE,
    MARKUP_VERBATIM,
} Markup;

typedef enum {
    NOTABULLET,
    DASH,
    PLUS,
    STAR,
    LOWERDOT,
    UPPERDOT,
    LOWERPAREN,
    UPPERPAREN,
    NUMDOT,
    NUMPAREN,
} Bullet;

typedef struct {
    uint32_t len;
    uint32_t cap;
    int16_t *data;
} stack;

typedef struct {
    stack *indent_length_stack;
    stack *bullet_stack;
    stack *section_stack;
    stack *markup_stack;
    bool in_dollar_math;
} Scanner;

static inline void advance(TSLexer *lexer) { lexer->advance(lexer, false); }

static inline void skip(TSLexer *lexer) { lexer->advance(lexer, true); }

static unsigned serialize(Scanner *scanner, char *buffer) {
    size_t i = 0;

    size_t indent_count = MIN(scanner->indent_length_stack->len - 1, UINT8_MAX);
    size_t section_count = MIN(scanner->section_stack->len - 1, UINT8_MAX);
    size_t markup_count = MIN(scanner->markup_stack->len, UINT8_MAX);

    // The serialized layout is:
    // [indent_count][section_count][markup_count]
    // [indent entries...][bullet entries...][section entries...]
    // [markup entries...][in_dollar_math]
    size_t serialized_size =
        3 + indent_count * 2 + section_count + markup_count + 1;

    while (serialized_size > TREE_SITTER_SERIALIZATION_BUFFER_SIZE) {
        if (markup_count > 0) {
            markup_count--;
        } else if (section_count > 0) {
            section_count--;
        } else if (indent_count > 0) {
            indent_count--;
        } else {
            break;
        }

        serialized_size =
            3 + indent_count * 2 + section_count + markup_count + 1;
    }

    buffer[i++] = indent_count;
    buffer[i++] = section_count;
    buffer[i++] = markup_count;

    int iter = 1;
    for (; iter <= indent_count; ++iter) {
        buffer[i++] = scanner->indent_length_stack->data[iter];
    }

    iter = 1;
    for (; iter <= indent_count; ++iter) {
        buffer[i++] = scanner->bullet_stack->data[iter];
    }

    iter = 1;
    for (; iter <= section_count; ++iter) {
        buffer[i++] = scanner->section_stack->data[iter];
    }

    iter = 0;
    for (; iter < markup_count; ++iter) {
        buffer[i++] = scanner->markup_stack->data[iter];
    }

    buffer[i++] = scanner->in_dollar_math;

    return i;
}

static void deserialize(Scanner *scanner, const char *buffer, unsigned length) {
    VEC_CLEAR(scanner->section_stack);
    VEC_PUSH(scanner->section_stack, 0);
    VEC_CLEAR(scanner->indent_length_stack);
    VEC_PUSH(scanner->indent_length_stack, -1);
    VEC_CLEAR(scanner->bullet_stack);
    VEC_PUSH(scanner->bullet_stack, NOTABULLET);
    VEC_CLEAR(scanner->markup_stack);
    scanner->in_dollar_math = false;

    if (length == 0)
        return;

    // The current serialization format always has at least four bytes:
    // [indent_count][section_count][markup_count][in_dollar_math]
    size_t minimum_serialized_size = 4;
    if (length < minimum_serialized_size)
        return;

    size_t i = 0;

    size_t indent_count = (uint8_t)buffer[i++];
    size_t section_count = (uint8_t)buffer[i++];
    size_t markup_count = (uint8_t)buffer[i++];

    for (size_t iter = 0; iter < indent_count && i < length; ++iter, ++i)
        VEC_PUSH(scanner->indent_length_stack, buffer[i]);
    for (size_t iter = 0; iter < indent_count && i < length; ++iter, ++i)
        VEC_PUSH(scanner->bullet_stack, buffer[i]);
    for (size_t iter = 0; iter < section_count && i < length; ++iter, ++i)
        VEC_PUSH(scanner->section_stack, buffer[i]);
    for (size_t iter = 0; iter < markup_count && i < length; ++iter, ++i)
        VEC_PUSH(scanner->markup_stack, buffer[i]);

    if (i < length)
        scanner->in_dollar_math = buffer[i];
}

static bool dedent(Scanner *scanner, TSLexer *lexer) {
    VEC_POP(scanner->indent_length_stack);
    VEC_POP(scanner->bullet_stack);
    lexer->result_symbol = LISTEND;
    return true;
}

static Bullet getbullet(TSLexer *lexer) {
    if (lexer->lookahead == '-') {
        advance(lexer);
        if (iswspace(lexer->lookahead))
            return DASH;
    } else if (lexer->lookahead == '+') {
        advance(lexer);
        if (iswspace(lexer->lookahead))
            return PLUS;
    } else if (lexer->lookahead == '*') {
        advance(lexer);
        if (iswspace(lexer->lookahead))
            return STAR;
    } else if ('a' <= lexer->lookahead && lexer->lookahead <= 'z') {
        advance(lexer);
        if (lexer->lookahead == '.') {
            advance(lexer);
            if (iswspace(lexer->lookahead))
                return LOWERDOT;
        } else if (lexer->lookahead == ')') {
            advance(lexer);
            if (iswspace(lexer->lookahead))
                return LOWERPAREN;
        }
    } else if ('A' <= lexer->lookahead && lexer->lookahead <= 'Z') {
        advance(lexer);
        if (lexer->lookahead == '.') {
            advance(lexer);
            if (iswspace(lexer->lookahead))
                return UPPERDOT;
        } else if (lexer->lookahead == ')') {
            advance(lexer);
            if (iswspace(lexer->lookahead))
                return UPPERPAREN;
        }
    } else if ('0' <= lexer->lookahead && lexer->lookahead <= '9') {
        do {
            advance(lexer);
        } while ('0' <= lexer->lookahead && lexer->lookahead <= '9');
        if (lexer->lookahead == '.') {
            advance(lexer);
            if (iswspace(lexer->lookahead))
                return NUMDOT;
        } else if (lexer->lookahead == ')') {
            advance(lexer);
            if (iswspace(lexer->lookahead))
                return NUMPAREN;
        }
    }
    return NOTABULLET;
}

static Markup delimiter_to_markup(int32_t c) {
    switch (c) {
    case '*':
        return MARKUP_BOLD;
    case '/':
        return MARKUP_ITALIC;
    case '_':
        return MARKUP_UNDERLINE;
    case '+':
        return MARKUP_STRIKETHROUGH;
    case '=':
        return MARKUP_CODE;
    case '~':
        return MARKUP_VERBATIM;
    default:
        return MARKUP_NONE;
    }
}

static int32_t markup_to_delimiter(Markup markup) {
    switch (markup) {
    case MARKUP_BOLD:
        return '*';
    case MARKUP_ITALIC:
        return '/';
    case MARKUP_UNDERLINE:
        return '_';
    case MARKUP_STRIKETHROUGH:
        return '+';
    case MARKUP_CODE:
        return '=';
    case MARKUP_VERBATIM:
        return '~';
    default:
        return '\0';
    }
}

static enum TokenType markup_open_token(Markup markup) {
    switch (markup) {
    case MARKUP_BOLD:
        return BOLD_OPEN;
    case MARKUP_ITALIC:
        return ITALIC_OPEN;
    case MARKUP_UNDERLINE:
        return UNDERLINE_OPEN;
    case MARKUP_STRIKETHROUGH:
        return STRIKETHROUGH_OPEN;
    case MARKUP_CODE:
        return CODE_OPEN;
    case MARKUP_VERBATIM:
        return VERBATIM_OPEN;
    default:
        return ERROR_SENTINEL;
    }
}

static enum TokenType markup_close_token(Markup markup) {
    switch (markup) {
    case MARKUP_BOLD:
        return BOLD_CLOSE;
    case MARKUP_ITALIC:
        return ITALIC_CLOSE;
    case MARKUP_UNDERLINE:
        return UNDERLINE_CLOSE;
    case MARKUP_STRIKETHROUGH:
        return STRIKETHROUGH_CLOSE;
    case MARKUP_CODE:
        return CODE_CLOSE;
    case MARKUP_VERBATIM:
        return VERBATIM_CLOSE;
    default:
        return ERROR_SENTINEL;
    }
}

static bool is_markup_content_char(int32_t c) {
    return c != '\0' && c != '\n' && c != '\r' && !iswspace(c);
}

static bool is_markup_delimiter_char(int32_t c) {
    return delimiter_to_markup(c) != MARKUP_NONE;
}

static bool is_valid_post_marker_char(int32_t c) {
    return c == '\0' || c == '\n' || c == '\r' || c == ' ' || c == ')' ||
           c == '-' || c == '}' || c == '"' || c == '\'' || c == ':' ||
           c == ';' || c == '!' || c == '\\' || c == '[' || c == ',' ||
           c == '.' || c == '?' || is_markup_delimiter_char(c);
}

static bool is_literal_markup(Markup markup) {
    return markup == MARKUP_CODE || markup == MARKUP_VERBATIM;
}

static bool scan_markup_delimiter_from_consumed(Scanner *scanner, TSLexer *lexer,
                                                const bool *valid_symbols,
                                                Markup markup) {
    enum TokenType open_token = markup_open_token(markup);
    enum TokenType close_token = markup_close_token(markup);
    bool can_open = valid_symbols[open_token];
    bool can_close = valid_symbols[close_token];

    if (!can_open && !can_close)
        return false;

    Markup active_markup =
        scanner->markup_stack->len > 0 ? VEC_BACK(scanner->markup_stack)
                                       : MARKUP_NONE;

    if (can_close && active_markup == markup) {
        if (is_valid_post_marker_char(lexer->lookahead)) {
            VEC_POP(scanner->markup_stack);
            lexer->result_symbol = close_token;
            return true;
        }
        return false;
    }

    if (!can_open || active_markup == markup)
        return false;
    if (is_literal_markup(active_markup))
        return false;

    int32_t delimiter = markup_to_delimiter(markup);
    if (!is_markup_content_char(lexer->lookahead))
        return false;
    if (lexer->lookahead == delimiter)
        return false;

    int32_t previous = '\0';
    while (lexer->lookahead != '\0' && lexer->lookahead != '\n' &&
           lexer->lookahead != '\r') {
        if (lexer->lookahead == delimiter) {
            int32_t before_delimiter = previous;
            advance(lexer);
            if (is_markup_content_char(before_delimiter) &&
                is_valid_post_marker_char(lexer->lookahead)) {
                VEC_PUSH(scanner->markup_stack, markup);
                lexer->result_symbol = open_token;
                return true;
            }
            previous = delimiter;
            continue;
        }

        previous = lexer->lookahead;
        advance(lexer);
    }

    return false;
}

static bool scan_markup_delimiter(Scanner *scanner, TSLexer *lexer,
                                  const bool *valid_symbols) {
    Markup markup = delimiter_to_markup(lexer->lookahead);
    if (markup == MARKUP_NONE)
        return false;

    advance(lexer);
    lexer->mark_end(lexer);
    return scan_markup_delimiter_from_consumed(scanner, lexer, valid_symbols,
                                               markup);
}

static bool scan(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols) {
    // Error recovery
    if (valid_symbols[ERROR_SENTINEL]) {
        return false;
    }

    // Org inline markup does not span lines, so stale unmatched delimiters from
    // a previous line must not suppress valid markup on later lines.
    if (lexer->get_column(lexer) == 0) {
        VEC_CLEAR(scanner->markup_stack);
    }

    // - Section ends
    int16_t indent_length = 0;
    lexer->mark_end(lexer);
    for (;;) {
        if (lexer->lookahead == ' ') {
            indent_length++;
        } else if (lexer->lookahead == '\t') {
            indent_length += 8;
        } else if (lexer->lookahead == '\0') {
            if (valid_symbols[LISTEND]) {
                lexer->result_symbol = LISTEND;
            } else if (valid_symbols[SECTIONEND]) {
                lexer->result_symbol = SECTIONEND;
            } else if (valid_symbols[ENDOFFILE]) {
                lexer->result_symbol = ENDOFFILE;
            } else
                return false;

            return true;
        } else {
            break;
        }
        skip(lexer);
    }

    // - Listiem ends
    // Listend -> end of a line, looking for:
    // 1. dedent
    // 2. same indent, not a bullet
    // 3. two eols
    int16_t newlines = 0;
    if (valid_symbols[LISTEND] || valid_symbols[LISTITEMEND]) {
        for (;;) {
            if (lexer->lookahead == ' ') {
                indent_length++;
            } else if (lexer->lookahead == '\t') {
                indent_length += 8;
            } else if (lexer->lookahead == '\0') {
                return dedent(scanner, lexer);
            } else if (lexer->lookahead == '\n') {
                if (++newlines > 1)
                    return dedent(scanner, lexer);
                indent_length = 0;
            } else {
                break;
            }
            skip(lexer);
        }

        if (indent_length < VEC_BACK(scanner->indent_length_stack)) {
            return dedent(scanner, lexer);
        } else if (indent_length == VEC_BACK(scanner->indent_length_stack)) {
            if (getbullet(lexer) == VEC_BACK(scanner->bullet_stack)) {
                lexer->result_symbol = LISTITEMEND;
                return true;
            }
            return dedent(scanner, lexer);
        }
    }

    // - Col=0 star
    if (indent_length == 0 && lexer->get_column(lexer) == 0 &&
        lexer->lookahead == '*') {
        lexer->mark_end(lexer);
        int16_t stars = 1;
        advance(lexer);
        while (lexer->lookahead == '*') {
            stars++;
            advance(lexer);
        }

        if (lexer->lookahead == '\n') {
            return false;
        }

        if (valid_symbols[SECTIONEND] && iswspace(lexer->lookahead) &&
            stars > 0 && stars <= VEC_BACK(scanner->section_stack)) {
            VEC_POP(scanner->section_stack);
            lexer->result_symbol = SECTIONEND;
            return true;
        } else if (valid_symbols[HLSTARS] && iswspace(lexer->lookahead)) {
            VEC_PUSH(scanner->section_stack, stars);
            lexer->result_symbol = HLSTARS;
            return true;
        }

        if (stars == 1) {
            lexer->mark_end(lexer);
            if (scan_markup_delimiter_from_consumed(
                    scanner, lexer, valid_symbols, MARKUP_BOLD)) {
                return true;
            }
        }

        return false;
    }

    // - Liststart and bullets
    if ((valid_symbols[LISTSTART] || valid_symbols[BULLET]) && newlines == 0) {
        Bullet bullet = getbullet(lexer);

        if (valid_symbols[BULLET] &&
            bullet == VEC_BACK(scanner->bullet_stack) &&
            indent_length == VEC_BACK(scanner->indent_length_stack)) {
            lexer->mark_end(lexer);
            lexer->result_symbol = BULLET;
            return true;
        } else if (valid_symbols[LISTSTART] && bullet != NOTABULLET &&
                   indent_length > VEC_BACK(scanner->indent_length_stack)) {
            VEC_PUSH(scanner->indent_length_stack, indent_length);
            VEC_PUSH(scanner->bullet_stack, bullet);
            lexer->result_symbol = LISTSTART;
            return true;
        }
    }

    if (scan_markup_delimiter(scanner, lexer, valid_symbols)) {
        return true;
    }

    if ((valid_symbols[LINKOPEN] || valid_symbols[FOOTNOTEOPEN] ||
         valid_symbols[FNDEFOPEN]) &&
        lexer->lookahead == '[') {
        bool at_bol = lexer->get_column(lexer) == 0;
        advance(lexer);
        if (valid_symbols[LINKOPEN] && lexer->lookahead == '[') {
            advance(lexer);
            lexer->mark_end(lexer);
            bool has_content = false;
            while (lexer->lookahead != '\n' && lexer->lookahead != '\0') {
                int32_t prev_lookahead = lexer->lookahead;
                advance(lexer);
                if (prev_lookahead == ']' && lexer->lookahead == ']') {
                    advance(lexer);
                    if (!has_content) {
                        return false;
                    }
                    lexer->result_symbol = LINKOPEN;
                    return true;
                }
                has_content = true;
            }
        }

        if ((valid_symbols[FOOTNOTEOPEN] || valid_symbols[FNDEFOPEN]) &&
            towlower(lexer->lookahead) == 'f') {
            advance(lexer);
            if (towlower(lexer->lookahead) != 'n')
                return false;
            advance(lexer);
            if (lexer->lookahead != ':')
                return false;
            advance(lexer);
            lexer->mark_end(lexer);

            bool has_label = false;
            while (lexer->lookahead != '\n' && lexer->lookahead != '\r' &&
                   lexer->lookahead != '\0' && !iswspace(lexer->lookahead) &&
                   lexer->lookahead != ']') {
                has_label = true;
                advance(lexer);
            }

            if (has_label && lexer->lookahead == ']') {
                // Prefer FNDEFOPEN (fndef start) when at beginning of line
                if (valid_symbols[FNDEFOPEN] && at_bol) {
                    lexer->result_symbol = FNDEFOPEN;
                } else if (valid_symbols[FOOTNOTEOPEN]) {
                    lexer->result_symbol = FOOTNOTEOPEN;
                } else {
                    return false;
                }
                return true;
            }
        }
    }

    // $ LaTeX math delimiters
    if (lexer->lookahead == '$') {
        advance(lexer);
        lexer->mark_end(lexer);
        if (scanner->in_dollar_math &&
            valid_symbols[LATEX_MATH_SINGLE_DOLLAR]) {
            // look for closing dollar
            scanner->in_dollar_math = false;
            lexer->result_symbol = LATEX_MATH_SINGLE_DOLLAR;
            return true;
        }

        if (valid_symbols[LATEX_MATH_SINGLE_DOLLAR]) {
            // look for opening dollar
            if (lexer->lookahead == '$') {
                return false; // ignore $$ (handled by grammar)
            }
            while (lexer->lookahead != '\n' && !lexer->eof(lexer)) {
                // check until EOL for closing dollar
                advance(lexer);
                if (lexer->lookahead == '$') {
                    advance(lexer);
                    if (('0' <= lexer->lookahead && lexer->lookahead <= '9') ||
                        ('A' <= lexer->lookahead && lexer->lookahead <= 'Z') ||
                        ('a' <= lexer->lookahead && lexer->lookahead <= 'z')) {
                        // next dollar is part of word
                        if (valid_symbols[TEXT_DOLLAR]) {
                            lexer->result_symbol = TEXT_DOLLAR;
                            return true;
                        }
                    } else {
                        if (valid_symbols[LATEX_MATH_SINGLE_DOLLAR]) {
                            scanner->in_dollar_math = true;
                            lexer->result_symbol = LATEX_MATH_SINGLE_DOLLAR;
                            return true;
                        }
                    }
                }
            }

            // didn't find closing dollar
            if (valid_symbols[TEXT_DOLLAR]) {
                lexer->result_symbol = TEXT_DOLLAR;
                return true;
            }
        }
    }

    return false; // default
}

void *tree_sitter_org_external_scanner_create() {
    Scanner *scanner = (Scanner *)calloc(1, sizeof(Scanner));
    scanner->indent_length_stack = (stack *)calloc(1, sizeof(stack));
    scanner->bullet_stack = (stack *)calloc(1, sizeof(stack));
    scanner->section_stack = (stack *)calloc(1, sizeof(stack));
    scanner->markup_stack = (stack *)calloc(1, sizeof(stack));
    deserialize(scanner, NULL, 0);
    return scanner;
}

bool tree_sitter_org_external_scanner_scan(void *payload, TSLexer *lexer,
                                           const bool *valid_symbols) {
    Scanner *scanner = (Scanner *)payload;
    return scan(scanner, lexer, valid_symbols);
}

unsigned tree_sitter_org_external_scanner_serialize(void *payload,
                                                    char *buffer) {
    Scanner *scanner = (Scanner *)payload;
    return serialize(scanner, buffer);
}

void tree_sitter_org_external_scanner_deserialize(void *payload,
                                                  const char *buffer,
                                                  unsigned length) {
    Scanner *scanner = (Scanner *)payload;
    deserialize(scanner, buffer, length);
}

void tree_sitter_org_external_scanner_destroy(void *payload) {
    Scanner *scanner = (Scanner *)payload;
    VEC_FREE(scanner->indent_length_stack);
    VEC_FREE(scanner->bullet_stack);
    VEC_FREE(scanner->section_stack);
    VEC_FREE(scanner->markup_stack);
    free(scanner->indent_length_stack);
    free(scanner->bullet_stack);
    free(scanner->section_stack);
    free(scanner->markup_stack);
    free(scanner);
}
