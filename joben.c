#include "libgccjit.h"
#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

struct loc {
    size_t pos, ln, col;
};

void
loc_init(struct loc *l)
{
    l->pos = 0;
    l->ln = 1;
    l->col = 1;
}

void
loc_next_line(struct loc *l)
{
    l->pos++;
    l->ln++;
    l->col = 1;
}

void
loc_next_column(struct loc *l)
{
    l->pos++;
    l->col++;
}

struct span {
    struct loc start, end;
};

struct source {
    struct loc loc;
    FILE *f;
    int failed;
};

void
source_init(struct source *s, FILE *f)
{
    loc_init(&s->loc);
    s->f = f;
    s->failed = 0;
}

char
source_peek(struct source *s)
{
    if (fseek(s->f, (long)s->loc.pos, SEEK_SET) != 0) {
        return -1;
    }
    int c = fgetc(s->f);
    if (c == EOF) {
        return -1;
    }
    return (char)c;
}

char
source_next(struct source *s)
{
    char next = source_peek(s);
    if (next < 0) {
        return -1;
    }
    if (next == '\n') {
        loc_next_line(&s->loc);
        return next;
    }
    loc_next_column(&s->loc);
    return next;
}

struct source *
parse_char(struct source *s, char c)
{
    if (source_next(s) != c) {
        s->failed = 1;
    }
    return s;
}

struct source *
skip_spaces(struct source *s)
{
    while (1) {
        char c = source_peek(s);
        if (c < 0 || !isspace(c)) {
            break;
        }
        s = parse_char(s, c);
    }
    return s;
}

struct word_parser {
    const char *w;
};

struct source *
parse_word(void *p, struct source *s)
{
    const char *word = ((struct word_parser *)p)->w;
    size_t i = 0;
    for (char c = word[i]; c != '\0'; i++, c = word[i]) {
        s = parse_char(s, c);
        if (s->failed) {
            break;
        }
    }
    return s;
}

struct lowercase_parser {
    struct span span;
};

struct source *
parse_lowercase(void *p, struct source *s)
{
    struct loc start = s->loc;

    char first = source_peek(s);
    if (first < 0 || !islower(first) || !isalpha(first)) {
        s->failed = 1;
        return s;
    }
    s = parse_char(s, first);

    while (1) {
        char c = source_peek(s);
        if ((!islower(c) || !isalpha(c)) && c != '_') {
            break;
        }
        s = parse_char(s, c);
    }

    ((struct lowercase_parser *)p)->span = (struct span){start, s->loc};
    return s;
}

struct parser {
    void *data;
    struct source *(*parse)(void *p, struct source *s);
};

struct seq_parser {
    struct parser *parsers[10];
    size_t size;
};

struct source *
parse_seq(void *p, struct source *s)
{
    struct seq_parser *seq = p;
    for (size_t i = 0; i < seq->size; i++) {
        struct parser *parser = seq->parsers[i];
        s = parser->parse(parser->data, s);
        if (s->failed) {
            return s;
        }
        if (i + 1 < seq->size) {
            s = skip_spaces(s);
        }
    }
    return s;
}

struct app {
    const char *filename;
    FILE *infile;
    struct source src;
};

int
app_init(struct app *app, int argc, const char *argv[])
{
    if (argc < 2) {
        return -1;
    }
    app->filename = argv[1];
    app->infile = fopen(app->filename, "r");
    if (!app->infile) {
        perror("open file error");
        return -1;
    }
    source_init(&app->src, app->infile);
    return 0;
}

int
app_close(struct app *app)
{
    int ret = fclose(app->infile);
    if (ret != 0) {
        perror("close file error");
    }
    return ret;
}

int
main(int argc, const char *argv[])
{
    // Parsing some text.
    struct app app;
    if (app_init(&app, argc, argv) != 0) {
        printf("usage: joben FILE\n");
        return 1;
    }

    struct source *s = &app.src;

    struct word_parser fn = {"fn"};
    struct parser fn_parser = {&fn, parse_word};
    struct lowercase_parser name;
    struct parser name_parser = {&name, parse_lowercase};
    struct seq_parser seq = {{&fn_parser, &name_parser}, 2};
    s = parse_seq(&seq, s);
    if (s->failed) {
        printf("parse seq error: pos=%lu\n", s->loc.pos);
        return 1;
    }
    printf("name: start.col=%lu, end.col=%lu\n", name.span.start.col, name.span.end.col);
    if (app_close(&app) != 0) {
        return 1;
    }

    // JIT example below.

    gcc_jit_context *ctx = gcc_jit_context_acquire();
    if (!ctx) {
        printf("acquire JIT context error\n");
        return 1;
    }

    gcc_jit_context_set_bool_option(ctx, GCC_JIT_BOOL_OPTION_DUMP_GENERATED_CODE, 1);

    gcc_jit_type *void_type = gcc_jit_context_get_type(ctx, GCC_JIT_TYPE_VOID);
    gcc_jit_type *const_char_ptr_type = gcc_jit_context_get_type(ctx, GCC_JIT_TYPE_CONST_CHAR_PTR);
    gcc_jit_param *param_name = gcc_jit_context_new_param(ctx, NULL, const_char_ptr_type, "name");
    gcc_jit_function *func =
        gcc_jit_context_new_function(ctx, NULL, GCC_JIT_FUNCTION_EXPORTED, void_type, "say_hi", 1, &param_name, 0);
    gcc_jit_param *param_format = gcc_jit_context_new_param(ctx, NULL, const_char_ptr_type, "format");
    gcc_jit_function *printf_func =
        gcc_jit_context_new_function(ctx, NULL, GCC_JIT_FUNCTION_IMPORTED,
                                     gcc_jit_context_get_type(ctx, GCC_JIT_TYPE_INT), "printf", 1, &param_format, 1);

    gcc_jit_rvalue *args[2];
    args[0] = gcc_jit_context_new_string_literal(ctx, "Hello, %s!\n");
    args[1] = gcc_jit_param_as_rvalue(param_name);

    gcc_jit_block *block = gcc_jit_function_new_block(func, NULL);
    gcc_jit_block_add_eval(block, NULL, gcc_jit_context_new_call(ctx, NULL, printf_func, 2, args));
    gcc_jit_block_end_with_void_return(block, NULL);

    gcc_jit_result *ret = gcc_jit_context_compile(ctx);
    if (!ret) {
        printf("JIT compile error\n");
        return 1;
    }

    void (*say_hi)(const char *) = (void (*)(const char *))gcc_jit_result_get_code(ret, "say_hi");
    if (!say_hi) {
        printf("get code error\n");
        return 1;
    }

    say_hi("Joben");

    gcc_jit_context_release(ctx);
    gcc_jit_result_release(ret);

    return 0;
}
