#include <stddef.h>
#include <string.h>
#define NOB_STRIP_PREFIX
#define NOB_IMPLEMENTATION
#include "../extern/nob.h"

#define STB_C_LEXER_IMPLEMENTATION
#include "../extern/stb_c_lexer.h"

typedef enum {
    BOGUS, // invalid type
    DECIMAL,
    LONGDECIMAL,
    DOUBLE,
    STRING,
    POINTER,
    CHAR
} FormatType;

typedef struct {
    char* field_name;
    FormatType ft;
} Field;

typedef struct {
    Field* items;
    size_t count;
    size_t capacity;
} Fields;

typedef struct {
    char* name;
    Fields fields;
} Struct;

Struct str = {0};

int next_token(stb_lexer* lex) {
    return stb_c_lexer_get_token(lex);
}

bool expect_id_name(stb_lexer* lex, char* id) {
    if (lex->token != CLEX_id || strcmp(lex->string, id) != 0) return false;
    stb_c_lexer_get_token(lex);
    return true;
}

bool expect_char(stb_lexer* lex, char ch) {
    if (lex->token != ch) return false;
    stb_c_lexer_get_token(lex);
    return true;
}

FormatType parse_type(stb_lexer* lex) {
    if (lex->token != CLEX_id) return BOGUS;

    // TODO: support unsigned and signed
    static const char* types[] = {
        "int", "short", "long", "float", "double", "char"
    };

    static const FormatType ftypes[] = {
        DECIMAL, DECIMAL, LONGDECIMAL, DOUBLE, DOUBLE, CHAR
    };

    size_t i = 0;
    for (i = 0; i < ARRAY_LEN(types); ++i) {
        if (strcmp(lex->string, types[i]) == 0) break;
    }

    if (i == ARRAY_LEN(types)) return BOGUS;
    stb_c_lexer_get_token(lex);

    if (expect_char(lex, '*')) {
        if (ftypes[i] == CHAR) return STRING;
        return POINTER;
    } 

    return ftypes[i];
}

bool parse_field(stb_lexer* lex) {
    FormatType ft = parse_type(lex);
    if (ft == BOGUS) return false;

    if (lex->token != CLEX_id) return false;

    Field field = { .ft = ft, .field_name = strdup(lex->string) };
    da_append(&str.fields, field);

    stb_c_lexer_get_token(lex);
    if (!expect_char(lex, ';')) return false;
    return true;
}

bool parse_struct(stb_lexer* lex) {
    if (!expect_id_name(lex, "typedef")) return false;
    if (!expect_id_name(lex, "struct")) return false;
    if (!expect_char(lex, '{')) return false;

    while (!expect_char(lex, '}')) {
        if (!parse_field(lex)) return false;
    }

    if (lex->token != CLEX_id) return false;
    str.name = strdup(lex->string);
    stb_c_lexer_get_token(lex);
    return true;
}

int main(int argc, char** argv) {
    UNUSED(argc);
    UNUSED(argv);

    String_Builder file = {0};
    if (!read_entire_file("./test/struct.c", &file)) return 1;

    stb_lexer lex = {0};
    const size_t buf_len = 1 << 10;
    char string_store[buf_len];
    stb_c_lexer_init(&lex, file.items, file.items + file.count, string_store, buf_len);

    while (stb_c_lexer_get_token(&lex)) {
        if (lex.token == CLEX_parse_error) {
            printf("\n<<<PARSE ERROR>>>\n");
            break;
        }

        // TODO: support more than one structure
        if (lex.token == '!') {
            stb_c_lexer_get_token(&lex);
            if (expect_id_name(&lex, "debug")) {
                if (!parse_struct(&lex)) return 69;
            }

        } 
    }

    printf("%s\n", str.name);
    for (size_t i = 0; i < str.fields.count; ++i) {
        Field f = str.fields.items[i];
        printf("ft: %d, name: %s\n", f.ft, f.field_name);
    }

    sb_free(file);
    return 0;
}
