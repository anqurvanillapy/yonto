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

static void
_on_signal(int sig)
{
    panic(strerror(sig));
}

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

enum object_type { OBJ_NUM = 1 };

struct object {
    enum object_type type;
    uint8_t marked;
    struct object *next;
};

void
object_init(struct object *o, enum object_type type, struct object *next)
{
    o->type = type;
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
gc_object_new(struct gc *vm, enum object_type type)
{
    if (vm->reachable == vm->max) {
        gc_run(vm);
    }
    struct object *o = new (struct object);
    object_init(o, type, vm->root);
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
source_back(struct source *s, struct loc loc)
{
    s->loc = loc;
    s->failed = 0;
    return s;
}

struct source *
parse_char(struct source *s, char c)
{
    char next = source_next(s);
    if (next != c) {
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

struct source *
word(void *w, struct source *s)
{
    const char *word = w;
    size_t i = 0;
    for (char c = word[i]; c != '\0'; i++, c = word[i]) {
        s = parse_char(s, c);
        if (s->failed) {
            break;
        }
    }
    return s;
}

struct lowercase {
    struct span span;
};

struct source *
lowercase(void *p, struct source *s)
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
        if (!(islower(c) && isalpha(c)) && c != '_') {
            break;
        }
        s = parse_char(s, c);
    }

    ((struct lowercase *)p)->span = (struct span){start, s->loc};
    return s;
}

struct parser {
    void *data;
    struct source *(*parse)(void *p, struct source *s);
};

struct all {
    struct parser *parsers[8];
    size_t size;
};

struct source *
all(void *p, struct source *s)
{
    struct all *a = p;
    for (size_t i = 0; i < a->size; i++) {
        struct parser *parser = a->parsers[i];
        s = parser->parse(parser->data, s);
        if (s->failed) {
            return s;
        }
        if (i + 1 < a->size) {
            s = skip_spaces(s);
        }
    }
    return s;
}

struct any {
    struct parser *parsers[16];
    size_t size;
};

struct source *
any(void *p, struct source *s)
{
    struct any *a = p;
    struct loc loc = s->loc;
    for (size_t i = 0; i < a->size; i++) {
        struct parser *parser = a->parsers[i];
        s = parser->parse(parser->data, s);
        if (!s->failed) {
            return s;
        }
        s = source_back(s, loc);
    }
    s->failed = 1;
    return s;
}

struct many {
    struct parser *parser;
};

struct source *
many(void *p, struct source *s)
{
    struct parser *parser = ((struct many *)p)->parser;
    while (1) {
        struct loc loc = s->loc;
        s = parser->parse(parser->data, s);
        if (s->failed) {
            return source_back(s, loc);
        }
        s = skip_spaces(s);
    }
}

// static struct parser _NEWLINE = {"\n", word};
static struct parser _FN = {"fn", word};
static struct parser _LPAREN = {"(", word};
static struct parser _RPAREN = {")", word};
static struct parser _COMMA = {",", word};
// static struct parser _ASSIGN = {"=", word};

struct param {
    struct node node;
    struct lowercase name;
};

void
param_init(struct param *p)
{
    node_init(&p->node);
}

void *
node_as_param(struct node *node)
{
    return (uint8_t *)node - offsetof(struct param, node);
}

struct fn_params {
    struct node *params;
};

void
fn_params_init(struct fn_params *f)
{
    f->params = NULL;
}

struct source *
parse_fn_param(void *p, struct source *s)
{
    struct lowercase name;
    s = lowercase(&name, s);
    if (s->failed) {
        return s;
    }
    struct param *param = new (struct param);
    param_init(param);
    param->name = name;
    param->node.key = _next_uid();
    struct fn_params *params = p;
    params->params = tree_insert(params->params, &param->node);
    return s;
}

struct source *
parse_fn_params(void *p, struct source *s)
{
    struct fn_params *parser = p;

    struct all all_no_params = {{&_LPAREN, &_RPAREN}, 2};
    struct parser no_params_parser = {&all_no_params, all};

    struct parser one_param_parser = {parser, parse_fn_param};
    struct all other_params = {{&_COMMA, &one_param_parser}, 2};
    struct parser other_params_parser = {&other_params, all};
    struct many many_other_params = {&other_params_parser};
    struct parser many_other_params_parser = {&many_other_params, many};
    struct all all_multi_params = {{&_LPAREN, &one_param_parser, &many_other_params_parser, &_RPAREN}, 4};
    struct parser multi_params_parser = {&all_multi_params, all};

    struct any branches = {{&no_params_parser, &multi_params_parser}, 2};
    return any(&branches, s);
}

struct fn {
    struct node node;
    struct lowercase name;
    struct fn_params params;
};

void
fn_init(struct fn *f)
{
    node_init(&f->node);
    fn_params_init(&f->params);
}

int
parse_fn(struct fn *fn, struct source *s)
{
    struct parser name = {&fn->name, lowercase};
    struct parser params = {&fn->params, parse_fn_params};
    struct all p = {{&_FN, &name, &params}, 3};
    return all(&p, s)->failed;
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
    printf("param: key=%d, pos=%lu\n", param->node.key, param->name.span.start.pos);
}

int
main(int argc, const char *argv[])
{
    signal(SIGSEGV, _on_signal);

    // Parsing some text.
    struct app app;
    if (app_init(&app, argc, argv) != 0) {
        printf("usage: joben FILE\n");
        return 1;
    }

    struct fn fn;
    fn_init(&fn);
    if (parse_fn(&fn, &app.src)) {
        printf("parse function error: pos=%lu\n", app.src.loc.pos);
        return 1;
    }
    tree_foreach(fn.params.params, iter_param);

    printf("name: start.col=%lu, end.col=%lu\n", fn.name.span.start.col, fn.name.span.end.col);
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
