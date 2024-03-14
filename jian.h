#pragma once

#include <libgccjit++.h>

/*
#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JIAN_VERSION_MAJOR 0
#define JIAN_VERSION_MINOR 1
#define JIAN_VERSION_PATCH 0

#ifdef __GNUC__
#include <execinfo.h>
inline static void printStack(void) {
  void *buf[10];
  int frameNum = backtrace(buf, sizeof(buf) / sizeof(void *));
  char **symbols = backtrace_symbols(buf, frameNum);

  printf("stacktrace:\n");
  for (int i = 0; i < frameNum; ++i) {
    printf("\t%s\n", symbols[i]);
  }

  free(symbols);
}
#else
inline static void printStack(void) { printf("(unknown)\n"); }
#endif

inline static void panic(const char *msg) {
  printf("panic: %s\n\n", msg);
  printStack();
  exit(1);
}

inline static void unreachable(void) { panic("unreachable"); }

inline static void *allocate(size_t size) {
  void *ret = malloc(size);
  if (ret) {
    return ret;
  }
  panic("out of memory");
  return NULL;
}

inline static void *allocateZeroed(size_t size, size_t count) {
  void *ret = calloc(count, size);
  if (ret) {
    return ret;
  }
  panic("out of memory");
  return NULL;
}

inline static void *reallocate(void *p, size_t size) {
  void *ret = realloc(p, size);
  if (ret) {
    return ret;
  }
  panic("out of memory");
  return NULL;
}

#define new(type) allocate(sizeof(type))
#define make(type, count) allocateZeroed(sizeof(type), count)

inline static int max(int a, int b) { return a > b ? a : b; }

struct IDs {
  volatile int next;
};

inline static void IDs_Default(struct IDs *g) { g->next = 0; }

inline static int IDs_New(struct IDs *g) {
  g->next++;
  return g->next;
}

struct node {
  struct node *Left, *Right;
  int Key, Height;
};

inline static void node_Default(struct node *node) {
  node->Left = NULL;
  node->Right = NULL;
  node->Height = 1;
}

inline static int node_height(struct node *node) { return !node ? 0 :
node->Height; }

inline static struct node *node_rightRotate(struct node *x) {
  struct node *y = x->Left;
  struct node *z = y->Right;
  y->Right = x;
  x->Left = z;
  x->Height = max(node_height(x->Left), node_height(x->Right)) + 1;
  y->Height = max(node_height(y->Left), node_height(y->Right)) + 1;
  return y;
}

inline static struct node *node_leftRotate(struct node *x) {
  struct node *y = x->Right;
  struct node *z = y->Left;
  y->Left = x;
  x->Right = z;
  x->Height = max(node_height(x->Left), node_height(x->Right)) + 1;
  y->Height = max(node_height(y->Left), node_height(y->Right)) + 1;
  return y;
}

inline static int node_balance(struct node *node) {
  return !node ? 0 : node_height(node->Left) - node_height(node->Right);
}

inline static struct node *tree_Insert(struct node *root, struct node *other) {
  if (!root) {
    return other;
  }

  if (other->Key < root->Key) {
    root->Left = tree_Insert(root->Left, other);
  } else if (other->Key > root->Key) {
    root->Right = tree_Insert(root->Right, other);
  } else {
    return root;
  }

  root->Height = max(node_height(root->Left), node_height(root->Right)) + 1;

  int balance = node_balance(root);
  if (balance > 1 && other->Key < root->Left->Key) {
    return node_rightRotate(root);
  }
  if (balance < -1 && other->Key > root->Right->Key) {
    return node_leftRotate(root);
  }
  if (balance > 1 && other->Key > root->Left->Key) {
    root->Left = node_leftRotate(root->Left);
    return node_rightRotate(root);
  }
  if (balance < -1 && other->Key < root->Right->Key) {
    root->Right = node_rightRotate(root->Right);
    return node_leftRotate(root);
  }

  return root;
}

inline static void tree_Iter(void *data, struct node *root,
                      void (*f)(void *data, struct node *node)) {
  if (!root) {
    return;
  }
  tree_Iter(data, root->Left, f);
  f(data, root);
  tree_Iter(data, root->Right, f);
}

inline static void tree_Free(struct node *root,
                          void *(*downcast)(struct node *node)) {
  if (!root) {
    return;
  }
  tree_Free(root->Left, downcast);
  tree_Free(root->Right, downcast);
  free(downcast(root));
}

struct slice {
  uint8_t *Data;
  size_t ElemSize, Size, Cap;
};

inline static void slice_Init(struct slice *s, size_t elemSize) {
  s->ElemSize = elemSize;
  s->Size = 0;
  s->Cap = 4;
  s->Data = allocateZeroed(elemSize, s->Cap);
}

inline static void slice_Append(struct slice *s, void *value) {
  if (s->Size == s->Cap) {
    s->Cap *= 2;
    s->Data = reallocate(s->Data, s->Cap * s->ElemSize);
  }
  memcpy(s->Data + s->Size * s->ElemSize, value, s->ElemSize);
  s->Size++;
}

inline static void slice_Iter(void *data, struct slice *s,
                       void (*f)(void *data, void *elem)) {
  uint8_t *elem = s->Data;
  for (size_t i = 0; i < s->Size; i++, elem += s->ElemSize) {
    f(data, elem);
  }
}

inline static void slice_Free(struct slice *s) {
  free(s->Data);
  s->Data = NULL;
  s->Size = 0;
  s->Cap = 0;
}

struct entry {
  struct entry *Next;
  const char *Key;
  int Val;
};

struct map {
  struct entry **Buckets;
  size_t Size, Cap;
};

inline static void map_Default(struct map *m) {
  m->Size = 0;
  m->Cap = 8;
  m->Buckets = make(struct entry *, m->Cap);
}

inline static size_t map_hash(const char *key, size_t cap) {
  size_t h = 0;
  for (char c = *key; c != '\0'; c++) {
    h = h * 31 + (size_t)c;
  }
  return h % cap;
}

inline static void map_rehash(struct map *m) {
  size_t newCap = m->Cap * 2;
  struct entry **newBuckets = make(struct entry *, newCap);

  for (size_t i = 0; i < m->Cap; i++) {
    struct entry *e = m->Buckets[i];
    while (e) {
      struct entry *next = e->Next;
      size_t index = map_hash(e->Key, newCap);
      e->Next = newBuckets[index];
      newBuckets[index] = e;
      e = next;
    }
  }

  free(m->Buckets);
  m->Buckets = newBuckets;
  m->Cap = newCap;
}

inline static bool map_Set(struct map *m, const char *key, int val) {
  if ((double)m->Size / (double)m->Cap >= 1.0) {
    map_rehash(m);
  }
  size_t index = map_hash(key, m->Cap);
  struct entry *e = m->Buckets[index];
  while (e) {
    if (strcmp(e->Key, key) == 0) {
      e->Val = val;
      return true;
    }
    e = e->Next;
  }
  e = new (struct entry);
  e->Key = key;
  e->Val = val;
  e->Next = m->Buckets[index];
  m->Buckets[index] = e;
  m->Size++;
  return false;
}

inline static bool map_Get(struct map *m, const char *key, int *val) {
  size_t index = map_hash(key, m->Cap);
  struct entry *e = m->Buckets[index];
  while (e) {
    if (strcmp(e->Key, key) == 0) {
      *val = e->Val;
      return true;
    }
    e = e->Next;
  }
  return false;
}

inline static void map_Free(struct map *m) {
  for (size_t i = 0; i < m->Cap; i++) {
    struct entry *e = m->Buckets[i];
    while (e) {
      struct entry *next = e->Next;
      if (e->Key) {
        free((void *)e->Key);
      }
      free(e);
      e = next;
    }
  }
  free(m->Buckets);
  m->Buckets = NULL;
}

inline static void map_Merge(struct map *lhs, struct map *rhs) {
  for (size_t i = 0; i < rhs->Cap; i++) {
    struct entry *rhs_e = rhs->Buckets[i];
    while (rhs_e) {
      struct entry *rhs_next = rhs_e->Next;
      if (map_Set(lhs, rhs_e->Key, rhs_e->Val)) {
        free((void *)rhs_e->Key);
      }
      rhs_e->Key = NULL;
      rhs_e = rhs_next;
    }
  }
  map_Free(rhs);
}

enum objectKind { object_Num = 1 };

struct object {
  enum objectKind Kind;
  bool Marked;
  struct object *Next;
};

inline static void object_Init(struct object *o, enum objectKind kind,
                        struct object *next) {
  o->Kind = kind;
  o->Marked = 0;
  o->Next = next;
}

inline static void object_Mark(struct object *o) {
  if (o->Marked) {
    return;
  }
  o->Marked = 1;
  // TODO: Mark other objects from the members.
}

struct gc {
  struct object *Stack[256];
  size_t StackSize, Reachable, Max;
  struct object *Root;
};

inline static void gc_Default(struct gc *c) {
  c->StackSize = 0;
  c->Reachable = 0;
  c->Max = 8;
  c->Root = NULL;
}

inline static void gc_Push(struct gc *gc, struct object *value) {
  if (gc->StackSize >= sizeof(gc->Stack) / sizeof(struct object *)) {
    printf("Stack overflow\n");
    exit(1);
  }
  gc->Stack[gc->StackSize] = value;
  gc->StackSize++;
}

inline static struct object *gc_Pop(struct gc *gc) {
  if (gc->StackSize <= 0) {
    printf("Stack overflow\n");
    exit(1);
  }
  struct object *ret = gc->Stack[gc->StackSize];
  gc->StackSize--;
  return ret;
}

inline static void gc_Mark(struct gc *vm) {
  for (size_t i = 0; i < vm->StackSize; i++) {
    object_Mark(vm->Stack[i]);
  }
}

inline static void gc_Sweep(struct gc *vm) {
  struct object **object = &vm->Root;
  while (*object) {
    if ((*object)->Marked) {
      (*object)->Marked = 0;
      object = &(*object)->Next;
      continue;
    }
    struct object *unreached = *object;
    *object = unreached->Next;
    free(unreached);
    vm->Reachable--;
  }
}

inline static void gc_Run(struct gc *vm) {
  gc_Mark(vm);
  gc_Sweep(vm);
  vm->Max = vm->Reachable == 0 ? 8 : vm->Reachable * 2;
}

inline static struct object *gc_NewObject(struct gc *vm, enum objectKind kind) {
  if (vm->Reachable == vm->Max) {
    gc_Run(vm);
  }
  struct object *o = new (struct object);
  object_Init(o, kind, vm->Root);
  vm->Root = o;
  vm->Reachable++;
  return o;
}

inline static void gc_Free(struct gc *vm) {
  vm->StackSize = 0;
  gc_Run(vm);
}

struct loc {
  size_t Pos, Ln, Col;
};

inline static void loc_Default(struct loc *l) {
  l->Pos = 0;
  l->Ln = 1;
  l->Col = 1;
}

inline static void loc_NextLine(struct loc *l) {
  l->Pos++;
  l->Ln++;
  l->Col = 1;
}

inline static void loc_NextColumn(struct loc *l) {
  l->Pos++;
  l->Col++;
}

struct span {
  struct loc Start, End;
};

struct Source {
  struct loc Loc;
  FILE *File;
  struct IDs *IDs;
  bool Failed, Atom, NewlineSensitive;
};

inline static void source_Init(struct Source *s, FILE *f, struct IDs *ids) {
  loc_Default(&s->Loc);
  s->File = f;
  s->IDs = ids;
  s->Failed = false;
  s->Atom = false;
  s->NewlineSensitive = false;
}

inline static size_t source_Size(struct Source *s) {
  fseek(s->File, 0, SEEK_END);
  long size = ftell(s->File);
  fseek(s->File, (long)s->Loc.Pos, SEEK_SET);
  return (size_t)size;
}

inline static const char *source_NewText(struct Source *s, struct span span) {
  size_t size = span.End.Pos - span.Start.Pos + 1;
  char *text = allocate(size);
  fseek(s->File, (long)span.Start.Pos, SEEK_SET);
  return fgets(text, (int)size, s->File);
}

inline static char source_Peek(struct Source *s) {
  if (fseek(s->File, (long)s->Loc.Pos, SEEK_SET) != 0) {
    return -1;
  }
  int c = fgetc(s->File);
  if (c == EOF) {
    return -1;
  }
  return (char)c;
}

inline static char source_Next(struct Source *s) {
  char next = source_Peek(s);
  if (next < 0) {
    return -1;
  }
  if (next == '\n') {
    loc_NextLine(&s->Loc);
    return next;
  }
  loc_NextColumn(&s->Loc);
  return next;
}

inline static struct Source *source_Back(struct Source *s, struct loc loc) {
  s->Loc = loc;
  s->Failed = false;
  return s;
}

inline static struct Source *source_Eat(struct Source *s, char c) {
  if (source_Next(s) != c) {
    s->Failed = true;
  }
  return s;
}

inline static struct Source *source_SkipSpaces(struct Source *s) {
  while (true) {
    char c = source_Peek(s);
    if (c < 0 || (s->NewlineSensitive && c == '\n') || !isspace(c)) {
      break;
    }
    s = source_Eat(s, c);
  }
  return s;
}

struct Parser;
struct Expr;
struct Def;

struct Range {
  char From, To;
};

union ParserCtx {
  const char *Word;
  struct Range Range;
  struct Parser *Parser;
  struct Parser **Parsers;

  struct span *Span;
  struct slice *Slice;
  struct node **Nodes;
  struct Expr *Expr;
  struct Def *Def;
};

struct Parser {
  struct Source *(*Parse)(union ParserCtx *ctx, struct Source *s);
  union ParserCtx Ctx;
};

inline static struct Source *soi(union ParserCtx *ctx, struct Source *s) {
  (void)ctx;
  if (s->Loc.Pos != 0) {
    s->Failed = true;
  }
  return s;
}

inline static struct Source *eoi(union ParserCtx *ctx, struct Source *s) {
  (void)ctx;
  if (s->Loc.Pos != source_Size(s)) {
    s->Failed = true;
  }
  return s;
}

static struct Parser Soi = {.Parse = soi};
static struct Parser Eoi = {.Parse = eoi};

inline static struct Source *parseAtom(struct Parser *parser, struct Source *s)
{ int atom = s->Atom; s->Atom = true; s = parser->Parse(&parser->Ctx, s);
  s->Atom = atom;
  return s;
}

inline static struct Source *word(union ParserCtx *ctx, struct Source *s) {
  const char *word = ctx->Word;
  size_t i = 0;
  for (char c = word[i]; c != '\0'; i++, c = word[i]) {
    s = source_Eat(s, c);
    if (s->Failed) {
      break;
    }
  }
  return s;
}

static struct Parser LParen = {word, {.Word = "("}};
static struct Parser RParen = {word, {.Word = ")"}};
static struct Parser Comma = {word, {.Word = ","}};
static struct Parser Under = {word, {.Word = "_"}};
static struct Parser Newline = {word, {.Word = "\n"}};
static struct Parser Semicolon = {word, {.Word = ";"}};
static struct Parser Assign = {word, {.Word = "="}};
static struct Parser If = {word, {.Word = "if"}};
static struct Parser Then = {word, {.Word = "then"}};
static struct Parser Else = {word, {.Word = "else"}};
static struct Parser Arrow = {word, {.Word = "=>"}};
static struct Parser Unit = {word, {.Word = "()"}};
static struct Parser False = {word, {.Word = "false"}};
static struct Parser True = {word, {.Word = "true"}};

inline static struct Source *range(union ParserCtx *ctx, struct Source *s) {
  char from = ctx->Range.From, to = ctx->Range.To;
  char c = source_Peek(s);
  if (c < from || c > to) {
    s->Failed = true;
    return s;
  }
  return source_Eat(s, c);
}

// static struct Parser AsciiBinDigit = {Range, {.Range = {'0', '1'}}};
// static struct Parser AsciiOctDigit = {Range, {.Range = {'0', '7'}}};
static struct Parser AsciiDigit = {range, {.Range = {'0', '9'}}};
// static struct Parser AsciiNonZeroDigit = {Range, {.Range = {'1', '9'}}};

inline static struct Source *parseLowercase(struct span *span, struct Source *s)
{ struct loc start = s->Loc;

  char first = source_Peek(s);
  if (first < 0 || !islower(first) || !isalpha(first)) {
    s->Failed = true;
    return s;
  }
  s = source_Eat(s, first);

  while (true) {
    char c = source_Peek(s);
    if (!(islower(c) && isalpha(c)) && c != '_') {
      break;
    }
    s = source_Eat(s, c);
  }

  *span = (struct span){start, s->Loc};
  return s;
}

inline static struct Source *lowercase(union ParserCtx *ctx, struct Source *s) {
  return parseLowercase(ctx->Span, s);
}

inline static struct Source *parseAll(struct Parser **parsers, struct Source *s)
{ struct Parser **parser = parsers; while (*parser) { s =
(*parser)->Parse(&(*parser)->Ctx, s); if (s->Failed) { return s;
    }
    parser++;
    if (!s->Atom && *parser) {
      s = source_SkipSpaces(s);
    }
  }
  return s;
}

inline static struct Source *all(union ParserCtx *ctx, struct Source *s) {
  return parseAll(ctx->Parsers, s);
}

inline static struct Source *parseAny(struct Parser **parsers, struct Source *s)
{ struct loc loc = s->Loc; for (struct Parser **parser = parsers; *parser;
parser++) { s = (*parser)->Parse(&(*parser)->Ctx, s); if (!s->Failed) { return
s;
    }
    s = source_Back(s, loc);
  }
  s->Failed = true;
  return s;
}

inline static struct Source *any(union ParserCtx *ctx, struct Source *s) {
  return parseAny(ctx->Parsers, s);
}

static struct Parser *EndSymbols[] = {&Semicolon, &Newline, NULL};
static struct Parser End = {any, {.Parsers = EndSymbols}};

inline static struct Source *parseEnd(struct Source *s) {
  int sensitive = s->NewlineSensitive;
  s->NewlineSensitive = true;
  s = source_SkipSpaces(s);
  s = End.Parse(&End.Ctx, s);
  s->NewlineSensitive = sensitive;
  return s;
}

inline static struct Source *many(union ParserCtx *ctx, struct Source *s) {
  while (true) {
    struct loc loc = s->Loc;
    s = ctx->Parser->Parse(&ctx->Parser->Ctx, s);
    if (s->Failed) {
      return source_Back(s, loc);
    }
    if (!s->Atom) {
      s = source_SkipSpaces(s);
    }
  }
}

inline static struct Source *option(union ParserCtx *ctx, struct Source *s) {
  struct loc loc = s->Loc;
  s = ctx->Parser->Parse(&ctx->Parser->Ctx, s);
  if (s->Failed) {
    return source_Back(s, loc);
  }
  return s;
}

enum ExprKind {
  Expr_App = 1,
  Expr_Ite,
  Expr_Lam,
  Expr_Num,
  Expr_Unit,
  Expr_False,
  Expr_True,
  Expr_Unresolved,
  Expr_Resolved,
};
union ExprData {
  struct App *App;
  struct Ite *Ite;
  struct Lambda *Lam;
  struct span Span;
  int ID;
};
struct Expr {
  enum ExprKind Kind;
  union ExprData Data;
};

inline static struct Source *ParseExpr(struct Expr *expr, struct Source *s);

inline static struct Source *Expr(union ParserCtx *ctx, struct Source *s) {
  return ParseExpr(ctx->Expr, s);
}

struct App {
  struct Expr F;
  struct slice Args;
};

inline static void App_Default(struct App *a) { slice_Init(&a->Args,
sizeof(struct Expr)); }

inline static struct Source *Arg(union ParserCtx *ctx, struct Source *s) {
  struct Expr a;
  s = ParseExpr(&a, s);
  if (!s->Failed) {
    slice_Append(ctx->Slice, &a);
  }
  return s;
}

inline static struct Source *Args(union ParserCtx *ctx, struct Source *s) {
  struct Parser *noArgs[] = {&LParen, &RParen, NULL};
  struct Parser allNoArgs = {all, {.Parsers = noArgs}};

  struct Parser oneArg = {Arg, {.Slice = ctx->Slice}};
  struct Parser *otherArgs[] = {&Comma, &oneArg, NULL};
  struct Parser allOtherArgs = {all, {.Parsers = otherArgs}};
  struct Parser manyOtherArgs = {many, {.Parser = &allOtherArgs}};
  struct Parser *multiArgs[] = {&LParen, &oneArg, &manyOtherArgs, &RParen,
                                NULL};
  struct Parser allMultiArgs = {all, {.Parsers = multiArgs}};

  struct Parser *branches[] = {&allNoArgs, &allMultiArgs, NULL};
  return parseAny(branches, s);
}

inline static struct Source *Expr_ref(union ParserCtx *ctx, struct Source *s);
inline static struct Source *Expr_paren(union ParserCtx *ctx, struct Source *s);

inline static struct Source *Expr_app(union ParserCtx *ctx, struct Source *s) {
  struct App *app = new (struct App);
  App_Default(app);

  struct Parser fnRef = {Expr_ref, {.Expr = &app->F}};
  struct Parser fnExpr = {Expr_paren, {.Expr = &app->F}};
  struct Parser *fnParsers[] = {&fnRef, &fnExpr, NULL};
  struct Parser f = {any, {.Parsers = fnParsers}};

  struct Parser xs = {Args, {.Slice = &app->Args}};

  struct Parser *parsers[] = {&f, &xs, NULL};
  s = parseAll(parsers, s);
  if (s->Failed) {
    free(app);
    return s;
  }
  ctx->Expr->Data.App = app;
  ctx->Expr->Kind = Expr_App;
  return s;
}

struct Ite {
  struct Expr If, Then, Else;
};

inline static struct Source *Expr_ite(union ParserCtx *ctx, struct Source *s) {
  struct Ite *ite = new (struct Ite);
  struct Parser i = {Expr, {.Expr = &ite->If}};
  struct Parser t = {Expr, {.Expr = &ite->Then}};
  struct Parser e = {Expr, {.Expr = &ite->Else}};
  struct Parser *parsers[] = {&If, &i, &Then, &t, &Else, &e, NULL};
  s = parseAll(parsers, s);
  if (s->Failed) {
    free(ite);
    return s;
  }
  ctx->Expr->Kind = Expr_Ite;
  ctx->Expr->Data.Ite = ite;
  return s;
}

struct Param {
  struct node AsNode;
  struct span Name;
};

inline static struct Source *Param(union ParserCtx *ctx, struct Source *s) {
  struct span name;
  s = parseLowercase(&name, s);
  if (s->Failed) {
    return s;
  }
  struct Param *param = new (struct Param);
  node_Default(&param->AsNode);
  param->Name = name;
  param->AsNode.Key = IDs_New(s->IDs);
  *ctx->Nodes = tree_Insert(*ctx->Nodes, (struct node *)param);
  return s;
}

inline static struct Source *Params(union ParserCtx *ctx, struct Source *s) {
  struct Parser *noParams[] = {&LParen, &RParen, NULL};
  struct Parser allNoParams = {all, {.Parsers = noParams}};

  struct Parser oneParam = {Param, {.Nodes = ctx->Nodes}};
  struct Parser *otherParams[] = {&Comma, &oneParam, NULL};
  struct Parser allOtherParams = {all, {.Parsers = otherParams}};
  struct Parser manyOtherParams = {many, {.Parser = &allOtherParams}};
  struct Parser *multiParams[] = {&LParen, &oneParam, &manyOtherParams, &RParen,
                                  NULL};
  struct Parser allMultiParams = {all, {.Parsers = multiParams}};

  struct Parser *branches[] = {&allNoParams, &allMultiParams, NULL};
  return parseAny(branches, s);
}

struct Lambda {
  struct Param *Params;
  struct Expr Body;
};

inline static void Lambda_Default(struct Lambda *lam) { lam->Params = NULL; }

inline static struct Source *Expr_lambda(union ParserCtx *ctx, struct Source *s)
{ struct Lambda *lam = new (struct Lambda); Lambda_Default(lam); struct Parser
ps = {Params, {.Nodes = (struct node **)&lam->Params}}; struct Parser body =
{Expr, {.Expr = &lam->Body}}; struct Parser *parsers[] = {&ps, &Arrow, &body,
NULL}; s = parseAll(parsers, s); if (s->Failed) { free(lam); return s;
  }
  ctx->Expr->Kind = Expr_Lam;
  ctx->Expr->Data.Lam = lam;
  return s;
}

inline static struct Source *decimalDigits(union ParserCtx *ctx, struct Source
*s) { struct loc loc = s->Loc; struct Parser optionalUnder = {option, {.Parser =
&Under}}; struct Parser *otherDigits[] = {&optionalUnder, &AsciiDigit, NULL};
  struct Parser allOtherDigits = {all, {.Parsers = otherDigits}};
  struct Parser manyOtherDigits = {many, {.Parser = &allOtherDigits}};
  struct Parser *digits[] = {&AsciiDigit, &manyOtherDigits, NULL};
  s = parseAll(digits, s);
  if (!s->Failed) {
    *ctx->Span = (struct span){loc, s->Loc};
  }
  return s;
}

inline static struct Source *decimalNumber(union ParserCtx *ctx, struct Source
*s) { struct Parser parser = {decimalDigits, {.Span = ctx->Span}}; return
parseAtom(&parser, s);
}

inline static struct Source *Number(union ParserCtx *ctx, struct Source *s) {
  return decimalNumber(ctx, s);
}

inline static struct Source *Expr_number(union ParserCtx *ctx, struct Source *s)
{ struct span num; union ParserCtx num_ctx = {.Span = &num}; s =
Number(&num_ctx, s); if (!s->Failed) { ctx->Expr->Kind = Expr_Num;
    ctx->Expr->Data.Span = num;
  }
  return s;
}

inline static struct Source *Expr_unit(union ParserCtx *ctx, struct Source *s) {
  s = Unit.Parse(&Unit.Ctx, s);
  if (!s->Failed) {
    ctx->Expr->Kind = Expr_Unit;
  }
  return s;
}

inline static struct Source *Expr_false(union ParserCtx *ctx, struct Source *s)
{ s = False.Parse(&False.Ctx, s); if (!s->Failed) { ctx->Expr->Kind =
Expr_False;
  }
  return s;
}

inline static struct Source *Expr_true(union ParserCtx *ctx, struct Source *s) {
  s = True.Parse(&True.Ctx, s);
  if (!s->Failed) {
    ctx->Expr->Kind = Expr_True;
  }
  return s;
}

inline static struct Source *Expr_ref(union ParserCtx *ctx, struct Source *s) {
  struct span ref;
  s = parseLowercase(&ref, s);
  if (!s->Failed) {
    ctx->Expr->Kind = Expr_Unresolved;
    ctx->Expr->Data.Span = ref;
  }
  return s;
}

inline static struct Source *Expr_paren(union ParserCtx *ctx, struct Source *s)
{ struct Parser e = {Expr, {.Expr = ctx->Expr}}; struct Parser *parsers[] =
{&LParen, &e, &RParen, NULL}; return parseAll(parsers, s);
}

inline static struct Source *ParseExpr(struct Expr *expr, struct Source *s) {
  struct Parser app = {Expr_app, {.Expr = expr}};
  struct Parser ite = {Expr_ite, {.Expr = expr}};
  struct Parser lam = {Expr_lambda, {.Expr = expr}};
  struct Parser num = {Expr_number, {.Expr = expr}};
  struct Parser unit = {Expr_unit, {.Expr = expr}};
  struct Parser fl = {Expr_false, {.Expr = expr}};
  struct Parser tr = {Expr_true, {.Expr = expr}};
  struct Parser ref = {Expr_ref, {.Expr = expr}};
  struct Parser paren = {Expr_paren, {.Expr = expr}};
  struct Parser *branches[] = {&app, &ite, &lam, &num,   &unit,
                               &fl,  &tr,  &ref, &paren, NULL};
  return parseAny(branches, s);
}

enum DefKind { Def_Fn = 1, Def_Val };
union DefBody {
  struct Expr Ret;
};

struct Def {
  struct node AsNode;

  struct span Name;
  struct Param *Params;
  enum DefKind Kind;
  union DefBody Body;
};

inline static void Def_Default(struct Def *d) {
  node_Default(&d->AsNode);
  d->Params = NULL;
}

inline static struct Source *Fn(union ParserCtx *ctx, struct Source *s) {
  struct Parser name = {lowercase, {.Span = &ctx->Def->Name}};
  struct Parser ps = {Params, {.Nodes = (struct node **)&ctx->Def->Params}};
  struct Parser ret = {Expr, {.Expr = &ctx->Def->Body.Ret}};
  struct Parser *parsers[] = {&name, &ps, &ret, NULL};
  s = parseAll(parsers, s);
  if (s->Failed) {
    return s;
  }
  s = parseEnd(s);
  if (!s->Failed) {
    ctx->Def->Kind = Def_Fn;
  }
  return s;
}

inline static struct Source *Val(union ParserCtx *ctx, struct Source *s) {
  struct Parser name = {lowercase, {.Span = &ctx->Def->Name}};
  struct Parser ret = {Expr, {.Expr = &ctx->Def->Body.Ret}};
  struct Parser *parsers[] = {&name, &Assign, &ret, NULL};
  s = parseAll(parsers, s);
  if (s->Failed) {
    return s;
  }
  s = parseEnd(s);
  if (!s->Failed) {
    ctx->Def->Kind = Def_Val;
  }
  return s;
}

inline static struct Source *Def(union ParserCtx *ctx, struct Source *s) {
  struct Def *d = new (struct Def);
  Def_Default(d);

  struct Parser fn = {Fn, {.Def = d}};
  struct Parser val = {Val, {.Def = d}};

  struct Parser *branches[] = {&fn, &val, NULL};
  s = parseAny(branches, s);
  if (s->Failed) {
    free(d);
    return s;
  }
  d->AsNode.Key = IDs_New(s->IDs);
  *ctx->Nodes = tree_Insert(*ctx->Nodes, (struct node *)d);
  return s;
}

struct Program {
  struct Def *Defs;
};

inline static void Program_Default(struct Program *p) { p->Defs = NULL; }

inline static struct Source *ParseProgram(struct Def **defs, struct Source *s) {
  struct Parser oneDef = {Def, {.Nodes = (struct node **)defs}};
  struct Parser manyDefs = {many, {.Parser = &oneDef}};
  struct Parser *parsers[] = {&Soi, &manyDefs, &Eoi, NULL};
  return parseAll(parsers, s);
}

enum Resolution { Resolution_OK, Resolution_NotFound, Resolution_Duplicate };

inline static const char *Resolution_ToString(enum Resolution state) {
  switch (state) {
  case Resolution_OK:
    return "resolved successfully";
  case Resolution_NotFound:
    return "variable not found";
  case Resolution_Duplicate:
    return "duplicate variable";
  }
}

struct Resolver {
  struct Source *Src;
  struct map Globals, Locals, Params;
  enum Resolution State;
  struct span NameSpan;
  const char *NameText;
};

inline static void Resolver_Init(struct Resolver *r, struct Source *s) {
  r->Src = s;
  map_Default(&r->Globals);
  r->State = Resolution_OK;
  r->NameText = NULL;
}

inline static void Resolver_Free(struct Resolver *r) {
  map_Free(&r->Globals);
  if (r->NameText) {
    free((void *)r->NameText);
  }
}

inline static void Resolver_Expr(struct Resolver *r, struct Expr *e);

inline static void Resolver_resolveArg(void *data, void *arg) {
  Resolver_Expr((struct Resolver *)data, (struct Expr *)arg);
}

inline static void Resolver_validateLocal(void *data, struct node *node) {
  struct Resolver *r = data;
  if (r->State != Resolution_OK) {
    return;
  }
  struct Param *p = (struct Param *)node;
  const char *nameText = source_NewText(r->Src, p->Name);
  if (map_Set(&r->Params, nameText, p->AsNode.Key)) {
    r->State = Resolution_Duplicate;
    r->NameSpan = p->Name;
    r->NameText = nameText;
  }
}

inline static void Resolver_insertLocal(void *data, struct node *node) {
  struct Resolver *r = data;
  if (r->State != Resolution_OK) {
    return;
  }
  struct Param *p = (struct Param *)node;
  const char *nameText = source_NewText(r->Src, p->Name);
  if (map_Set(&r->Locals, nameText, p->AsNode.Key)) {
    free((void *)nameText);
  }
}

inline static void Resolver_insertLocals(struct Resolver *r, struct Param
*params) { map_Default(&r->Params); tree_Iter(r, (struct node *)params,
Resolver_validateLocal); if (r->State != Resolution_OK) { return;
  }
  map_Merge(&r->Locals, &r->Params);
  tree_Iter(r, (struct node *)params, Resolver_insertLocal);
}

inline static void Resolver_Expr(struct Resolver *r, struct Expr *e) {
  switch (e->Kind) {
  case Expr_App: {
    Resolver_Expr(r, &e->Data.App->F);
    if (r->State != Resolution_OK) {
      return;
    }
    slice_Iter(r, &e->Data.App->Args, Resolver_resolveArg);
    return;
  }
  case Expr_Ite: {
    Resolver_Expr(r, &e->Data.Ite->If);
    if (r->State != Resolution_OK) {
      return;
    }
    Resolver_Expr(r, &e->Data.Ite->Then);
    if (r->State != Resolution_OK) {
      return;
    }
    Resolver_Expr(r, &e->Data.Ite->Else);
    return;
  }
  case Expr_Lam: {
    Resolver_insertLocals(r, e->Data.Lam->Params);
    if (r->State != Resolution_OK) {
      return;
    }
    Resolver_Expr(r, &e->Data.Lam->Body);
    return;
  }
  case Expr_Unresolved: {
    const char *nameText = source_NewText(r->Src, e->Data.Span);
    int id;
    if (map_Get(&r->Locals, nameText, &id) ||
        map_Get(&r->Globals, nameText, &id)) {
      free((void *)nameText);
      e->Kind = Expr_Resolved;
      e->Data.ID = id;
      return;
    }
    r->State = Resolution_NotFound;
    r->NameSpan = e->Data.Span;
    r->NameText = nameText;
    return;
  }
  case Expr_Num:
  case Expr_Unit:
  case Expr_False:
  case Expr_True:
    return;
  case Expr_Resolved:
    unreachable();
  }
}

inline static void Resolver_insertGlobal(void *data, struct node *node) {
  struct Resolver *r = data;
  if (r->State != Resolution_OK) {
    return;
  }

  struct Def *d = (struct Def *)node;
  const char *nameText = source_NewText(r->Src, d->Name);
  if (map_Set(&r->Globals, nameText, d->AsNode.Key)) {
    r->State = Resolution_Duplicate;
    r->NameSpan = d->Name;
    r->NameText = nameText;
    return;
  }

  map_Default(&r->Locals);
  Resolver_insertLocals(r, d->Params);
  if (r->State == Resolution_OK) {
    Resolver_Expr(r, &d->Body.Ret);
  }
  map_Free(&r->Locals);
}

inline static void Resolver_Program(struct Resolver *r, struct Program *p) {
  tree_Iter(r, (struct node *)p->Defs, Resolver_insertGlobal);
}

enum TermKind {
  Term_Univ = 1,

  Term_FnType,
  Term_NumType,
  Term_UnitType,
  Term_BoolType,

  Term_Fn,
  Term_Num,
  Term_Unit,
  Term_False,
  Term_True,
};

struct Term {
  enum TermKind Kind;
};

enum ThmKind { Thm_Undefined = 1 };

struct Thm {
  struct node AsNode;

  enum ThmKind Kind;
};

enum ElabStateKind {
  Elaboration_OK,
  Elaboration_CheckFailed,
  Elaboration_InferFailed
};

struct ElabState {
  enum ElabStateKind Kind;
  struct Expr *Expr;
  struct Term *Got, *Expected;
};

inline static void ElabState_Default(struct ElabState *s) {
  s->Kind = Elaboration_OK;
  s->Expr = NULL;
  s->Got = NULL;
  s->Expr = NULL;
}

struct Elab {
  struct node *Metas, *Globals, *Locals;
  struct IDs *IDs;
  struct ElabState State;
};

inline static void Elab_Init(struct Elab *e, struct IDs *ids) {
  e->Metas = NULL;
  e->Globals = NULL;
  e->Locals = NULL;
  e->IDs = ids;
  ElabState_Default(&e->State);
}

inline static void Elab_Check(struct Elab *e, struct Expr *ex, struct Term *ty)
{ (void)e; switch (ex->Kind) { case Expr_App:
    // TODO
    panic("TODO: application");
  case Expr_Ite:
    // TODO
    panic("TODO: if-then-else");
  case Expr_Lam:
    // TODO
    panic("TODO: lambda");
  case Expr_Num:
    if (ty->Kind == Term_NumType) {
      return;
    }
    // TODO
    panic("TODO: fail");
  case Expr_Unit:
    if (ty->Kind == Term_UnitType) {
      return;
    }
    // TODO
    panic("TODO: fail");
  case Expr_False:
  case Expr_True:
    if (ty->Kind == Term_BoolType) {
      return;
    }
    // TODO
    panic("TODO: fail");
  case Expr_Resolved:
    // TODO
    panic("TODO: reference");
  case Expr_Unresolved:
    unreachable();
  }
}

inline static void Elab_Infer(struct Elab *e, struct Expr *ex, struct Term *tm,
                struct Term *ty) {
  (void)e;
  switch (ex->Kind) {
  case Expr_App:
    // TODO
    panic("TODO: application");
  case Expr_Ite:
    // TODO
    panic("TODO: if-then-else");
  case Expr_Lam:
    tm->Kind = Term_Fn;
    ty->Kind = Term_FnType;
    // TODO
    panic("TODO: lambda");
  case Expr_Num:
    tm->Kind = Term_Num;
    ty->Kind = Term_NumType;
    return;
  case Expr_Unit:
    tm->Kind = Term_Unit;
    ty->Kind = Term_UnitType;
    return;
  case Expr_False:
    tm->Kind = Term_False;
    ty->Kind = Term_BoolType;
    return;
  case Expr_True:
    tm->Kind = Term_True;
    ty->Kind = Term_BoolType;
    return;
  case Expr_Resolved:
    // TODO
    panic("TODO: reference");
  case Expr_Unresolved:
    unreachable();
  }
}

// void Elab_Def(struct Elab *e, struct Def *d) {}
// void Elab_Program(struct Elab *e, struct Program *p) {}

struct Driver {
  const char *Filename;
  FILE *Infile;
  struct IDs IDs;
  struct Source Src;
};

inline static int Driver_printUsage(void) {
  printf("JianScript programming language.\n"
         "\n"
         "Usage:\n"
         "\n"
         "\tjian <command> [<arguments>]\n"
         "\n"
         "Commands are:\n"
         "\n"
         "\tjian run\t\trun a script with the default JIT mode\n"
         "\tjian help\tprint this usage message\n"
         "\tjian version\tprint the version\n"
         "\n");
  return 0;
}

inline static int Driver_printVersion(void) {
  printf("JianScript v%d.%d.%d\n", JIAN_VERSION_MAJOR, JIAN_VERSION_MINOR,
         JIAN_VERSION_PATCH);
  return 0;
}

inline static int Driver_runScript(struct Driver *d, const char *filename) {
  d->Filename = filename;
  d->Infile = fopen(d->Filename, "r");
  if (!d->Infile) {
    perror("open file error");
    return -1;
  }

  IDs_Default(&d->IDs);
  source_Init(&d->Src, d->Infile, &d->IDs);
  struct Program p;
  Program_Default(&p);
  struct Source *s = ParseProgram(&p.Defs, &d->Src);
  if (s->Failed) {
    printf("%s:%lu:%lu: Parse error (pos=%lu)\n", d->Filename, s->Loc.Ln,
           s->Loc.Col, s->Loc.Pos);
    return -1;
  }

  struct Resolver resolver;
  Resolver_Init(&resolver, &d->Src);
  Resolver_Program(&resolver, &p);
  if (resolver.State == Resolution_OK) {
    return 0;
  }

  printf("%s:%lu:%lu: resolve error: %s \"%s\"\n", d->Filename,
         resolver.NameSpan.Start.Ln, resolver.NameSpan.Start.Col,
         Resolution_ToString(resolver.State), resolver.NameText);
  return -1;
}

inline static int Driver_Run(struct Driver *d, int argc, const char *argv[]) {
  switch (argc) {
  case 2: {
    if (strcmp(argv[1], "help") == 0) {
      return Driver_printUsage();
    }
    if (strcmp(argv[1], "version") == 0) {
      return Driver_printVersion();
    }
    break;
  }
  case 3:
    if (strcmp(argv[1], "run") == 0) {
      return Driver_runScript(d, argv[2]);
    }
    break;
  default:
    break;
  }
  Driver_printUsage();
  return -1;
}

inline static int Driver_Free(struct Driver *i) {
  int ret = fclose(i->Infile);
  if (ret != 0) {
    perror("close file error");
  }
  return ret;
}

#if !__has_feature(address_sanitizer) && !__has_feature(thread_sanitizer) &&   \
    !__has_feature(memory_sanitizer) &&                                        \
    !__has_feature(undefined_behavior_sanitizer)
inline static void onSignal(int sig) { panic(strerror(sig)); }
inline static void recovery(void) { signal(SIGSEGV, onSignal); }
#else
inline static void recovery(void) {}
#endif
*/

namespace jian {

static inline void create_code(gccjit::context ctxt) {
  /* Let's try to inject the equivalent of this C code:
     void
     greet (const char *name)
     {
        printf ("hello %s\n", name);
     }
  */
  gccjit::type void_type = ctxt.get_type(GCC_JIT_TYPE_VOID);
  gccjit::type const_char_ptr_type = ctxt.get_type(GCC_JIT_TYPE_CONST_CHAR_PTR);
  gccjit::param param_name = ctxt.new_param(const_char_ptr_type, "name");
  std::vector<gccjit::param> func_params;
  func_params.push_back(param_name);
  gccjit::function func = ctxt.new_function(GCC_JIT_FUNCTION_EXPORTED,
                                            void_type, "greet", func_params, 0);

  gccjit::param param_format = ctxt.new_param(const_char_ptr_type, "format");
  std::vector<gccjit::param> printf_params;
  printf_params.push_back(param_format);
  gccjit::function printf_func = ctxt.new_function(
      GCC_JIT_FUNCTION_IMPORTED, ctxt.get_type(GCC_JIT_TYPE_INT), "printf",
      printf_params, 1);

  gccjit::block block = func.new_block();
  block.add_eval(
      ctxt.new_call(printf_func, ctxt.new_rvalue("Hello, %s!\n"), param_name));
  block.end_with_return();
}

static inline int main(int argc, const char *argv[]) {
  (void)argc;
  (void)argv;

  /*
  recovery();

  struct Driver driver;
  if (Driver_Run(&driver, argc, argv) != 0) {
    return 1;
  }
  */

  gccjit::context ctxt;
  gcc_jit_result *result;

  /* Get a "context" object for working with the library.  */
  ctxt = gccjit::context::acquire();

  /* Set some options on the context.
     Turn this on to see the code being generated, in assembler form.  */
  ctxt.set_bool_option(GCC_JIT_BOOL_OPTION_DUMP_GENERATED_CODE, 0);

  /* Populate the context.  */
  create_code(ctxt);

  /* Compile the code.  */
  result = ctxt.compile();
  if (!result) {
    fprintf(stderr, "NULL result");
    exit(1);
  }

  ctxt.release();

  /* Extract the generated code from "result".  */
  typedef void (*fn_type)(const char *);
  fn_type greet =
      reinterpret_cast<fn_type>(gcc_jit_result_get_code(result, "greet"));
  if (!greet) {
    fprintf(stderr, "NULL greet");
    exit(1);
  }

  /* Now call the generated function: */
  greet("JianScript");
  fflush(stdout);

  gcc_jit_result_release(result);
  return 0;
}

} // namespace jian
