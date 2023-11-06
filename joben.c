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

struct parser {
    struct source *(*parse)(void *p, struct source *s);
};

struct word {
    struct parser parser;
    const char *word;
};

static struct source *
_parse_word(void *w, struct source *s)
{
    const char *word = ((struct word *)w)->word;
    size_t i = 0;
    for (char c = word[i]; c != '\0'; i++, c = word[i]) {
        s = parse_char(s, c);
        if (s->failed) {
            break;
        }
    }
    return s;
}

struct word
word(const char *word)
{
    struct word w;
    w.parser.parse = _parse_word;
    w.word = word;
    return w;
}

struct lowercase {
    struct parser parser;
    struct span span;
};

static struct source *
_parse_lowercase(void *p, struct source *s)
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

struct lowercase
lowercase(void)
{
    struct lowercase lc;
    lc.parser.parse = _parse_lowercase;
    return lc;
}

struct all {
    struct parser parser;
    struct parser *parsers[8];
    size_t size;
};

struct source *
_parse_all(void *p, struct source *s)
{
    struct all *a = p;
    for (size_t i = 0; i < a->size; i++) {
        struct parser *parser = a->parsers[i];
        s = parser->parse(parser, s);
        if (s->failed) {
            return s;
        }
        if (i + 1 < a->size) {
            s = skip_spaces(s);
        }
    }
    return s;
}

struct all
all(void)
{
    struct all a;
    a.parser.parse = _parse_all;
    a.size = 0;
    return a;
}

void
all_add(struct all *a, struct parser *parser)
{
    if (a->size >= sizeof(a->parsers) / sizeof(struct parser)) {
        panic("parsers out of bound");
    }
    a->parsers[a->size] = parser;
    a->size++;
}

struct any {
    struct parser parser;
    struct parser *parsers[16];
    size_t size;
};

static struct source *
_parse_any(void *p, struct source *s)
{
    struct any *a = p;
    struct loc loc = s->loc;
    for (size_t i = 0; i < a->size; i++) {
        struct parser *parser = a->parsers[i];
        s = parser->parse(parser, s);
        if (!s->failed) {
            return s;
        }
        s = source_back(s, loc);
    }
    s->failed = 1;
    return s;
}

struct any
any(void)
{
    struct any a;
    a.parser.parse = _parse_any;
    a.size = 0;
    return a;
}

void
any_add(struct any *a, struct parser *parser)
{
    if (a->size >= sizeof(a->parsers) / sizeof(struct parser)) {
        panic("parsers out of bound");
    }
    a->parsers[a->size] = parser;
    a->size++;
}

struct many {
    struct parser parser;
    struct parser *p;
};

static struct source *
_parse_many(void *p, struct source *s)
{
    struct parser *parser = ((struct many *)p)->p;
    while (1) {
        struct loc loc = s->loc;
        s = parser->parse(parser, s);
        if (s->failed) {
            return source_back(s, loc);
        }
        s = skip_spaces(s);
    }
}

struct many
many(struct parser *p)
{
    struct many m;
    m.parser.parse = _parse_many;
    m.p = p;
    return m;
}

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
    struct parser parser;
    struct node *params;
};

struct fn_param {
    struct parser parser;
    struct fn_params *params;
};

static struct source *
_parse_fn_param(void *p, struct source *s)
{
    struct lowercase name = lowercase();
    s = name.parser.parse(&name, s);
    if (s->failed) {
        return s;
    }
    struct param *param = new (struct param);
    param_init(param);
    param->name = name;
    param->node.key = _next_uid();
    struct fn_param *fn_param = p;
    fn_param->params->params = tree_insert(fn_param->params->params, &param->node);
    return s;
}

struct fn_param
fn_param(void *p)
{
    struct fn_param f;
    f.parser.parse = _parse_fn_param;
    f.params = (struct fn_params *)p;
    return f;
}

static struct source *
_parse_fn_params(void *p, struct source *s)
{
    struct word lparen = word("(");
    struct word rparen = word(")");
    struct word comma = word(",");

    struct all all_no_params = all();
    all_add(&all_no_params, &lparen.parser);
    all_add(&all_no_params, &rparen.parser);

    struct fn_param one_param = fn_param(p);

    struct all other_params = all();
    all_add(&other_params, &comma.parser);
    all_add(&other_params, &one_param.parser);
    struct many many_other_params = many(&other_params.parser);
    struct all all_multi_params = all();
    all_add(&all_multi_params, &lparen.parser);
    all_add(&all_multi_params, &one_param.parser);
    all_add(&all_multi_params, &many_other_params.parser);
    all_add(&all_multi_params, &rparen.parser);

    struct any branches = any();
    any_add(&branches, &all_no_params.parser);
    any_add(&branches, &all_multi_params.parser);
    return branches.parser.parse(&branches, s);
}

struct fn_params
fn_params(void)
{
    struct fn_params f;
    f.parser.parse = _parse_fn_params;
    f.params = NULL;
    return f;
}

struct fn {
    struct parser parser;
    struct lowercase name;
    struct fn_params params;
};

static struct source *
_parse_fn(void *p, struct source *s)
{
    struct fn *f = p;
    struct word fn_word = word("fn");
    struct word assign_word = word("=");
    struct all all_fn = all();
    all_add(&all_fn, &fn_word.parser);
    all_add(&all_fn, &f->name.parser);
    all_add(&all_fn, &f->params.parser);
    all_add(&all_fn, &assign_word.parser);
    return all_fn.parser.parse(&all_fn, s);
}

struct fn
fn(void)
{
    struct fn f;
    f.parser.parse = _parse_fn;
    f.name = lowercase();
    f.params = fn_params();
    return f;
}

enum def_kind { DEF_FN = 1 };

struct def {
    struct lowercase name;
    struct fn_params params;

    enum def_kind kind;
    union {
        struct fn fn;
    } body;
};

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

    struct fn f = fn();
    struct source *s = f.parser.parse(&f, &app.src);
    if (s->failed) {
        printf("parse function error: pos=%lu\n", s->loc.pos);
        return 1;
    }
    tree_foreach(f.params.params, iter_param);

    printf("name: start.col=%lu, end.col=%lu\n", f.name.span.start.col, f.name.span.end.col);
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
