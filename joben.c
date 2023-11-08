#include "libgccjit.h"
#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef __GNUC__
#include <execinfo.h>
void
print_stack(void)
{
    void *buf[10];
    int nframes = backtrace(buf, sizeof(buf) / sizeof(void *));
    char **symbols = backtrace_symbols(buf, nframes);

    printf("stacktrace:\n");
    for (int i = 0; i < nframes; ++i) {
        printf("\t%s\n", symbols[i]);
    }

    free(symbols);
}
#else
void
print_stack(void)
{
    printf("(unknown)\n");
}
#endif

void
panic(const char *msg)
{
    printf("panic: %s\n\n", msg);
    print_stack();
    exit(1);
}

void *
allocate(size_t size)
{
    void *ret = malloc(size);
    if (ret) {
        return ret;
    }
    panic("out of memory");
    return NULL;
}

#define new(type) allocate(sizeof(type))

static volatile int _NEXT_UID = 0;

static int
_next_uid(void)
{
    _NEXT_UID++;
    return _NEXT_UID;
}

static int
_max(int a, int b)
{
    return a > b ? a : b;
}

struct node {
    int key;
    struct node *left;
    struct node *right;
    int height;
};

void
node_init(struct node *node)
{
    node->left = NULL;
    node->right = NULL;
    node->height = 1;
}

static int
_node_height(struct node *node)
{
    return !node ? 0 : node->height;
}

static struct node *
_node_right_rotate(struct node *x)
{
    struct node *y = x->left;
    struct node *z = y->right;
    y->right = x;
    x->left = z;
    x->height = _max(_node_height(x->left), _node_height(x->right)) + 1;
    y->height = _max(_node_height(y->left), _node_height(y->right)) + 1;
    return y;
}

static struct node *
_node_left_rotate(struct node *x)
{
    struct node *y = x->right;
    struct node *z = y->left;
    y->left = x;
    x->right = z;
    x->height = _max(_node_height(x->left), _node_height(x->right)) + 1;
    y->height = _max(_node_height(y->left), _node_height(y->right)) + 1;
    return y;
}

static int
_node_balance(struct node *node)
{
    return !node ? 0 : _node_height(node->left) - _node_height(node->right);
}

struct node *
tree_insert(struct node *root, struct node *other)
{
    if (!root) {
        return other;
    }

    if (other->key < root->key) {
        root->left = tree_insert(root->left, other);
    } else if (other->key > root->key) {
        root->right = tree_insert(root->right, other);
    } else {
        return root;
    }

    root->height = _max(_node_height(root->left), _node_height(root->right)) + 1;

    int balance = _node_balance(root);
    if (balance > 1 && other->key < root->left->key) {
        return _node_right_rotate(root);
    }
    if (balance < -1 && other->key > root->right->key) {
        return _node_left_rotate(root);
    }
    if (balance > 1 && other->key > root->left->key) {
        root->left = _node_left_rotate(root->left);
        return _node_right_rotate(root);
    }
    if (balance < -1 && other->key < root->right->key) {
        root->right = _node_right_rotate(root->right);
        return _node_left_rotate(root);
    }

    return root;
}

void
tree_foreach(struct node *root, void (*f)(struct node *node))
{
    if (!root) {
        return;
    }
    tree_foreach(root->left, f);
    f(root);
    tree_foreach(root->right, f);
}

void
tree_free(struct node *root, void *(*downcast)(struct node *node))
{
    if (!root) {
        return;
    }
    tree_free(root->left, downcast);
    tree_free(root->right, downcast);
    free(downcast(root));
}

enum object_kind { OBJ_NUM = 1 };

struct object {
    enum object_kind kind;
    uint8_t marked;
    struct object *next;
};

void
object_init(struct object *o, enum object_kind kind, struct object *next)
{
    o->kind = kind;
    o->marked = 0;
    o->next = next;
}

void
object_mark(struct object *o)
{
    if (o->marked) {
        return;
    }
    o->marked = 1;
    // TODO: Mark other objects from the members.
}

struct gc {
    struct object *stack[256];
    size_t stack_size, reachable, max;
    struct object *root;
};

void
gc_init(struct gc *c)
{
    c->stack_size = 0;
    c->reachable = 0;
    c->max = 8;
    c->root = NULL;
}

void
gc_push(struct gc *gc, struct object *value)
{
    if (gc->stack_size >= sizeof(gc->stack) / sizeof(struct object *)) {
        printf("stack overflow\n");
        exit(1);
    }
    gc->stack[gc->stack_size] = value;
    gc->stack_size++;
}

struct object *
gc_pop(struct gc *gc)
{
    if (gc->stack_size <= 0) {
        printf("stack overflow\n");
        exit(1);
    }
    struct object *ret = gc->stack[gc->stack_size];
    gc->stack_size--;
    return ret;
}

void
gc_mark(struct gc *vm)
{
    for (size_t i = 0; i < vm->stack_size; i++) {
        object_mark(vm->stack[i]);
    }
}

void
gc_sweep(struct gc *vm)
{
    struct object **object = &vm->root;
    while (*object) {
        if ((*object)->marked) {
            (*object)->marked = 0;
            object = &(*object)->next;
            continue;
        }
        struct object *unreached = *object;
        *object = unreached->next;
        free(unreached);
        vm->reachable--;
    }
}

void
gc_run(struct gc *vm)
{
    gc_mark(vm);
    gc_sweep(vm);
    vm->max = vm->reachable == 0 ? 8 : vm->reachable * 2;
}

struct object *
gc_object_new(struct gc *vm, enum object_kind kind)
{
    if (vm->reachable == vm->max) {
        gc_run(vm);
    }
    struct object *o = new (struct object);
    object_init(o, kind, vm->root);
    vm->root = o;
    vm->reachable++;
    return o;
}

void
gc_close(struct gc *vm)
{
    vm->stack_size = 0;
    gc_run(vm);
}

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
    int failed, atom, newline_sensitive;
};

void
source_init(struct source *s, FILE *f)
{
    loc_init(&s->loc);
    s->f = f;
    s->failed = 0;
    s->atom = 0;
    s->newline_sensitive = 0;
}

size_t
source_size(struct source *s)
{
    fseek(s->f, 0, SEEK_END);
    long size = ftell(s->f);
    fseek(s->f, (long)s->loc.pos, SEEK_SET);
    return (size_t)size;
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
source_back(struct source *s, struct loc loc)
{
    s->loc = loc;
    s->failed = 0;
    return s;
}

struct source *
source_eat(struct source *s, char c)
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
        if (c < 0 || (s->newline_sensitive && c == '\n') || !isspace(c)) {
            break;
        }
        s = source_eat(s, c);
    }
    return s;
}

struct parser;
struct expr;
struct fn;
struct prog;

struct range {
    char from, to;
};

union parser_ctx {
    const char *word;
    struct range range;
    struct parser *parser;
    struct parser **parsers;

    struct span *span;
    struct node **nodes;
    struct expr *expr;
    struct fn *fn;
    struct prog *prog;
};

struct parser {
    struct source *(*parse)(union parser_ctx *ctx, struct source *s);
    union parser_ctx ctx;
};

struct source *
soi(union parser_ctx *ctx, struct source *s)
{
    (void)ctx;
    if (s->loc.pos != 0) {
        s->failed = 1;
    }
    return s;
}

struct source *
eoi(union parser_ctx *ctx, struct source *s)
{
    (void)ctx;
    if (s->loc.pos != source_size(s)) {
        s->failed = 1;
    }
    return s;
}

static struct parser _SOI = {soi, {.word = NULL}};
static struct parser _EOI = {eoi, {.word = NULL}};

struct source *
atom(union parser_ctx *ctx, struct source *s)
{
    int atom = s->atom;
    s->atom = 1;
    s = ctx->parser->parse(&ctx->parser->ctx, s);
    s->atom = atom;
    return s;
}

struct source *
newline_sensitive(union parser_ctx *ctx, struct source *s)
{
    int newline_sensitive = s->newline_sensitive;
    s->newline_sensitive = 1;
    s = ctx->parser->parse(&ctx->parser->ctx, s);
    s->newline_sensitive = newline_sensitive;
    return s;
}

struct source *
word(union parser_ctx *ctx, struct source *s)
{
    const char *word = ctx->word;
    size_t i = 0;
    for (char c = word[i]; c != '\0'; i++, c = word[i]) {
        s = source_eat(s, c);
        if (s->failed) {
            break;
        }
    }
    return s;
}

static struct parser _LPAREN = {word, {.word = "("}};
static struct parser _RPAREN = {word, {.word = ")"}};
static struct parser _COMMA = {word, {.word = ","}};
static struct parser _UNDER = {word, {.word = "_"}};
static struct parser _NEWLINE = {word, {.word = "\n"}};
static struct parser _SEMICOLON = {word, {.word = ";"}};
static struct parser _FN = {word, {.word = "fn"}};
static struct parser _RETURN = {word, {.word = "return"}};

struct source *
range(union parser_ctx *ctx, struct source *s)
{
    char from = ctx->range.from, to = ctx->range.to;
    char c = source_peek(s);
    if (c < from || c > to) {
        s->failed = 1;
        return s;
    }
    return source_eat(s, c);
}

// static struct parser _ASCII_BIN_DIGIT = {range, {.range = {'0', '1'}}};
// static struct parser _ASCII_OCT_DIGIT = {range, {.range = {'0', '7'}}};
static struct parser _ASCII_DIGIT = {range, {.range = {'0', '9'}}};
// static struct parser _ASCII_NONZERO_DIGIT = {range, {.range = {'1', '9'}}};

struct source *
lowercase(union parser_ctx *ctx, struct source *s)
{
    struct loc start = s->loc;

    char first = source_peek(s);
    if (first < 0 || !islower(first) || !isalpha(first)) {
        s->failed = 1;
        return s;
    }
    s = source_eat(s, first);

    while (1) {
        char c = source_peek(s);
        if (!(islower(c) && isalpha(c)) && c != '_') {
            break;
        }
        s = source_eat(s, c);
    }

    *ctx->span = (struct span){start, s->loc};
    return s;
}

struct source *
all(union parser_ctx *ctx, struct source *s)
{
    struct parser **parser = ctx->parsers;
    while (*parser) {
        s = (*parser)->parse(&(*parser)->ctx, s);
        if (s->failed) {
            return s;
        }
        parser++;
        if (!s->atom && *parser) {
            s = skip_spaces(s);
        }
    }
    return s;
}

struct source *
any(union parser_ctx *ctx, struct source *s)
{
    struct loc loc = s->loc;
    for (struct parser **parser = ctx->parsers; *parser; parser++) {
        s = (*parser)->parse(&(*parser)->ctx, s);
        if (!s->failed) {
            return s;
        }
        s = source_back(s, loc);
    }
    s->failed = 1;
    return s;
}

static struct parser *_END_SYMBOLS[] = {&_SEMICOLON, &_NEWLINE, NULL};
static struct parser _END = {any, {.parsers = _END_SYMBOLS}};

struct source *
many(union parser_ctx *ctx, struct source *s)
{
    while (1) {
        struct loc loc = s->loc;
        s = ctx->parser->parse(&ctx->parser->ctx, s);
        if (s->failed) {
            return source_back(s, loc);
        }
        if (!s->atom) {
            s = skip_spaces(s);
        }
    }
}

struct source *
option(union parser_ctx *ctx, struct source *s)
{
    struct loc loc = s->loc;
    s = ctx->parser->parse(&ctx->parser->ctx, s);
    if (s->failed) {
        return source_back(s, loc);
    }
    return s;
}

static struct source *
_decimal_digits(union parser_ctx *ctx, struct source *s)
{
    struct loc loc = s->loc;
    struct parser option_under = {option, {.parser = &_UNDER}};
    struct parser *other_digits[] = {&option_under, &_ASCII_DIGIT, NULL};
    struct parser all_other_digits = {all, {.parsers = other_digits}};
    struct parser many_other_digits = {many, {.parser = &all_other_digits}};
    struct parser *digits[] = {&_ASCII_DIGIT, &many_other_digits, NULL};
    union parser_ctx all_digits = {.parsers = digits};
    s = all(&all_digits, s);
    if (!s->failed) {
        *ctx->span = (struct span){loc, s->loc};
    }
    return s;
}

static struct source *
_decimal_number(union parser_ctx *ctx, struct source *s)
{
    struct parser decimal_digits = {_decimal_digits, {.span = ctx->span}};
    union parser_ctx atom_decimal_digits = {.parser = &decimal_digits};
    return atom(&atom_decimal_digits, s);
}

struct source *
number(union parser_ctx *ctx, struct source *s)
{
    return _decimal_number(ctx, s);
}

enum expr_kind { EXPR_NUM };
union expr_data {
    struct span num;
};
struct expr {
    enum expr_kind kind;
    union expr_data data;
};

static struct source *
_number_expr(union parser_ctx *ctx, struct source *s)
{
    struct span num;
    union parser_ctx num_ctx = {.span = &num};
    s = number(&num_ctx, s);
    if (!s->failed) {
        ctx->expr->kind = EXPR_NUM;
        ctx->expr->data.num = num;
    }
    return s;
}

struct source *
expr(union parser_ctx *ctx, struct source *s)
{
    struct parser num = {_number_expr, {.expr = ctx->expr}};
    struct parser *branches[] = {&num, NULL};
    union parser_ctx any_branches = {.parsers = branches};
    return any(&any_branches, s);
}

struct param {
    struct node node;
    struct span name;
};

struct source *
fn_param(union parser_ctx *ctx, struct source *s)
{
    struct span name;
    union parser_ctx name_parser = {.span = &name};
    s = lowercase(&name_parser, s);
    if (s->failed) {
        return s;
    }
    struct param *param = new (struct param);
    node_init(&param->node);
    param->name = name;
    param->node.key = _next_uid();
    *ctx->nodes = tree_insert(*ctx->nodes, &param->node);
    return s;
}

struct source *
fn_params(union parser_ctx *ctx, struct source *s)
{
    struct parser *no_params[] = {&_LPAREN, &_RPAREN, NULL};
    struct parser all_no_params = {all, {.parsers = no_params}};

    struct parser one_param = {fn_param, {.nodes = ctx->nodes}};
    struct parser *other_params[] = {&_COMMA, &one_param, NULL};
    struct parser all_other_params = {all, {.parsers = other_params}};
    struct parser many_other_params = {many, {.parser = &all_other_params}};
    struct parser *multi_params[] = {&_LPAREN, &one_param, &many_other_params, &_RPAREN, NULL};
    struct parser all_multi_params = {all, {.parsers = multi_params}};

    struct parser *branches[] = {&all_no_params, &all_multi_params, NULL};
    union parser_ctx any_branches = {.parsers = branches};
    return any(&any_branches, s);
}

struct fn {
    struct span name;
    struct node *params;
    struct expr ret;
};

struct source *
fn(union parser_ctx *ctx, struct source *s)
{
    struct parser name = {lowercase, {.span = &ctx->fn->name}};
    struct parser params = {fn_params, {.nodes = &ctx->fn->params}};
    struct parser ret = {expr, {.expr = &ctx->fn->ret}};
    struct parser *ret_end[] = {&ret, &_END, NULL};
    struct parser all_ret_end = {all, {.parsers = ret_end}};
    struct parser sensitive_all_ret_end = {newline_sensitive, {.parser = &all_ret_end}};
    struct parser *parsers[] = {&_FN, &name, &params, &_RETURN, &sensitive_all_ret_end, NULL};
    union parser_ctx all_fn = {.parsers = parsers};
    return all(&all_fn, s);
}

struct prog {
    struct fn fn;
};

struct source *
prog(union parser_ctx *ctx, struct source *s)
{
    struct parser fn_parser = {fn, {.fn = ctx->fn}};
    // TODO: Many function definitions.
    struct parser *parsers[] = {&_SOI, &fn_parser, &_EOI, NULL};
    union parser_ctx all_parsers = {.parsers = parsers};
    return all(&all_parsers, s);
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

static void
iter_param(struct node *node)
{
    struct param *param = (struct param *)node;
    printf("param: key=%d, pos=%lu\n", param->node.key, param->name.start.pos);
}

#if !__has_feature(address_sanitizer) && !__has_feature(thread_sanitizer) && !__has_feature(memory_sanitizer)
static void
_on_signal(int sig)
{
    panic(strerror(sig));
}

static void
_recovery(void)
{
    signal(SIGSEGV, _on_signal);
}
#else
static void
_recovery(void)
{
}
#endif

int
main(int argc, const char *argv[])
{
    _recovery();

    // Parsing some text.
    struct app app;
    if (app_init(&app, argc, argv) != 0) {
        printf("usage: joben FILE\n");
        return 1;
    }

    struct prog program;
    program.fn.params = NULL; // FIXME: better initialization here?
    union parser_ctx prog_parser = {.fn = &program.fn};
    struct source *s = prog(&prog_parser, &app.src);
    if (s->failed) {
        printf("parse error: pos=%lu\n", s->loc.pos);
        return 1;
    }
    tree_foreach(program.fn.params, iter_param);

    printf("name start col: %lu, end col: %lu\n", program.fn.name.start.col, program.fn.name.end.col);
    printf("ret start col: %lu, end col: %lu\n", program.fn.ret.data.num.start.col, program.fn.ret.data.num.end.col);
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
