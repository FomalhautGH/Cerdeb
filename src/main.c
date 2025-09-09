#include <assert.h>
#include <stddef.h>
#include <stdio.h>
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

    if (!expect_char(lex, ';')) return false;
    return true;
}

void construct_debug_print(String_Builder *sb) {
    static const char* formats[] = {
        NULL, "%d", "%ld", "%f", "%s", "%p", "%c"
    };
    
    // TODO: dont use nob as a dependecy for using this constructed function
    sb_appendf(sb, "char* debug_print(%s *str) {\n", str.name);
    sb_appendf(sb, "    return temp_sprintf(\"%s {", str.name);

    for (size_t i = 0; i < str.fields.count; ++i) {
        if (i != 0) sb_appendf(sb, ",");
        Field f = str.fields.items[i];
        const char* format = formats[f.ft];
        sb_appendf(sb, " .%s = %s", f.field_name, format);
    }

    sb_appendf(sb, " })\"");

    for (size_t i = 0; i < str.fields.count; ++i) {
        Field f = str.fields.items[i];
        sb_appendf(sb, ", str->%s", f.field_name);
    }
    
    sb_appendf(sb, ");\n}\n\n");
    sb_append_null(sb);
}

void sb_insert_at(String_Builder* dest, const char* src, size_t pos) {
    assert(pos < dest->count);

    size_t added_chars = strlen(src);
    size_t len = dest->count + added_chars;

    da_reserve(dest, len);
    dest->count = len;

    for (size_t i = len - 1; i - added_chars + 1 > pos; --i) {
        dest->items[i] = dest->items[i - added_chars];
    }

    for (size_t i = pos; i < pos + added_chars; ++i) {
        dest->items[i] = src[i - pos];
    }

}

int main(int argc, char** argv) {
    UNUSED(argc);
    UNUSED(argv);
    
    assert(argc >= 2);

    String_Builder file = {0};
    if (!read_entire_file(argv[1], &file)) return 1;

    stb_lexer lex = {0};
    const size_t buf_len = 1 << 10;
    char string_store[buf_len];
    stb_c_lexer_init(&lex, file.items, file.items + file.count, string_store, buf_len);

    size_t pos = 0;
    size_t pos_rm = 0;
    String_Builder result = {0};
    while (stb_c_lexer_get_token(&lex)) {
        if (lex.token == CLEX_parse_error) {
            printf("\n<<<PARSE ERROR>>>\n");
            break;
        }

        // TODO: support more than one structure
        if (lex.token == '!') {
            pos_rm = lex.where_firstchar - file.items;
            stb_c_lexer_get_token(&lex);
            if (expect_id_name(&lex, "debug")) {
                if (!parse_struct(&lex)) return 2;
                construct_debug_print(&result);
                pos = lex.where_firstchar - file.items;
            }
        } 
    }

    sb_insert_at(&file, "// ", pos_rm);
    sb_insert_at(&file, result.items, pos + strlen("// "));

    if (!write_entire_file("out.c", file.items, file.count)) return 1;

    Cmd cmd = {0};
    nob_cc(&cmd);
    nob_cc_inputs(&cmd, "out.c");
    nob_cc_output(&cmd, "out");
    if (!cmd_run(&cmd)) return 1;

    sb_free(file);
    return 0;
}
