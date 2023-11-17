#include "libgccjit.h"
#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef __GNUC__
#include <execinfo.h>
void print_stack(void) {
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
void print_stack(void) { printf("(unknown)\n"); }
#endif

void panic(const char *msg) {
  printf("panic: %s\n\n", msg);
  print_stack();
  exit(1);
}

void *allocate(size_t size) {
  void *ret = malloc(size);
  if (ret) {
    return ret;
  }
  panic("out of memory");
  return NULL;
}

void *allocate_zeroed(size_t size, size_t count) {
  void *ret = calloc(count, size);
  if (ret) {
    return ret;
  }
  panic("out of memory");
  return NULL;
}

void *reallocate(void *p, size_t size) {
  void *ret = realloc(p, size);
  if (ret) {
    return ret;
  }
  panic("out of memory");
  return NULL;
}

#define new(type) allocate(sizeof(type))
#define make(type, count) allocate_zeroed(sizeof(type), count)

static volatile int _NEXT_UID = 0;

static int _next_uid(void) {
  _NEXT_UID++;
  return _NEXT_UID;
}

static int _max(int a, int b) { return a > b ? a : b; }

struct node {
  struct node *left, *right;
  int key, height;
};

void node_default(struct node *node) {
  node->left = NULL;
  node->right = NULL;
  node->height = 1;
}

static int _node_height(struct node *node) { return !node ? 0 : node->height; }

static struct node *_node_right_rotate(struct node *x) {
  struct node *y = x->left;
  struct node *z = y->right;
  y->right = x;
  x->left = z;
  x->height = _max(_node_height(x->left), _node_height(x->right)) + 1;
  y->height = _max(_node_height(y->left), _node_height(y->right)) + 1;
  return y;
}

static struct node *_node_left_rotate(struct node *x) {
  struct node *y = x->right;
  struct node *z = y->left;
  y->left = x;
  x->right = z;
  x->height = _max(_node_height(x->left), _node_height(x->right)) + 1;
  y->height = _max(_node_height(y->left), _node_height(y->right)) + 1;
  return y;
}

static int _node_balance(struct node *node) {
  return !node ? 0 : _node_height(node->left) - _node_height(node->right);
}

struct node *tree_insert(struct node *root, struct node *other) {
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

void tree_iter(void *data, struct node *root,
               void (*f)(void *data, struct node *node)) {
  if (!root) {
    return;
  }
  tree_iter(data, root->left, f);
  f(data, root);
  tree_iter(data, root->right, f);
}

void tree_free(struct node *root, void *(*downcast)(struct node *node)) {
  if (!root) {
    return;
  }
  tree_free(root->left, downcast);
  tree_free(root->right, downcast);
  free(downcast(root));
}

struct slice {
  uint8_t *data;
  size_t elem_size, size, cap;
};

void slice_init(struct slice *s, size_t elem_size) {
  s->elem_size = elem_size;
  s->size = 0;
  s->cap = 4;
  s->data = allocate_zeroed(elem_size, s->cap);
}

void slice_append(struct slice *s, void *value) {
  if (s->size == s->cap) {
    s->cap *= 2;
    s->data = reallocate(s->data, s->cap * s->elem_size);
  }
  memcpy(s->data + s->size * s->elem_size, value, s->elem_size);
  s->size++;
}

void slice_free(struct slice *s) {
  free(s->data);
  s->size = 0;
  s->cap = 0;
}

struct entry {
  struct entry *next;
  const char *key;
  int val;
};

struct map {
  struct entry **buckets;
  size_t size, cap;
};

void map_default(struct map *m) {
  m->buckets = make(struct entry *, m->cap);
  m->size = 0;
  m->cap = 8;
}

static size_t _hash(const char *key, size_t cap) {
  size_t hash = 0;
  for (char c = *key; c != '\0'; c++) {
    hash = hash * 31 + (size_t)c;
  }
  return hash % cap;
}

static void _map_rehash(struct map *m) {
  size_t new_capacity = m->cap * 2;
  struct entry **new_buckets = make(struct entry *, new_capacity);

  for (size_t i = 0; i < m->cap; i++) {
    struct entry *e = m->buckets[i];
    while (e) {
      struct entry *next = e->next;
      size_t index = _hash(e->key, new_capacity);
      e->next = new_buckets[index];
      new_buckets[index] = e;
      e = next;
    }
  }

  free(m->buckets);
  m->buckets = new_buckets;
  m->cap = new_capacity;
}

bool map_set(struct map *m, const char *key, int val, int *old) {
  if ((double)m->size / (double)m->cap >= 1.0) {
    _map_rehash(m);
  }
  size_t index = _hash(key, m->cap);
  struct entry *e = m->buckets[index];
  while (e) {
    if (strcmp(e->key, key)) {
      if (old) {
        *old = e->val;
      }
      e->val = val;
      return true;
    }
    e = e->next;
  }
  e = new (struct entry);
  e->key = key;
  e->val = val;
  e->next = m->buckets[index];
  m->buckets[index] = e;
  m->size++;
  return false;
}

bool map_get(struct map *m, const char *key, int *val) {
  size_t index = _hash(key, m->cap);
  struct entry *e = m->buckets[index];
  while (e) {
    if (strcmp(e->key, key)) {
      *val = e->val;
      return true;
    }
    e = e->next;
  }
  return false;
}

void map_del(struct map *m, const char *key) {
  size_t index = _hash(key, m->cap);
  struct entry *e = m->buckets[index];
  struct entry *prev = NULL;
  while (e) {
    if (strcmp(e->key, key)) {
      if (prev) {
        prev->next = e->next;
      } else {
        m->buckets[index] = e->next;
      }
      free(e);
      m->size--;
      return;
    }
    prev = e;
    e = e->next;
  }
}

void map_free(struct map *m) {
  for (size_t i = 0; i < m->cap; i++) {
    struct entry *e = m->buckets[i];
    while (e) {
      struct entry *next = e->next;
      free(e);
      e = next;
    }
  }
  free(m->buckets);
}

enum object_kind { OBJ_NUM = 1 };

struct object {
  enum object_kind kind;
  uint8_t marked;
  struct object *next;
};

void object_init(struct object *o, enum object_kind kind, struct object *next) {
  o->kind = kind;
  o->marked = 0;
  o->next = next;
}

void object_mark(struct object *o) {
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

void gc_default(struct gc *c) {
  c->stack_size = 0;
  c->reachable = 0;
  c->max = 8;
  c->root = NULL;
}

void gc_push(struct gc *gc, struct object *value) {
  if (gc->stack_size >= sizeof(gc->stack) / sizeof(struct object *)) {
    printf("stack overflow\n");
    exit(1);
  }
  gc->stack[gc->stack_size] = value;
  gc->stack_size++;
}

struct object *gc_pop(struct gc *gc) {
  if (gc->stack_size <= 0) {
    printf("stack overflow\n");
    exit(1);
  }
  struct object *ret = gc->stack[gc->stack_size];
  gc->stack_size--;
  return ret;
}

void gc_mark(struct gc *vm) {
  for (size_t i = 0; i < vm->stack_size; i++) {
    object_mark(vm->stack[i]);
  }
}

void gc_sweep(struct gc *vm) {
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

void gc_run(struct gc *vm) {
  gc_mark(vm);
  gc_sweep(vm);
  vm->max = vm->reachable == 0 ? 8 : vm->reachable * 2;
}

struct object *gc_object_new(struct gc *vm, enum object_kind kind) {
  if (vm->reachable == vm->max) {
    gc_run(vm);
  }
  struct object *o = new (struct object);
  object_init(o, kind, vm->root);
  vm->root = o;
  vm->reachable++;
  return o;
}

void gc_close(struct gc *vm) {
  vm->stack_size = 0;
  gc_run(vm);
}

struct loc {
  size_t pos, ln, col;
};

void loc_default(struct loc *l) {
  l->pos = 0;
  l->ln = 1;
  l->col = 1;
}

void loc_next_line(struct loc *l) {
  l->pos++;
  l->ln++;
  l->col = 1;
}

void loc_next_column(struct loc *l) {
  l->pos++;
  l->col++;
}

struct span {
  struct loc start, end;
};

struct source {
  struct loc loc;
  FILE *f;
  bool failed, atom, newline_sensitive;
};

void source_init(struct source *s, FILE *f) {
  loc_default(&s->loc);
  s->f = f;
  s->failed = false;
  s->atom = false;
  s->newline_sensitive = false;
}

size_t source_size(struct source *s) {
  fseek(s->f, 0, SEEK_END);
  long size = ftell(s->f);
  fseek(s->f, (long)s->loc.pos, SEEK_SET);
  return (size_t)size;
}

const char *source_text(struct source *s, struct span span) {
  size_t size = span.end.pos - span.start.pos + 1;
  if (size == 0) {
    return NULL;
  }
  char *text = allocate(size);
  fseek(s->f, (long)span.start.pos, SEEK_SET);
  return fgets(text, (int)size, s->f);
}

char source_peek(struct source *s) {
  if (fseek(s->f, (long)s->loc.pos, SEEK_SET) != 0) {
    return -1;
  }
  int c = fgetc(s->f);
  if (c == EOF) {
    return -1;
  }
  return (char)c;
}

char source_next(struct source *s) {
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

struct source *source_back(struct source *s, struct loc loc) {
  s->loc = loc;
  s->failed = false;
  return s;
}

struct source *source_eat(struct source *s, char c) {
  if (source_next(s) != c) {
    s->failed = true;
  }
  return s;
}

struct source *skip_spaces(struct source *s) {
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
struct def;

struct range {
  char from, to;
};

union parser_ctx {
  const char *word;
  struct range range;
  struct parser *parser;
  struct parser **parsers;

  struct span *span;
  struct slice *slice;
  struct node **nodes;
  struct expr *expr;
  struct def *def;
};

struct parser {
  struct source *(*parse)(union parser_ctx *ctx, struct source *s);
  union parser_ctx ctx;
};

struct source *soi(union parser_ctx *ctx, struct source *s) {
  (void)ctx;
  if (s->loc.pos != 0) {
    s->failed = true;
  }
  return s;
}

struct source *eoi(union parser_ctx *ctx, struct source *s) {
  (void)ctx;
  if (s->loc.pos != source_size(s)) {
    s->failed = true;
  }
  return s;
}

static struct parser _SOI = {soi, {.word = NULL}};
static struct parser _EOI = {eoi, {.word = NULL}};

struct source *parse_atom(struct parser *parser, struct source *s) {
  int atom = s->atom;
  s->atom = true;
  s = parser->parse(&parser->ctx, s);
  s->atom = atom;
  return s;
}

struct source *word(union parser_ctx *ctx, struct source *s) {
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
static struct parser _ASSIGN = {word, {.word = "="}};
static struct parser _IF = {word, {.word = "if"}};
static struct parser _THEN = {word, {.word = "then"}};
static struct parser _ELSE = {word, {.word = "else"}};
static struct parser _ARROW = {word, {.word = "=>"}};
static struct parser _UNIT = {word, {.word = "()"}};
static struct parser _FALSE = {word, {.word = "false"}};
static struct parser _TRUE = {word, {.word = "true"}};

struct source *range(union parser_ctx *ctx, struct source *s) {
  char from = ctx->range.from, to = ctx->range.to;
  char c = source_peek(s);
  if (c < from || c > to) {
    s->failed = true;
    return s;
  }
  return source_eat(s, c);
}

// static struct parser _ASCII_BIN_DIGIT = {range, {.range = {'0', '1'}}};
// static struct parser _ASCII_OCT_DIGIT = {range, {.range = {'0', '7'}}};
static struct parser _ASCII_DIGIT = {range, {.range = {'0', '9'}}};
// static struct parser _ASCII_NONZERO_DIGIT = {range, {.range = {'1', '9'}}};

struct source *parse_lowercase(struct span *span, struct source *s) {
  struct loc start = s->loc;

  char first = source_peek(s);
  if (first < 0 || !islower(first) || !isalpha(first)) {
    s->failed = true;
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

  *span = (struct span){start, s->loc};
  return s;
}

struct source *lowercase(union parser_ctx *ctx, struct source *s) {
  return parse_lowercase(ctx->span, s);
}

struct source *parse_all(struct parser **parsers, struct source *s) {
  struct parser **parser = parsers;
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

struct source *all(union parser_ctx *ctx, struct source *s) {
  return parse_all(ctx->parsers, s);
}

struct source *parse_any(struct parser **parsers, struct source *s) {
  struct loc loc = s->loc;
  for (struct parser **parser = parsers; *parser; parser++) {
    s = (*parser)->parse(&(*parser)->ctx, s);
    if (!s->failed) {
      return s;
    }
    s = source_back(s, loc);
  }
  s->failed = true;
  return s;
}

struct source *any(union parser_ctx *ctx, struct source *s) {
  return parse_any(ctx->parsers, s);
}

static struct parser *_END_SYMBOLS[] = {&_SEMICOLON, &_NEWLINE, NULL};
static struct parser _END = {any, {.parsers = _END_SYMBOLS}};

struct source *parse_end(struct source *s) {
  int newline_sensitive = s->newline_sensitive;
  s->newline_sensitive = true;
  s = skip_spaces(s);
  s = _END.parse(&_END.ctx, s);
  s->newline_sensitive = newline_sensitive;
  return s;
}

struct source *many(union parser_ctx *ctx, struct source *s) {
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

struct source *option(union parser_ctx *ctx, struct source *s) {
  struct loc loc = s->loc;
  s = ctx->parser->parse(&ctx->parser->ctx, s);
  if (s->failed) {
    return source_back(s, loc);
  }
  return s;
}

enum expr_kind {
  EXPR_APP = 1,
  EXPR_ITE,
  EXPR_LAM,
  EXPR_NUM,
  EXPR_UNIT,
  EXPR_FALSE,
  EXPR_TRUE,
  EXPR_REF,
  EXPR_PAREN
};
union expr_data {
  struct app *app;
  struct ite *ite;
  struct lambda *lam;
  struct span span;
};
struct expr {
  enum expr_kind kind;
  union expr_data data;
};

struct source *parse_expr(struct expr *expr, struct source *s);

struct source *expr(union parser_ctx *ctx, struct source *s) {
  return parse_expr(ctx->expr, s);
}

struct app {
  struct expr f;
  struct slice args;
};

void app_default(struct app *a) { slice_init(&a->args, sizeof(struct expr)); }

struct source *arg(union parser_ctx *ctx, struct source *s) {
  struct expr a;
  s = parse_expr(&a, s);
  if (!s->failed) {
    slice_append(ctx->slice, &a);
  }
  return s;
}

struct source *args(union parser_ctx *ctx, struct source *s) {
  struct parser *no_args[] = {&_LPAREN, &_RPAREN, NULL};
  struct parser all_no_args = {all, {.parsers = no_args}};

  struct parser one_arg = {arg, {.slice = ctx->slice}};
  struct parser *other_args[] = {&_COMMA, &one_arg, NULL};
  struct parser all_other_args = {all, {.parsers = other_args}};
  struct parser many_other_args = {many, {.parser = &all_other_args}};
  struct parser *multi_args[] = {&_LPAREN, &one_arg, &many_other_args, &_RPAREN,
                                 NULL};
  struct parser all_multi_args = {all, {.parsers = multi_args}};

  struct parser *branches[] = {&all_no_args, &all_multi_args, NULL};
  return parse_any(branches, s);
}

static struct source *_expr_ref(union parser_ctx *ctx, struct source *s);
static struct source *_expr_paren(union parser_ctx *ctx, struct source *s);

static struct source *_expr_app(union parser_ctx *ctx, struct source *s) {
  struct app *app = new (struct app);
  app_default(app);

  struct parser f_ref = {_expr_ref, {.expr = &app->f}};
  struct parser f_expr = {_expr_paren, {.expr = &app->f}};
  struct parser *f_parsers[] = {&f_ref, &f_expr, NULL};
  struct parser f = {any, {.parsers = f_parsers}};

  struct parser x = {args, {.slice = &app->args}};

  struct parser *parsers[] = {&f, &x, NULL};
  s = parse_all(parsers, s);
  if (s->failed) {
    free(app);
    return s;
  }
  ctx->expr->data.app = app;
  ctx->expr->kind = EXPR_APP;
  return s;
}

struct ite {
  struct expr i, t, e;
};

static struct source *_expr_ite(union parser_ctx *ctx, struct source *s) {
  struct ite *ite = new (struct ite);
  struct parser i = {expr, {.expr = &ite->i}};
  struct parser t = {expr, {.expr = &ite->t}};
  struct parser e = {expr, {.expr = &ite->e}};
  struct parser *parsers[] = {&_IF, &i, &_THEN, &t, &_ELSE, &e, NULL};
  s = parse_all(parsers, s);
  if (s->failed) {
    free(ite);
    return s;
  }
  ctx->expr->kind = EXPR_ITE;
  ctx->expr->data.ite = ite;
  return s;
}

struct param {
  struct node as_node;

  struct span name;
};

struct source *param(union parser_ctx *ctx, struct source *s) {
  struct span name;
  s = parse_lowercase(&name, s);
  if (s->failed) {
    return s;
  }
  struct param *param = new (struct param);
  node_default(&param->as_node);
  param->name = name;
  param->as_node.key = _next_uid();
  *ctx->nodes = tree_insert(*ctx->nodes, &param->as_node);
  return s;
}

struct source *params(union parser_ctx *ctx, struct source *s) {
  struct parser *no_params[] = {&_LPAREN, &_RPAREN, NULL};
  struct parser all_no_params = {all, {.parsers = no_params}};

  struct parser one_param = {param, {.nodes = ctx->nodes}};
  struct parser *other_params[] = {&_COMMA, &one_param, NULL};
  struct parser all_other_params = {all, {.parsers = other_params}};
  struct parser many_other_params = {many, {.parser = &all_other_params}};
  struct parser *multi_params[] = {&_LPAREN, &one_param, &many_other_params,
                                   &_RPAREN, NULL};
  struct parser all_multi_params = {all, {.parsers = multi_params}};

  struct parser *branches[] = {&all_no_params, &all_multi_params, NULL};
  return parse_any(branches, s);
}

struct lambda {
  struct node *params;
  struct expr body;
};

void lambda_default(struct lambda *lam) { lam->params = NULL; }

static struct source *_expr_lambda(union parser_ctx *ctx, struct source *s) {
  struct lambda *lam = new (struct lambda);
  lambda_default(lam);
  struct parser ps = {params, {.nodes = &lam->params}};
  struct parser body = {expr, {.expr = &lam->body}};
  struct parser *parsers[] = {&ps, &_ARROW, &body, NULL};
  s = parse_all(parsers, s);
  if (s->failed) {
    free(lam);
    return s;
  }
  ctx->expr->kind = EXPR_LAM;
  ctx->expr->data.lam = lam;
  return s;
}

static struct source *_decimal_digits(union parser_ctx *ctx, struct source *s) {
  struct loc loc = s->loc;
  struct parser option_under = {option, {.parser = &_UNDER}};
  struct parser *other_digits[] = {&option_under, &_ASCII_DIGIT, NULL};
  struct parser all_other_digits = {all, {.parsers = other_digits}};
  struct parser many_other_digits = {many, {.parser = &all_other_digits}};
  struct parser *digits[] = {&_ASCII_DIGIT, &many_other_digits, NULL};
  s = parse_all(digits, s);
  if (!s->failed) {
    *ctx->span = (struct span){loc, s->loc};
  }
  return s;
}

static struct source *_decimal_number(union parser_ctx *ctx, struct source *s) {
  struct parser decimal_digits = {_decimal_digits, {.span = ctx->span}};
  return parse_atom(&decimal_digits, s);
}

struct source *number(union parser_ctx *ctx, struct source *s) {
  return _decimal_number(ctx, s);
}

static struct source *_expr_number(union parser_ctx *ctx, struct source *s) {
  struct span num;
  union parser_ctx num_ctx = {.span = &num};
  s = number(&num_ctx, s);
  if (!s->failed) {
    ctx->expr->kind = EXPR_NUM;
    ctx->expr->data.span = num;
  }
  return s;
}

static struct source *_expr_unit(union parser_ctx *ctx, struct source *s) {
  s = _UNIT.parse(&_UNIT.ctx, s);
  if (!s->failed) {
    ctx->expr->kind = EXPR_UNIT;
  }
  return s;
}

static struct source *_expr_false(union parser_ctx *ctx, struct source *s) {
  s = _FALSE.parse(&_FALSE.ctx, s);
  if (!s->failed) {
    ctx->expr->kind = EXPR_FALSE;
  }
  return s;
}

static struct source *_expr_true(union parser_ctx *ctx, struct source *s) {
  s = _TRUE.parse(&_TRUE.ctx, s);
  if (!s->failed) {
    ctx->expr->kind = EXPR_TRUE;
  }
  return s;
}

static struct source *_expr_ref(union parser_ctx *ctx, struct source *s) {
  struct span ref;
  s = parse_lowercase(&ref, s);
  if (!s->failed) {
    ctx->expr->kind = EXPR_REF;
    ctx->expr->data.span = ref;
  }
  return s;
}

static struct source *_expr_paren(union parser_ctx *ctx, struct source *s) {
  struct parser e = {expr, {.expr = ctx->expr}};
  struct parser *parsers[] = {&_LPAREN, &e, &_RPAREN, NULL};
  return parse_all(parsers, s);
}

struct source *parse_expr(struct expr *e, struct source *s) {
  struct parser expr_app = {_expr_app, {.expr = e}};
  struct parser expr_ite = {_expr_ite, {.expr = e}};
  struct parser expr_lam = {_expr_lambda, {.expr = e}};
  struct parser expr_num = {_expr_number, {.expr = e}};
  struct parser expr_unit = {_expr_unit, {.expr = e}};
  struct parser expr_false = {_expr_false, {.expr = e}};
  struct parser expr_true = {_expr_true, {.expr = e}};
  struct parser expr_ref = {_expr_ref, {.expr = e}};
  struct parser expr_paren = {_expr_paren, {.expr = e}};
  struct parser *branches[] = {&expr_app,   &expr_ite,   &expr_lam,  &expr_num,
                               &expr_unit,  &expr_false, &expr_true, &expr_ref,
                               &expr_paren, NULL};
  return parse_any(branches, s);
}

enum body_kind { BODY_FN = 1, BODY_VAL };
union body {
  struct expr ret;
};

struct def {
  struct node as_node;

  struct span name;
  struct node *params;
  enum body_kind kind;
  union body body;
};

struct source *fn(union parser_ctx *ctx, struct source *s) {
  struct parser name = {lowercase, {.span = &ctx->def->name}};
  struct parser ps = {params, {.nodes = &ctx->def->params}};
  struct parser ret = {expr, {.expr = &ctx->def->body.ret}};
  struct parser *parsers[] = {&name, &ps, &ret, NULL};
  s = parse_all(parsers, s);
  if (s->failed) {
    return s;
  }
  s = parse_end(s);
  if (!s->failed) {
    ctx->def->kind = BODY_FN;
  }
  return s;
}

struct source *val(union parser_ctx *ctx, struct source *s) {
  struct parser name = {lowercase, {.span = &ctx->def->name}};
  struct parser ret = {expr, {.expr = &ctx->def->body.ret}};
  struct parser *parsers[] = {&name, &_ASSIGN, &ret, NULL};
  s = parse_all(parsers, s);
  if (s->failed) {
    return s;
  }
  s = parse_end(s);
  if (!s->failed) {
    ctx->def->kind = BODY_VAL;
  }
  return s;
}

void def_default(struct def *d) {
  node_default(&d->as_node);
  d->params = NULL;
}

struct source *def(union parser_ctx *ctx, struct source *s) {
  struct def *d = new (struct def);
  def_default(d);

  struct parser def_fn = {fn, {.def = d}};
  struct parser def_val = {val, {.def = d}};

  struct parser *branches[] = {&def_fn, &def_val, NULL};
  s = parse_any(branches, s);
  if (s->failed) {
    free(d);
    return s;
  }
  d->as_node.key = _next_uid();
  *ctx->nodes = tree_insert(*ctx->nodes, &d->as_node);
  return s;
}

struct prog {
  struct node *defs;
};

void prog_default(struct prog *p) { p->defs = NULL; }

struct source *parse_prog(struct node **defs, struct source *s) {
  struct parser one_def = {def, {.nodes = defs}};
  struct parser many_defs = {many, {.parser = &one_def}};
  struct parser *parsers[] = {&_SOI, &many_defs, &_EOI, NULL};
  return parse_all(parsers, s);
}

struct resolver {
  struct source *s;
  struct map m;
  bool failed;
  struct span failed_name;
};

void resolver_init(struct resolver *r, struct source *s) {
  r->s = s;
  map_default(&r->m);
  r->failed = false;
}

// static void _resolve_param(void *data, struct node *node) {
//   struct resolver *r = data;
//   struct param *p = (struct param *)node;
// }

// static void _resolve_def(void *data, struct node *node) {
//   struct resolver *r = data;
//   struct def *d = (struct def *)node;
// }

struct interp {
  const char *filename;
  FILE *infile;
  struct source src;
};

int interp_init(struct interp *i, int argc, const char *argv[]) {
  if (argc < 2) {
    return -1;
  }
  i->filename = argv[1];
  i->infile = fopen(i->filename, "r");
  if (!i->infile) {
    perror("open file error");
    return -1;
  }
  source_init(&i->src, i->infile);
  return 0;
}

int interp_close(struct interp *i) {
  int ret = fclose(i->infile);
  if (ret != 0) {
    perror("close file error");
  }
  return ret;
}

static void _iter_param(void *data, struct node *node) {
  (void)data;
  struct param *param = (struct param *)node;
  printf("param: key=%d, pos=%lu\n", param->as_node.key, param->name.start.pos);
}

static void _iter_def(void *data, struct node *node) {
  struct def *d = (struct def *)node;
  printf("def: key=%d, pos=%lu, kind=%d, ret_kind=%d\n", d->as_node.key,
         d->name.start.pos, d->kind, d->body.ret.kind);
  tree_iter(data, d->params, _iter_param);
}

#if !__has_feature(address_sanitizer) && !__has_feature(thread_sanitizer) &&   \
    !__has_feature(memory_sanitizer)
static void _on_signal(int sig) { panic(strerror(sig)); }
static void _recovery(void) { signal(SIGSEGV, _on_signal); }
#else
static void _recovery(void) {}
#endif

int main(int argc, const char *argv[]) {
  _recovery();

  // Parsing some text.
  struct interp interp;
  if (interp_init(&interp, argc, argv) != 0) {
    printf("usage: oxn FILE\n");
    return 1;
  }

  struct prog program;
  prog_default(&program);
  struct source *s = parse_prog(&program.defs, &interp.src);
  if (s->failed) {
    printf("%s:%lu:%lu: parse error (pos=%lu)\n", interp.filename, s->loc.ln,
           s->loc.col, s->loc.pos);
    return 1;
  }
  tree_iter(NULL, program.defs, _iter_def);
  if (interp_close(&interp) != 0) {
    return 1;
  }

  // JIT example below.

  gcc_jit_context *ctx = gcc_jit_context_acquire();
  if (!ctx) {
    printf("acquire JIT context error\n");
    return 1;
  }

  gcc_jit_context_set_bool_option(ctx, GCC_JIT_BOOL_OPTION_DUMP_GENERATED_CODE,
                                  1);

  gcc_jit_type *void_type = gcc_jit_context_get_type(ctx, GCC_JIT_TYPE_VOID);
  gcc_jit_type *const_char_ptr_type =
      gcc_jit_context_get_type(ctx, GCC_JIT_TYPE_CONST_CHAR_PTR);
  gcc_jit_param *param_name =
      gcc_jit_context_new_param(ctx, NULL, const_char_ptr_type, "name");
  gcc_jit_function *func =
      gcc_jit_context_new_function(ctx, NULL, GCC_JIT_FUNCTION_EXPORTED,
                                   void_type, "say_hi", 1, &param_name, 0);
  gcc_jit_param *param_format =
      gcc_jit_context_new_param(ctx, NULL, const_char_ptr_type, "format");
  gcc_jit_function *printf_func = gcc_jit_context_new_function(
      ctx, NULL, GCC_JIT_FUNCTION_IMPORTED,
      gcc_jit_context_get_type(ctx, GCC_JIT_TYPE_INT), "printf", 1,
      &param_format, 1);

  gcc_jit_rvalue *args[2];
  args[0] = gcc_jit_context_new_string_literal(ctx, "Hello, %s!\n");
  args[1] = gcc_jit_param_as_rvalue(param_name);

  gcc_jit_block *block = gcc_jit_function_new_block(func, NULL);
  gcc_jit_block_add_eval(
      block, NULL, gcc_jit_context_new_call(ctx, NULL, printf_func, 2, args));
  gcc_jit_block_end_with_void_return(block, NULL);

  gcc_jit_result *ret = gcc_jit_context_compile(ctx);
  if (!ret) {
    printf("JIT compile error\n");
    return 1;
  }

  void (*say_hi)(const char *) =
      (void (*)(const char *))gcc_jit_result_get_code(ret, "say_hi");
  if (!say_hi) {
    printf("get code error\n");
    return 1;
  }

  say_hi("Oxn");

  gcc_jit_context_release(ctx);
  gcc_jit_result_release(ret);

  return 0;
}
