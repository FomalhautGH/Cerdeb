#include <libgen.h>

#define NOB_STRIP_PREFIX
#define NOB_IMPLEMENTATION
#include "../extern/nob.h"

#define STB_C_LEXER_IMPLEMENTATION
#include "../extern/stb_c_lexer.h"

#define BUILD_DIR "./output/"

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

#define BUF_LEN 1 << 10
stb_lexer lex = {0};
char string_store[BUF_LEN];
Struct str = {0};

int next_token() {
    return stb_c_lexer_get_token(&lex);
}

bool expect_id_name(char* id) {
    if (lex.token != CLEX_id || strcmp(lex.string, id) != 0) return false;
    stb_c_lexer_get_token(&lex);
    return true;
}

bool expect_char(char ch) {
    if (lex.token != ch) return false;
    stb_c_lexer_get_token(&lex);
    return true;
}

FormatType parse_type() {
    if (lex.token != CLEX_id) return BOGUS;

    // TODO: support unsigned and signed
    static const char* types[] = {
        "int", "short", "long", "float", "double", "char"
    };

    static const FormatType ftypes[] = {
        DECIMAL, DECIMAL, LONGDECIMAL, DOUBLE, DOUBLE, CHAR
    };

    size_t i = 0;
    for (i = 0; i < ARRAY_LEN(types); ++i) {
        if (strcmp(lex.string, types[i]) == 0) break;
    }

    if (i == ARRAY_LEN(types)) return BOGUS;
    stb_c_lexer_get_token(&lex);

    if (expect_char('*')) {
        if (ftypes[i] == CHAR) return STRING;
        return POINTER;
    } 

    return ftypes[i];
}

bool parse_field() {
    FormatType ft = parse_type();
    if (ft == BOGUS) return false;

    if (lex.token != CLEX_id) return false;

    Field field = { .ft = ft, .field_name = strdup(lex.string) };
    da_append(&str.fields, field);

    stb_c_lexer_get_token(&lex);
    if (!expect_char(';')) return false;
    return true;
}

bool parse_struct(size_t* pos) {
    if (!expect_id_name("typedef")) return false;
    if (!expect_id_name("struct")) return false;
    if (!expect_char('{')) return false;

    str.fields.count = 0;
    while (!expect_char('}')) {
        if (!parse_field()) return false;
    }

    if (lex.token != CLEX_id) return false;
    str.name = strdup(lex.string);
    stb_c_lexer_get_token(&lex);

    *pos = lex.where_firstchar - lex.input_stream + 1;
    if (!expect_char(';')) return false;
    return true;
}

String_Builder construct_debug_print() {
    String_Builder sb = {0};

    static const char* formats[] = {
        NULL, "%d", "%ld", "%f", "%s", "%p", "%c"
    };
    
    // TODO: dont use nob as a dependecy for using this constructed function
    sb_appendf(&sb, "char* debug_print(%s *str) {\n", str.name);
    sb_appendf(&sb, "    return temp_sprintf(\"%s {", str.name);

    for (size_t i = 0; i < str.fields.count; ++i) {
        if (i != 0) sb_appendf(&sb, ",");
        Field f = str.fields.items[i];
        const char* format = formats[f.ft];
        sb_appendf(&sb, " .%s = %s", f.field_name, format);
    }

    sb_appendf(&sb, " }\"");

    for (size_t i = 0; i < str.fields.count; ++i) {
        Field f = str.fields.items[i];
        sb_appendf(&sb, ", str->%s", f.field_name);
    }
    
    sb_appendf(&sb, ");\n}\n\n");
    sb_append_null(&sb);

    return sb;
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

bool ends_width(const char* string, const char* suffix) {
    size_t string_len = strlen(string);
    size_t suffix_len = strlen(suffix);

    if (string_len < suffix_len) return false;
    const char* suffix_end = suffix + suffix_len - 1;
    const char* string_end = string + string_len - 1;

    for (size_t i = 0; i < suffix_len; ++i) {
        if (*(suffix_end - i) != *(string_end - i)) return false;
    }

    return true;
}

void insert_debug_print(String_Builder *file, const char* print, size_t where, size_t comment) {
    sb_insert_at(file, "// ", comment);
    sb_insert_at(file, print, where + strlen("// "));
}

bool generate_file(String_Builder file, const char* file_name) {
    String_Builder out = {0};

    sb_appendf(&out, "%s/%s", BUILD_DIR, file_name);
    sb_append_null(&out);
    if (!write_entire_file(out.items, file.items, file.count)) return false;

    sb_free(out);
    return true;
}

bool parse_file(char* file_path) {
    String_Builder file = {0};
    if (!read_entire_file(file_path, &file)) return false;
    stb_c_lexer_init(&lex, file.items, file.items + file.count, string_store, BUF_LEN);

    size_t where = 0;
    size_t where_comment = 0;
    size_t scope_depth = 0;
    String_Builder debug_print = {0};
    while (stb_c_lexer_get_token(&lex)) {
        if (lex.token == CLEX_parse_error) {
            fprintf(stderr, "\n<<<PARSE ERROR>>>\n");
            break;
        }

        bool debug = false;
        switch (lex.token) {
            case '{': ++scope_depth; break;
            case '}': --scope_depth; break;
            case '!': {
                if (scope_depth > 0) break;
                where_comment = lex.where_firstchar - lex.input_stream;
                stb_c_lexer_get_token(&lex);
                if (expect_id_name("debug")) debug = true;
            } break;
        }

        // TODO: support more than one structure
        if (debug) {
            if (!parse_struct(&where)) return false;
            debug_print = construct_debug_print();
            debug = false;
        }
    }

    insert_debug_print(&file, debug_print.items, where, where_comment);
    if (!generate_file(file, basename(file_path))) return false;
    sb_free(file);
    return true;
}

bool parse_files(size_t argc, char** argv) {
    if (!mkdir_if_not_exists(BUILD_DIR)) return 1;

    // TODO: input files must be the first files provided for now
    for (size_t i = 1; i < argc; ++i) {
        if (*argv[i] == '-') break;
        if (!parse_file(argv[i])) return false;
    }

    return true;
}

bool compile_files(size_t argc, char** argv) {
    Cmd comp = {0};
    nob_cc(&comp);

    size_t i = 1;
    String_Builder sb = {0};
    for (; i < argc; ++i) {
        if (*argv[i] == '-') break;

        sb.count = 0;
        sb_appendf(&sb, "%s%s", BUILD_DIR, basename(argv[i]));
        sb_append_null(&sb);

        cmd_append(&comp, sb.items);
    }

    for (; i < argc; ++i) cmd_append(&comp, argv[i]);
    if (!cmd_run(&comp)) return false;
    return true;
}

int main(int argc, char** argv) {
    assert(argc >= 2);

    if (!parse_files(argc, argv)) return 1;
    if (!compile_files(argc, argv)) return 2;

    return 0;
}
