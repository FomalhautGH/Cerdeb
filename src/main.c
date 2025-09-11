#include <libgen.h>

#define NOB_STRIP_PREFIX
#define NOB_IMPLEMENTATION
#include "../extern/nob.h"

#define STB_C_LEXER_IMPLEMENTATION
#include "../extern/stb_c_lexer.h"

#define BUILD_DIR "./output/"

static void print_token(stb_lexer *lexer) {
   switch (lexer->token) {
      case CLEX_id        : printf("_%s", lexer->string); break;
      case CLEX_eq        : printf("=="); break;
      case CLEX_noteq     : printf("!="); break;
      case CLEX_lesseq    : printf("<="); break;
      case CLEX_greatereq : printf(">="); break;
      case CLEX_andand    : printf("&&"); break;
      case CLEX_oror      : printf("||"); break;
      case CLEX_shl       : printf("<<"); break;
      case CLEX_shr       : printf(">>"); break;
      case CLEX_plusplus  : printf("++"); break;
      case CLEX_minusminus: printf("--"); break;
      case CLEX_arrow     : printf("->"); break;
      case CLEX_andeq     : printf("&="); break;
      case CLEX_oreq      : printf("|="); break;
      case CLEX_xoreq     : printf("^="); break;
      case CLEX_pluseq    : printf("+="); break;
      case CLEX_minuseq   : printf("-="); break;
      case CLEX_muleq     : printf("*="); break;
      case CLEX_diveq     : printf("/="); break;
      case CLEX_modeq     : printf("%%="); break;
      case CLEX_shleq     : printf("<<="); break;
      case CLEX_shreq     : printf(">>="); break;
      case CLEX_eqarrow   : printf("=>"); break;
      case CLEX_dqstring  : printf("\"%s\"", lexer->string); break;
      case CLEX_sqstring  : printf("'\"%s\"'", lexer->string); break;
      case CLEX_charlit   : printf("'%s'", lexer->string); break;
      #if defined(STB__clex_int_as_double) && !defined(STB__CLEX_use_stdlib)
      case CLEX_intlit    : printf("#%g", lexer->real_number); break;
      #else
      case CLEX_intlit    : printf("#%ld", lexer->int_number); break;
      #endif
      case CLEX_floatlit  : printf("%g", lexer->real_number); break;
      default:
         if (lexer->token >= 0 && lexer->token < 256)
            printf("%c", (int) lexer->token);
         else {
            printf("<<<UNKNOWN TOKEN %ld >>>\n", lexer->token);
         }
         break;
   }

   printf("\n");
}

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
    const char* field_name;
    FormatType ft;
} Field;

typedef struct {
    Field* items;
    size_t count;
    size_t capacity;
} Fields;

typedef struct {
    size_t pos_print;
    size_t pos_comment;
    const char* name;
    Fields fields;
} Struct;

typedef struct {
    Struct* items;
    size_t count;
    size_t capacity;
} Structs;

#define BUF_LEN 1 << 10
stb_lexer lex = {0};
char string_store[BUF_LEN];
Structs structs = {0};

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
    if (strcmp(lex.string, "const") == 0) stb_c_lexer_get_token(&lex);

    // TODO: support unsigned and signed
    static const char* types[] = {
        "int", "short", "long", "float", "double", "char"
    };

    static const FormatType ftypes[] = {
        DECIMAL, DECIMAL, LONGDECIMAL, DOUBLE, DOUBLE, CHAR
    };

    size_t i = 0;
    for (; i < ARRAY_LEN(types); ++i) {
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

bool parse_field(Struct* str) {
    FormatType ft = parse_type();
    if (ft == BOGUS) return false;

    if (lex.token != CLEX_id) return false;

    Field field = { .ft = ft, .field_name = strdup(lex.string) };
    da_append(&str->fields, field);

    stb_c_lexer_get_token(&lex);
    if (!expect_char(';')) return false;
    return true;
}

bool parse_struct(size_t pos_comment) {
    Struct str = {0};

    if (!expect_id_name("typedef")) return false;
    if (!expect_id_name("struct")) return false;
    if (!expect_char('{')) return false;

    while (!expect_char('}')) {
        if (!parse_field(&str)) return false;
    }

    if (lex.token != CLEX_id) return false;
    str.name = strdup(lex.string);
    stb_c_lexer_get_token(&lex);

    size_t pos = lex.where_firstchar - lex.input_stream + 1;
    if (!expect_char(';')) return false;

    str.pos_print = pos;
    str.pos_comment = pos_comment;
    da_append(&structs, str);
    return true;
}

String_Builder construct_debug_print(Struct str) {
    String_Builder sb = {0};

    static const char* formats[] = {
        NULL, "%d", "%ld", "%f", "%s", "%p", "%c"
    };
    
    // TODO: dont use nob as a dependecy for using this constructed function
    sb_appendf(&sb, "char* %s_debug_print(%s *str) {\n", str.name, str.name);
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
    if (src == NULL) return;
    assert(dest && pos < dest->count);

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

size_t insert_debug_print(String_Builder *file, const char* print, size_t where, size_t comment) {
    assert(print);
    assert(file);

    const char* pre = "// ";
    sb_insert_at(file, pre, comment);
    sb_insert_at(file, print, where + strlen(pre));

    return strlen(print) + strlen(pre);
}

void insert_debug_prints(String_Builder* file) {
    size_t offset = 0;
    String_Builder debug_print = {0};
    for (size_t i = 0; i < structs.count; ++i) {
        Struct str = structs.items[i];

        debug_print.count = 0;
        size_t where = str.pos_print + offset;
        size_t where_comment = str.pos_comment + offset;
        debug_print = construct_debug_print(str);

        offset += insert_debug_print(file, debug_print.items, where, where_comment);
    }

    sb_free(debug_print);
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

    size_t where_comment = 0;
    size_t scope_depth = 0;
    while (true) {
        if (lex.token == CLEX_parse_error) {
            fprintf(stderr, "\n<<<PARSE ERROR>>>\n");
            break;
        }

        bool debug = false;
        switch (lex.token) {
            case '{': ++scope_depth; break;
            case '}': --scope_depth; break;
            default: { 
                where_comment = lex.where_firstchar - lex.input_stream;
                if (scope_depth == 0 && expect_char('!') && expect_id_name("debug")) debug = true;
            }
        }

        if (debug) {
            if (!parse_struct(where_comment)) return false;
            debug = false;
        } else {
            if (!stb_c_lexer_get_token(&lex)) break;
        }
    }

    insert_debug_prints(&file);
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
    sb_free(sb);
    return true;
}

int main(int argc, char** argv) {
    assert(argc >= 2);

    // TODO: build a better lexer for this job instead of using stb_c_lexer
    if (!parse_files(argc, argv)) return 1;
    if (!compile_files(argc, argv)) return 2;

    return 0;
}
