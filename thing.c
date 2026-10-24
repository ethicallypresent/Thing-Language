#include <stdarg.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* ---------- errors ---------- */

/* Files: 0 is the program itself, and every file it outsources gets the next number. */
#define MAX_FILES 64
static const char *file_names[MAX_FILES]; /* as written in the outsource, for error messages */
static char *file_paths[MAX_FILES];       /* where each was found */
static char *file_text[MAX_FILES];
static int nfiles = 1;
static int cur_file; /* the file being read by the parser */
static int err_file; /* the file an error belongs to, while parsing and while running */
static const char *main_path;

static void die(int line, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

static void die(int line, const char *fmt, ...) {
    va_list ap;
    if (err_file > 0) {
        fprintf(stderr, "thing: %s, line %d: ", file_names[err_file], line);
    } else {
        fprintf(stderr, "thing: line %d: ", line);
    }
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

static char *copy_str(const char *s, size_t n) {
    char *p = malloc(n + 1);
    if (p == NULL) {
        die(0, "out of memory");
    }
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

/* ---------- tokenizer ---------- */

typedef enum { T_WORD, T_STRING, T_NUMBER, T_PUNCT, T_EOF } TokType;

typedef struct {
    TokType type;
    char *text;
    int line;
    int glued; /* no whitespace between this token and the one before it */
} Token;

static Token *tokens;
static size_t ntokens, captokens;

static int cur_glued;
static int first_line = 1; /* the line the next tokenize call starts on */
static int allow_notes = 1; /* off while reading the value inside { } in text */
static int note_mode;       /* thing notes: list the Next notes as they are found */
static const char *note_file;
static int notes_found;

static void push_token(TokType type, char *text, int line) {
    if (ntokens == captokens) {
        captokens = captokens ? captokens * 2 : 64;
        tokens = realloc(tokens, captokens * sizeof *tokens);
        if (tokens == NULL) {
            die(line, "out of memory");
        }
    }
    tokens[ntokens++] = (Token){type, text, line, cur_glued};
}

static void tokenize(const char *src) {
    int line = first_line;
    const char *p = src;
    while (*p) {
        unsigned char c = *p;
        cur_glued = p > src && !isspace((unsigned char)p[-1]);
        if (c == '\n') {
            line++;
            p++;
        } else if (isspace(c)) {
            p++;
        } else if (isdigit(c)) {
            /* a '.' is only part of a number when a digit follows,
               so "5." is the number 5 and then the end of the statement */
            const char *start = p++;
            while (isdigit((unsigned char)*p)) {
                p++;
            }
            if (*p == '.' && isdigit((unsigned char)p[1])) {
                p++;
                while (isdigit((unsigned char)*p)) {
                    p++;
                }
            }
            push_token(T_NUMBER, copy_str(start, p - start), line);
        } else if (isalpha(c) || c == '_') {
            const char *start = p;
            while (isalnum((unsigned char)*p) || *p == '_') {
                p++;
            }
            /* "bigger-than" and "smaller-than" are single words (a '-' is otherwise minus) */
            if (((p - start == 6 && strncmp(start, "bigger", 6) == 0) ||
                 (p - start == 7 && strncmp(start, "smaller", 7) == 0)) &&
                strncmp(p, "-than", 5) == 0 && !isalnum((unsigned char)p[5]) && p[5] != '_') {
                p += 5;
            }
            /* "if-not" is one word too */
            if (p - start == 2 && (strncmp(start, "if", 2) == 0 || strncmp(start, "If", 2) == 0) &&
                strncmp(p, "-not", 4) == 0 && !isalnum((unsigned char)p[4]) && p[4] != '_') {
                p += 4;
            }
            /* "List-of", "Book-of" and "value-of" are single words too */
            if (((p - start == 4 && (strncmp(start, "List", 4) == 0 || strncmp(start, "Book", 4) == 0)) ||
                 (p - start == 5 && strncmp(start, "value", 5) == 0)) &&
                strncmp(p, "-of", 3) == 0 && !isalnum((unsigned char)p[3]) && p[3] != '_') {
                p += 3;
            }
            /* A statement that starts with Now or Next is a note: it is skipped, up to the period that
               ends it (a '.' followed by a space or the end of the file). It may start where a
               statement starts, or after a comma, such as the one that begins a function's body. */
            if (allow_notes &&
                ((p - start == 3 && strncmp(start, "Now", 3) == 0) || (p - start == 4 && strncmp(start, "Next", 4) == 0)) &&
                (ntokens == 0 || (tokens[ntokens - 1].type == T_PUNCT && strchr(".!,", tokens[ntokens - 1].text[0]) != NULL))) {
                int note_line = line;
                int is_next = p - start == 4;
                const char *body = p;
                while (*p && !(*p == '.' && (p[1] == '\0' || isspace((unsigned char)p[1])))) {
                    if (*p == '\n') {
                        line++;
                    }
                    p++;
                }
                if (*p == '\0') {
                    die(note_line, "this note needs a period to end it");
                }
                if (note_mode && is_next) { /* show it on one line, without the comma that follows Next */
                    notes_found++;
                    printf("%s:%d: ", note_file, note_line);
                    while (body < p && (*body == ',' || *body == ':' || isspace((unsigned char)*body))) {
                        body++;
                    }
                    int gap = 0;
                    for (; body < p; body++) {
                        if (isspace((unsigned char)*body)) {
                            gap = 1;
                        } else {
                            if (gap) {
                                putchar(' ');
                            }
                            gap = 0;
                            putchar(*body);
                        }
                    }
                    puts(".");
                }
                p++;
                continue;
            }
            push_token(T_WORD, copy_str(start, p - start), line);
        } else if (c == '"') {
            int start_line = line;
            const char *start = ++p;
            while (*p && *p != '"') {
                if (*p == '\\' && p[1] != '\0') { /* a backslash and the character after it go together */
                    if (p[1] == '\n') {
                        line++;
                    }
                    p += 2;
                    continue;
                }
                if (*p == '\n') {
                    line++;
                }
                p++;
            }
            if (*p == '\0') {
                die(start_line, "unterminated string");
            }
            /* \" is a double quote, \n a line break, \t a tab, \r a carriage return, \\ a backslash */
            char *text = malloc((size_t)(p - start) + 1);
            if (text == NULL) {
                die(start_line, "out of memory");
            }
            size_t n = 0;
            for (const char *q = start; q < p; q++) {
                if (*q != '\\') {
                    text[n++] = *q;
                    continue;
                }
                q++;
                switch (*q) {
                case '"': text[n++] = '"'; break;
                case 'n': text[n++] = '\n'; break;
                case 't': text[n++] = '\t'; break;
                case 'r': text[n++] = '\r'; break;
                case '\\': text[n++] = '\\'; break;
                default:
                    die(start_line, "unknown escape \\%c in text; use \\\\ for a backslash itself", *q);
                }
            }
            text[n] = '\0';
            push_token(T_STRING, text, start_line);
            p++;
        } else {
            push_token(T_PUNCT, copy_str(p, 1), line);
            p++;
        }
    }
    push_token(T_EOF, copy_str("", 0), line);
}

/* ---------- values ---------- */

typedef enum { V_NIL, V_NUM, V_STR, V_BOOL, V_LIST, V_DICT, V_OBJ } ValType;

typedef struct Value Value;
typedef struct { Value *items; size_t len, cap; } List;
/* A Book: its Entries are named with words */
typedef struct { char **keys; Value *vals; size_t len, cap; } Dict;

struct Value {
    ValType type;
    double num;
    char *str;  /* owned by whoever holds the Value */
    int b;
    List *list; /* owned, and so are the items inside */
    Dict *dict; /* owned, and so are the keys and values inside (V_DICT, and the fields of a V_OBJ) */
    int tmpl;   /* V_OBJ: which template it was made from */
};

static const char *template_name(int index);

static void *xalloc(void *old, size_t n) {
    void *p = realloc(old, n ? n : 1);
    if (p == NULL) {
        die(0, "out of memory");
    }
    return p;
}

static Value new_list(void) {
    Value v = {.type = V_LIST};
    v.list = xalloc(NULL, sizeof *v.list);
    *v.list = (List){0};
    return v;
}

static Value new_dict(void) {
    Value v = {.type = V_DICT};
    v.dict = xalloc(NULL, sizeof *v.dict);
    *v.dict = (Dict){0};
    return v;
}

static void list_push(List *l, Value item) {
    if (l->len == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 4;
        l->items = xalloc(l->items, l->cap * sizeof *l->items);
    }
    l->items[l->len++] = item;
}

static long dict_find(Dict *d, const char *key) {
    for (size_t i = 0; i < d->len; i++) {
        if (strcmp(d->keys[i], key) == 0) {
            return (long)i;
        }
    }
    return -1;
}

static void free_value(Value v);

/* Takes ownership of val; an existing Entry with the same name is replaced. */
static void dict_set(Dict *d, const char *key, Value val) {
    long at = dict_find(d, key);
    if (at >= 0) {
        free_value(d->vals[at]);
        d->vals[at] = val;
        return;
    }
    if (d->len == d->cap) {
        d->cap = d->cap ? d->cap * 2 : 4;
        d->keys = xalloc(d->keys, d->cap * sizeof *d->keys);
        d->vals = xalloc(d->vals, d->cap * sizeof *d->vals);
    }
    d->keys[d->len] = copy_str(key, strlen(key));
    d->vals[d->len] = val;
    d->len++;
}

/* Values are copied, not shared: a list or Book is copied whole when it is assigned. */
static Value copy_value(Value v) {
    switch (v.type) {
    case V_STR:
        v.str = copy_str(v.str, strlen(v.str));
        break;
    case V_LIST: {
        Value out = new_list();
        for (size_t i = 0; i < v.list->len; i++) {
            list_push(out.list, copy_value(v.list->items[i]));
        }
        return out;
    }
    case V_DICT:
    case V_OBJ: {
        Value out = new_dict();
        out.type = v.type;
        out.tmpl = v.tmpl;
        for (size_t i = 0; i < v.dict->len; i++) {
            dict_set(out.dict, v.dict->keys[i], copy_value(v.dict->vals[i]));
        }
        return out;
    }
    default:
        break;
    }
    return v;
}

static void free_value(Value v) {
    switch (v.type) {
    case V_STR:
        free(v.str);
        break;
    case V_LIST:
        for (size_t i = 0; i < v.list->len; i++) {
            free_value(v.list->items[i]);
        }
        free(v.list->items);
        free(v.list);
        break;
    case V_DICT:
    case V_OBJ:
        for (size_t i = 0; i < v.dict->len; i++) {
            free(v.dict->keys[i]);
            free_value(v.dict->vals[i]);
        }
        free(v.dict->keys);
        free(v.dict->vals);
        free(v.dict);
        break;
    default:
        break;
    }
}

typedef struct {
    char *s;
    size_t len, cap;
} Buf;

static void buf_add(Buf *b, const char *text, size_t n) {
    if (b->len + n + 1 > b->cap) {
        b->cap = (b->len + n + 1) * 2;
        b->s = xalloc(b->s, b->cap);
    }
    memcpy(b->s + b->len, text, n);
    b->len += n;
    b->s[b->len] = '\0';
}

/* Inside a list or Book, words are shown in quotes. */
static void write_value(Buf *b, Value v, int nested) {
    char num[64];
    switch (v.type) {
    case V_NIL:
        buf_add(b, "nil", 3);
        break;
    case V_NUM:
        if (v.num < 1e15 && v.num > -1e15 && v.num == (double)(long long)v.num) {
            snprintf(num, sizeof num, "%lld", (long long)v.num);
        } else {
            snprintf(num, sizeof num, "%.15g", v.num);
        }
        buf_add(b, num, strlen(num));
        break;
    case V_STR:
        if (nested) {
            buf_add(b, "\"", 1);
        }
        buf_add(b, v.str, strlen(v.str));
        if (nested) {
            buf_add(b, "\"", 1);
        }
        break;
    case V_BOOL:
        buf_add(b, v.b ? "Yes" : "No", v.b ? 3 : 2);
        break;
    case V_LIST:
        buf_add(b, "[", 1);
        for (size_t i = 0; i < v.list->len; i++) {
            if (i) {
                buf_add(b, ", ", 2);
            }
            write_value(b, v.list->items[i], 1);
        }
        buf_add(b, "]", 1);
        break;
    case V_DICT:
        buf_add(b, "{", 1);
        for (size_t i = 0; i < v.dict->len; i++) {
            if (i) {
                buf_add(b, ", ", 2);
            }
            buf_add(b, "\"", 1);
            buf_add(b, v.dict->keys[i], strlen(v.dict->keys[i]));
            buf_add(b, "\": ", 3);
            write_value(b, v.dict->vals[i], 1);
        }
        buf_add(b, "}", 1);
        break;
    case V_OBJ:
        buf_add(b, template_name(v.tmpl), strlen(template_name(v.tmpl)));
        buf_add(b, "{", 1);
        for (size_t i = 0; i < v.dict->len; i++) {
            if (i) {
                buf_add(b, ", ", 2);
            }
            buf_add(b, v.dict->keys[i], strlen(v.dict->keys[i]));
            buf_add(b, ": ", 2);
            write_value(b, v.dict->vals[i], 1);
        }
        buf_add(b, "}", 1);
        break;
    }
}

/* The returned string is owned by the caller. */
static char *value_to_text(Value v) {
    Buf b = {0};
    write_value(&b, v, 0);
    return b.s ? b.s : copy_str("", 0);
}

static void print_value(Value v) {
    char *text = value_to_text(v);
    puts(text);
    free(text);
}

/* ---------- parser ---------- */

/* word operators in Expr.op (the signs use their own character) */
#define OP_PLUS 'P'
#define OP_MINUS 'M'
#define OP_EQUALS 'E'
#define OP_NOTEQ 'N'
#define OP_BIGGER 'B'
#define OP_SMALLER 'S'
#define OP_AND '&'
#define OP_OR '|'
#define OP_IN 'I'

typedef enum { E_LITERAL, E_VAR, E_ASKS, E_NEG, E_BINARY, E_TONUM, E_LIST, E_DICT, E_INDEX, E_BUILTIN, E_CALL, E_INTERP, E_NOT, E_METHOD, E_FIELD } ExprType;

typedef struct Expr Expr;
struct Expr {
    ExprType type;
    int line;
    Value lit;    /* E_LITERAL */
    char *name;   /* E_VAR, E_CALL (the function's name), E_METHOD (the method's name), E_FIELD (the field's name) */
    char *prompt; /* E_ASKS */
    char op;      /* E_BUILTIN: 'R' round, 'U' fly, 'D' sink, 'G' range, 'r' read, 'l' lines, 's' split, 'x' exists, 't' Total, 'c' Counted, 'm' Smaller, 'M' Bigger, 'o' Order. E_BINARY: + - * / % or OP_PLUS/OP_MINUS/OP_EQUALS/OP_NOTEQ/OP_BIGGER/OP_SMALLER/OP_AND/OP_OR/OP_IN */
    Expr *left;   /* E_NEG, E_NOT, E_TONUM (operand), E_BINARY, E_METHOD and E_FIELD (the object) */
    Expr *right;  /* E_BINARY, E_INDEX (the position or Entry) */
    Expr *args;   /* E_LIST: the items. E_DICT: Entry, value, Entry, value, ... E_BUILTIN and E_CALL: the arguments. E_INTERP: the pieces of text and values to join */
    size_t nargs;
};

typedef enum { S_THING, S_ASSIGN, S_SAY, S_FOR, S_IF, S_IFNOT, S_APPEND, S_SETINDEX, S_WRITE, S_SORT, S_DO, S_GIVE, S_CALL, S_TEMPLATE, S_SETFIELD } StmtType;

typedef struct {
    StmtType type;
    int line;
    char *name; /* S_THING, S_ASSIGN, S_FOR (loop variable), S_APPEND, S_SETINDEX */
    int want;   /* S_THING: V_LIST for "List of", V_DICT for "Book of", 0 for neither. S_WRITE: 1 for add */
    Expr index; /* S_SETINDEX: which position or Entry. S_WRITE: the file name */
    int has_expr;
    Expr expr;  /* S_THING (optional), S_ASSIGN, S_SAY, S_FOR (what to loop over), S_IF (condition) */
    int taken;       /* S_IF: 0 = not run yet, 1 = condition was Yes, 2 = No */
    size_t link;     /* S_IFNOT: index of the S_IF it is the other side of */
    int file;        /* which file the step was written in */
    char *target;    /* S_SETFIELD: the name of the object whose field changes */
    size_t body_end; /* S_FOR, S_IF, S_IFNOT, S_DO: the steps after this one, up to here, are the body */
} Stmt;

static Stmt *stmts;
static size_t nstmts, capstmts;
static size_t pos;

/* A function: its inputs, and where its S_DO step is (its body follows that step) */
typedef struct {
    char *name;
    char **params;
    size_t nparams;
    size_t start;
} Func;

static Func *funcs;
static size_t nfuncs;
static int in_do; /* parsing the body of a function, where give is allowed */

/* A template: the inputs it is made from, the fields every object of it has, and its methods. Its
   S_TEMPLATE step is followed by the field steps (run, in a frame of their own, to make an object) and
   by the S_DO steps of its methods, which only run when a method is called. */
typedef struct {
    char *name;
    char **params;
    size_t nparams;
    size_t start;
    char **fields;
    size_t nfields;
    Func *methods;
    size_t nmethods;
} Template;

static Template *templates;
static size_t ntemplates;
static int cur_template = -1; /* parsing a template's body, where Create makes a method */

static const char *template_name(int index) {
    return templates[index].name;
}

static int find_template(const char *name) {
    for (size_t i = 0; i < ntemplates; i++) {
        if (strcmp(templates[i].name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static Func *find_method(Template *t, const char *name) {
    for (size_t i = 0; i < t->nmethods; i++) {
        if (strcmp(t->methods[i].name, name) == 0) {
            return &t->methods[i];
        }
    }
    return NULL;
}

static Func *find_func(const char *name) {
    for (size_t i = 0; i < nfuncs; i++) {
        if (strcmp(funcs[i].name, name) == 0) {
            return &funcs[i];
        }
    }
    return NULL;
}

static Token *peek(void) { return &tokens[pos]; }
static Token *next(void) {
    Token *t = &tokens[pos];
    if (t->type != T_EOF) {
        pos++;
    }
    return t;
}

static int is_punct(Token *t, char c) {
    return t->type == T_PUNCT && t->text[0] == c;
}

static int is_word(Token *t, const char *w) {
    return t->type == T_WORD && strcmp(t->text, w) == 0;
}

static const char *describe(Token *t) {
    return t->type == T_EOF ? "end of file" : t->text;
}

static void expect_punct(char c) {
    Token *t = next();
    if (!is_punct(t, c)) {
        die(t->line, "expected '%c' but found '%s'", c, describe(t));
    }
}

static Token *expect_type(TokType type, const char *what) {
    Token *t = next();
    if (t->type != type) {
        die(t->line, "expected %s but found '%s'", what, describe(t));
    }
    return t;
}

static Stmt *new_stmt(StmtType type, int line) {
    if (nstmts == capstmts) {
        capstmts = capstmts ? capstmts * 2 : 16;
        stmts = realloc(stmts, capstmts * sizeof *stmts);
        if (stmts == NULL) {
            die(line, "out of memory");
        }
    }
    Stmt *s = &stmts[nstmts++];
    memset(s, 0, sizeof *s);
    s->type = type;
    s->line = line;
    s->file = cur_file;
    return s;
}

static Expr parse_expr(void);
static Expr parse_unary(void);
static Expr parse_primary(void);
static Expr parse_condition(void);
static int is_assign_op(Token *t);
static int is_ifnot(Token *t);
static int in_condition; /* parsing an If's condition, where and/or may join conditions */
static int in_dict_key;  /* reading the Entry name in { Entry: value }, where ':' is not a position */

static Expr *box(Expr e) {
    Expr *p = malloc(sizeof *p);
    if (p == NULL) {
        die(e.line, "out of memory");
    }
    *p = e;
    return p;
}

/* Total, Counted, Smaller, Bigger and Order work on a whole list */
static char data_call(Token *t) {
    if (is_word(t, "Total")) return 't';
    if (is_word(t, "Counted")) return 'c';
    if (is_word(t, "Smaller")) return 'm';
    if (is_word(t, "Bigger")) return 'M';
    if (is_word(t, "Order")) return 'o';
    return 0;
}

/* the flag word "backward", as in Order(scores, backward) */
static Expr parse_backward(void) {
    Token *w = next();
    if (!is_word(w, "backward")) {
        die(w->line, "expected 'backward' but found '%s'", describe(w));
    }
    if (is_punct(peek(), '=')) {
        die(w->line, "write just 'backward', as in Order(scores, backward)");
    }
    Expr e = {0};
    e.type = E_LITERAL;
    e.line = w->line;
    e.lit.type = V_BOOL;
    e.lit.b = 1;
    return e;
}

static void add_arg(Expr *e, Expr arg) {
    e->args = xalloc(e->args, (e->nargs + 1) * sizeof *e->args);
    e->args[e->nargs++] = arg;
}

/* the items of [ ... ] or the Entries of { ... }, up to the closing bracket */
static void parse_args(Expr *e, char close, int pairs) {
    if (is_punct(peek(), close)) {
        next();
        return;
    }
    for (;;) {
        if (pairs) {
            int outer = in_dict_key;
            in_dict_key = 1;
            add_arg(e, parse_expr());
            in_dict_key = outer;
            expect_punct(':');
            add_arg(e, parse_expr());
        } else {
            add_arg(e, parse_expr());
        }
        Token *t = next();
        if (is_punct(t, close)) {
            return;
        }
        if (!is_punct(t, ',')) {
            die(t->line, "expected ',' or '%c' but found '%s'", close, describe(t));
        }
        if (is_punct(peek(), close)) { /* a trailing comma is fine */
            next();
            return;
        }
    }
}

/* The pieces of "Hello {name}.": text, and the values to put between. {{ and }} are braces. */
static Expr parse_text_value(const char *src, int line) {
    Token *saved_tokens = tokens;
    size_t saved_n = ntokens, saved_cap = captokens, saved_pos = pos;
    int saved_cond = in_condition, saved_key = in_dict_key;
    tokens = NULL;
    ntokens = captokens = pos = 0;
    in_condition = in_dict_key = 0;
    first_line = line;
    allow_notes = 0;
    tokenize(src);
    allow_notes = 1;
    first_line = 1;
    Expr inside = parse_expr();
    if (peek()->type != T_EOF) {
        die(line, "only one value can go inside { } in text, but found '%s'", describe(peek()));
    }
    free(tokens);
    tokens = saved_tokens;
    ntokens = saved_n;
    captokens = saved_cap;
    pos = saved_pos;
    in_condition = saved_cond;
    in_dict_key = saved_key;
    return inside;
}

static void add_text_piece(Expr *e, Buf *text, int line) {
    if (text->len == 0) {
        return;
    }
    Expr piece = {0};
    piece.type = E_LITERAL;
    piece.line = line;
    piece.lit.type = V_STR;
    piece.lit.str = text->s;
    add_arg(e, piece);
    *text = (Buf){0};
}

static void parse_interp(Expr *e, const char *src, int line) {
    e->type = E_INTERP;
    Buf text = {0};
    for (const char *p = src; *p;) {
        if ((p[0] == '{' && p[1] == '{') || (p[0] == '}' && p[1] == '}')) {
            buf_add(&text, p, 1);
            p += 2;
        } else if (*p == '{') {
            const char *close = strchr(p, '}');
            if (close == NULL) {
                die(line, "a '{' in text needs a '}' to close it (and no quotes can go inside the braces); write {{ for a brace itself");
            }
            add_text_piece(e, &text, line);
            char *inside = copy_str(p + 1, (size_t)(close - p - 1));
            add_arg(e, parse_text_value(inside, line));
            free(inside);
            p = close + 1;
        } else {
            buf_add(&text, p, 1);
            p++;
        }
    }
    add_text_piece(e, &text, line);
}

/* A ':' picks a position (numbers:3). It never starts a body; that is the ','. */
static int at_colon_index(void) {
    Token *t = peek();
    if (!is_punct(t, ':') || in_dict_key) {
        return 0;
    }
    if (!tokens[pos + 1].glued) {
        die(t->line, "':' picks a position, as in numbers:3 (no spaces); a body starts after a ','");
    }
    return 1;
}

/* atom: number | "string" | Yes | No | nil | ask("prompt") | number(expr)
          | read(file) | lines(file) | split(text, separator) | exists(file)
          | round(number [, places]) | fly(number [, places]) | sink(number [, places])
          | range(end) | range(start, end [, step])
          | Total(list) | Counted(list) | Smaller(list) | Bigger(list) | Order(list [, backward])
          | [items] | {Entry: value, ...} | '(' expr ')'
          | name | field of object | Do name(inputs), a call to a function you created
          | Do method(inputs) on object */
static Expr parse_atom(void) {
    Token *t = next();
    Expr e = {0};
    e.line = t->line;
    e.type = E_LITERAL;
    if (t->type == T_NUMBER) {
        e.lit.type = V_NUM;
        e.lit.num = strtod(t->text, NULL);
    } else if (t->type == T_STRING) {
        if (strchr(t->text, '{') != NULL || strstr(t->text, "}}") != NULL) {
            parse_interp(&e, t->text, t->line);
        } else {
            e.lit.type = V_STR;
            e.lit.str = t->text;
        }
    } else if (is_word(t, "Yes") || is_word(t, "No")) {
        e.lit.type = V_BOOL;
        e.lit.b = t->text[0] == 'Y';
    } else if (is_word(t, "nil")) {
        e.lit.type = V_NIL;
    } else if (is_word(t, "ask")) {
        e.type = E_ASKS;
        expect_punct('(');
        e.prompt = expect_type(T_STRING, "a prompt string")->text;
        expect_punct(')');
    } else if (is_word(t, "number") && is_punct(peek(), '(')) {
        next();
        e.type = E_TONUM;
        e.left = box(parse_expr());
        expect_punct(')');
    } else if (is_word(t, "Sort") && is_punct(peek(), '(')) {
        die(t->line, "Sort puts a list in order where it is; use Order(...) to get an ordered copy");
    } else if (data_call(t) && is_punct(peek(), '(')) {
        next();
        e.type = E_BUILTIN;
        e.op = data_call(t);
        add_arg(&e, parse_expr());
        if (e.op == 'o' && is_punct(peek(), ',')) {
            next();
            add_arg(&e, parse_backward());
        }
        expect_punct(')');
    } else if ((is_word(t, "floor") || is_word(t, "ceiling") || is_word(t, "ceil")) && is_punct(peek(), '(')) {
        if (is_word(t, "floor")) {
            die(t->line, "there is no floor(...) in Thing; sink(...) rounds down");
        }
        die(t->line, "there is no %s(...) in Thing; fly(...) rounds up", t->text);
    } else if ((is_word(t, "round") || is_word(t, "range") || is_word(t, "fly") || is_word(t, "sink")) &&
               is_punct(peek(), '(')) {
        next();
        e.type = E_BUILTIN;
        e.op = is_word(t, "round") ? 'R' : is_word(t, "range") ? 'G' : is_word(t, "fly") ? 'U' : 'D';
        parse_args(&e, ')', 0);
        if (e.op != 'G' && (e.nargs < 1 || e.nargs > 2)) {
            die(t->line, "%s takes a number, and how many places to keep if you like, as in %s(x, 1)", t->text, t->text);
        }
        if (e.op == 'G' && (e.nargs < 1 || e.nargs > 3)) {
            die(t->line, "range takes an end, or a start and an end, or a start, an end and a step");
        }
    } else if ((is_word(t, "read") || is_word(t, "lines") || is_word(t, "split") || is_word(t, "exists")) &&
               is_punct(peek(), '(')) {
        next();
        e.type = E_BUILTIN;
        e.op = t->text[0] == 'r' ? 'r' : t->text[0] == 'l' ? 'l' : t->text[0] == 'e' ? 'x' : 's';
        add_arg(&e, parse_expr());
        if (e.op == 's') {
            expect_punct(',');
            add_arg(&e, parse_expr());
        }
        expect_punct(')');
    } else if (is_punct(t, '[')) {
        e.type = E_LIST;
        parse_args(&e, ']', 0);
    } else if (is_punct(t, '{')) {
        e.type = E_DICT;
        parse_args(&e, '}', 1);
    } else if (is_punct(t, '(')) {
        e = in_condition ? parse_condition() : parse_expr();
        expect_punct(')');
    } else if (is_word(t, "Do")) {
        Token *name = expect_type(T_WORD, "the name of a function after Do");
        if (!is_punct(peek(), '(')) {
            die(name->line, "call a function as Do %s(...), with its inputs in parentheses", name->text);
        }
        next();
        e.type = E_CALL;
        e.name = name->text;
        parse_args(&e, ')', 0);
        if (is_word(peek(), "on")) { /* Do add(5) on c  -- a method, run on an object */
            next();
            e.type = E_METHOD;
            e.left = box(parse_primary());
        }
    } else if (t->type == T_WORD && is_punct(peek(), '(')) {
        die(t->line, "'%s(...)' is not built in; to run a function you created, write Do %s(...)", t->text, t->text);
    } else if (t->type == T_WORD && is_word(peek(), "of")) { /* count of c  -- a field of an object */
        next();
        e.type = E_FIELD;
        e.name = t->text;
        e.left = box(parse_primary());
    } else if (t->type == T_WORD) {
        e.type = E_VAR;
        e.name = t->text;
    } else {
        die(t->line, "expected a value but found '%s'", describe(t));
    }
    return e;
}

/* primary: an atom, optionally followed by [position] or :position (any number of times) */
static Expr parse_primary(void) {
    Expr e = parse_atom();
    for (;;) {
        Token *p = peek();
        Expr where;
        if (is_punct(p, '[')) {
            next();
            where = parse_expr();
            expect_punct(']');
        } else if (at_colon_index()) {
            next();
            where = parse_unary();
        } else {
            break;
        }
        Expr ix = {0};
        ix.type = E_INDEX;
        ix.line = p->line;
        ix.left = box(e);
        ix.right = box(where);
        e = ix;
    }
    return e;
}

/* unary: '-' unary | primary */
static Expr parse_unary(void) {
    if (is_punct(peek(), '-')) {
        Token *op = next();
        Expr e = {0};
        e.type = E_NEG;
        e.line = op->line;
        e.left = box(parse_unary());
        return e;
    }
    return parse_primary();
}

static Expr make_binary(int line, char op, Expr left, Expr right) {
    Expr e = {0};
    e.type = E_BINARY;
    e.line = line;
    e.op = op;
    e.left = box(left);
    e.right = box(right);
    return e;
}

/* term: unary (('*' | '/' | '%') unary)* */
static Expr parse_term(void) {
    Expr left = parse_unary();
    while (is_punct(peek(), '*') || is_punct(peek(), '/') || is_punct(peek(), '%')) {
        Token *op = next();
        left = make_binary(op->line, op->text[0], left, parse_unary());
    }
    return left;
}

/* sum: term (('+' | '-') term)*   -- the signs are for numbers */
static Expr parse_sum(void) {
    Expr left = parse_term();
    while (is_punct(peek(), '+') || is_punct(peek(), '-')) {
        Token *op = next();
        left = make_binary(op->line, op->text[0], left, parse_term());
    }
    return left;
}

/* words: sum ((plus | minus) sum)*   -- plus and minus are for words, and bind looser than
   the signs, so "sum: " plus x + y is "sum: " plus (x + y) */
static Expr parse_words(void) {
    Expr left = parse_sum();
    for (;;) {
        Token *t = peek();
        char op;
        if (is_word(t, "plus")) {
            op = OP_PLUS;
        } else if (is_word(t, "minus")) {
            op = OP_MINUS;
        } else {
            return left;
        }
        next();
        left = make_binary(t->line, op, left, parse_sum());
    }
}

/* expression: words ((is | is not | is bigger-than | is smaller-than | in) words)? */
static Expr parse_expr(void) {
    Expr left = parse_words();
    Token *t = peek();
    if (is_punct(t, '<') || is_punct(t, '>')) {
        die(t->line, "'%c' no longer compares; write is %s", t->text[0], t->text[0] == '>' ? "bigger-than" : "smaller-than");
    }
    if (is_word(t, "is")) {
        next();
        if (is_word(peek(), "not")) {
            next();
            return make_binary(t->line, OP_NOTEQ, left, parse_words());
        }
        if (is_word(peek(), "bigger-than") || is_word(peek(), "smaller-than")) {
            char op = peek()->text[0] == 'b' ? OP_BIGGER : OP_SMALLER;
            next();
            return make_binary(t->line, op, left, parse_words());
        }
        return make_binary(t->line, OP_EQUALS, left, parse_words());
    }
    if (is_word(t, "Is") || is_word(t, "IsNot")) {
        die(t->line, "comparing is lowercase now: is, is not, is bigger-than, is smaller-than");
    }
    if (is_word(t, "in")) {
        next();
        return make_binary(t->line, OP_IN, left, parse_words());
    }
    if (is_word(t, "exists")) {
        die(t->line, "exists is written like a call now: exists(file)");
    }
    if (is_punct(t, '=') || is_word(t, "equals")) {
        die(t->line, "'%s' no longer compares; write is", t->text);
    }
    return left;
}

/* not: 'not' not | expr   -- flips a Yes or No; only conditions read it, like and and or */
static Expr parse_not(void) {
    if (is_word(peek(), "Not")) {
        die(peek()->line, "Not is lowercase now: not");
    }
    if (is_word(peek(), "not")) {
        Token *t = next();
        Expr e = {0};
        e.type = E_NOT;
        e.line = t->line;
        e.left = box(parse_not());
        return e;
    }
    return parse_expr();
}

/* condition: all ('or' all)*   all: not ('and' not)*
   Only an If (or an "instead,") reads these, so "and" and "or" elsewhere keep their other jobs. */
static Expr parse_condition(void) {
    int outer = in_condition;
    in_condition = 1;
    Expr left = parse_not();
    for (;;) {
        Token *t = peek();
        if (is_word(t, "and")) {
            next();
            Expr right = parse_not();
            /* "and" binds tighter than "or", so fold a run of them first */
            while (is_word(peek(), "and")) {
                Token *again = next();
                right = make_binary(again->line, OP_AND, right, parse_not());
            }
            left = make_binary(t->line, OP_AND, left, right);
        } else if (is_word(t, "or")) {
            next();
            Expr right = parse_not();
            while (is_word(peek(), "and")) {
                Token *again = next();
                right = make_binary(again->line, OP_AND, right, parse_not());
            }
            left = make_binary(t->line, OP_OR, left, right);
        } else {
            break;
        }
    }
    in_condition = outer;
    return left;
}

static int is_assign_op(Token *t) {
    return is_word(t, "holds");
}

/* '=', equals and is used to assign; say what to write instead */
static int is_old_assign(Token *t) {
    return is_word(t, "equals") || is_word(t, "is") || is_punct(t, '=');
}

static void reject_old_assign(Token *t) {
    if (is_old_assign(t)) {
        die(t->line, "'%s' no longer assigns; write 'holds', as in x holds 5", t->text);
    }
}

/* A <name>  |  A <name> holds <expr>  |  A <name> ask("prompt")   (An works the same) */
static void parse_thing(Token *kw) {
    Stmt *s = new_stmt(S_THING, kw->line);
    s->name = expect_type(T_WORD, "a variable name")->text;
    reject_old_assign(peek());
    if (is_assign_op(peek())) {
        next();
        s->has_expr = 1;
        s->expr = parse_expr();
    } else if (is_word(peek(), "ask")) {
        s->has_expr = 1;
        s->expr = parse_expr();
    }
}

/* List-of <name> [is <expr>]  |  Book-of <name> [is <expr>] */
static void parse_collection(Token *kw, int kind) {
    Stmt *s = new_stmt(S_THING, kw->line);
    s->name = expect_type(T_WORD, "a name")->text;
    s->want = kind;
    reject_old_assign(peek());
    if (is_assign_op(peek())) {
        next();
        s->has_expr = 1;
        s->expr = parse_expr();
    }
}

/* Sort(<list name> [, backward])  -- puts the list itself in order */
static void parse_sort(Token *kw) {
    Stmt *s = new_stmt(S_SORT, kw->line);
    expect_punct('(');
    s->name = expect_type(T_WORD, "the name of a list")->text;
    if (is_punct(peek(), ',')) {
        next();
        s->has_expr = 1;
        s->expr = parse_backward();
    }
    expect_punct(')');
}

/* write <expr> in <file>  |  add <expr> in <file> */
static void parse_write(Token *kw, int append) {
    Stmt *s = new_stmt(S_WRITE, kw->line);
    s->want = append;
    s->has_expr = 1;
    s->expr = parse_words(); /* not parse_expr, which would read "in" as a comparison */
    Token *in = next();
    if (is_word(in, "to")) {
        die(in->line, "write and add use 'in' now, as in write x in \"file.txt\"");
    }
    if (!is_word(in, "in")) {
        die(in->line, "expected 'in' but found '%s'", describe(in));
    }
    s->index = parse_expr();
}

/* append(<list name>, <expr>)  -- adds to the end of a list */
static void parse_append(Token *kw) {
    Stmt *s = new_stmt(S_APPEND, kw->line);
    expect_punct('(');
    s->name = expect_type(T_WORD, "the name of a list")->text;
    expect_punct(',');
    s->has_expr = 1;
    s->expr = parse_expr();
    expect_punct(')');
}

/* <name>[where] is <expr>  |  <name>:where is <expr> */
static void parse_setindex(Token *name) {
    Stmt *s = new_stmt(S_SETINDEX, name->line);
    s->name = name->text;
    if (is_punct(peek(), '[')) {
        next();
        s->index = parse_expr();
        expect_punct(']');
    } else {
        next(); /* : */
        s->index = parse_unary();
    }
    reject_old_assign(peek());
    if (!is_assign_op(peek())) {
        die(peek()->line, "expected 'is' but found '%s'", describe(peek()));
    }
    next();
    s->has_expr = 1;
    s->expr = parse_expr();
}

/* <name> is <expr> */
static void parse_assign(Token *name) {
    Stmt *s = new_stmt(S_ASSIGN, name->line);
    s->name = name->text;
    next(); /* is */
    s->has_expr = 1;
    s->expr = parse_expr();
}

/* say(<expr>)  -- prints any value */
static void parse_say(Token *kw) {
    Stmt *s = new_stmt(S_SAY, kw->line);
    if (!is_punct(peek(), '(')) {
        die(kw->line, "print with say(value), as in say(x) or say(\"text\")");
    }
    next();
    if (is_word(peek(), "value-of")) {
        die(peek()->line, "say(...) prints any value as it is, so value-of is not needed");
    }
    s->has_expr = 1;
    s->expr = parse_expr();
    expect_punct(')');
}

static void parse_step(void);

/* A body is a step followed by any number of ", step"s.
   It stops at "and" or the '.', which is how a statement gets back out of it. */
static int at_elseif(void) {
    return is_word(peek(), "instead") && is_punct(&tokens[pos + 1], ',');
}

static void parse_body(int is_if_body) {
    parse_step();
    /* An if's body doesn't take ", if-not" or "instead,": those belong to the level above,
       which is what lets an if/if-not pair or an else-if chain sit inside a loop body. */
    for (;;) {
        if (at_elseif()) {
            if (is_if_body) {
                break;
            }
            parse_step();
        } else if (is_punct(peek(), ',') && !(is_if_body && is_ifnot(&tokens[pos + 1]))) {
            next();
            parse_step();
        } else {
            break;
        }
    }
}

/* for every <name> in <expr> ',' body */
static void parse_for(Token *kw) {
    size_t at = nstmts;
    Stmt *s = new_stmt(S_FOR, kw->line);
    Token *every = next();
    if (!is_word(every, "every")) {
        die(every->line, "expected 'every' but found '%s'", describe(every));
    }
    s->name = expect_type(T_WORD, "a variable name")->text;
    Token *in = next();
    if (!is_word(in, "in")) {
        die(in->line, "expected 'in' but found '%s'", describe(in));
    }
    s->has_expr = 1;
    s->expr = parse_expr();
    Token *sep = next();
    if (!is_punct(sep, ',')) {
        die(sep->line, "expected ',' before the body but found '%s'", describe(sep));
    }
    parse_body(0);
    stmts[at].body_end = nstmts; /* stmts may have moved while parsing the body */
}

/* if-not (or If-not at the start of a statement) */
static int is_ifnot(Token *t) {
    return is_word(t, "if-not") || is_word(t, "If-not");
}

static long last_if = -1; /* the most recent if, which an if-not belongs to */

/* if <expr> [','] body */
static void parse_if(Token *kw) {
    size_t at = nstmts;
    Stmt *s = new_stmt(S_IF, kw->line);
    s->has_expr = 1;
    s->expr = parse_condition();
    if (is_punct(peek(), ',')) {
        next();
    }
    parse_body(1);
    stmts[at].body_end = nstmts;
    last_if = (long)at; /* set after the body so an If inside it doesn't take over */
}

/* if-not [','] body  -- runs when the most recent if was No */
static void parse_ifnot(Token *kw) {
    if (last_if < 0) {
        die(kw->line, "'if-not' needs an if before it");
    }
    size_t at = nstmts;
    Stmt *s = new_stmt(S_IFNOT, kw->line);
    s->link = (size_t)last_if;
    if (is_punct(peek(), ',')) {
        next();
    }
    parse_body(0);
    stmts[at].body_end = nstmts;
}

/* "instead," <expr> [','] body  -- an else-if: the same as if-not followed by if. */
static void parse_instead(Token *kw) {
    if (!is_punct(peek(), ',')) {
        die(kw->line, "an else-if is written \"instead,\" (with the comma after)");
    }
    next();
    if (last_if < 0) {
        die(kw->line, "'instead' needs an If before it");
    }
    size_t at = nstmts;
    Stmt *s = new_stmt(S_IFNOT, kw->line);
    s->link = (size_t)last_if;
    parse_if(kw);
    stmts[at].body_end = nstmts;
}

/* step keywords may start with a capital, since they can start a statement */
static int is_keyword(Token *t, const char *lower) {
    if (t->type != T_WORD || tolower((unsigned char)t->text[0]) != lower[0]) {
        return 0;
    }
    return strcmp(t->text + 1, lower + 1) == 0;
}

/* give <expr>  -- hands a value back from a function and leaves it */
static void parse_give(Token *kw) {
    if (!in_do) {
        die(kw->line, "give only works inside a function made with Create");
    }
    Stmt *s = new_stmt(S_GIVE, kw->line);
    s->has_expr = 1;
    s->expr = parse_expr();
}

/* Do name(inputs)  -- runs a function and ignores what it gives back */
static void parse_call_step(Token *name) {
    pos--; /* back to the name, so the whole call is read as a value */
    Expr call = parse_expr();
    if (call.type != E_CALL && call.type != E_METHOD) {
        die(name->line, "unknown step '%s'", name->text);
    }
    Stmt *s = new_stmt(S_CALL, name->line);
    s->has_expr = 1;
    s->expr = call;
}

static char parse_statement(void);

static const char *taken_names[] = {"ask", "number", "read", "lines", "split", "Total", "Counted", "Smaller", "Bigger",
                                    "Order", "Sort", "say", "write", "add", "give", "Do", "Create", "template", "on", "of", "self", "outsource", "A", "An", "holds", "not", "if", "If", "for",
                                    "For", "IfNot", "instead", "append", "exists", "Not", "round", "range", "fly", "sink", "floor",
                                    "ceiling", "ceil", NULL};

/* Create name(a, b), body!   -- the body is statements, and the '!' ends the last one */
static void parse_create(Token *kw) {
    Token *name = expect_type(T_WORD, "a name for the function");
    for (int i = 0; taken_names[i] && cur_template < 0; i++) { /* a method is always called with Do, so any word will do */
        if (strcmp(name->text, taken_names[i]) == 0) {
            die(name->line, "'%s' is already a word in Thing; pick another name for the function", name->text);
        }
    }
    if (find_template(name->text) >= 0 || (cur_template < 0 && find_func(name->text) != NULL)) {
        die(name->line, "there is already a function or template named '%s'", name->text);
    }
    if (cur_template >= 0 && find_method(&templates[cur_template], name->text) != NULL) {
        die(name->line, "the template '%s' already has a method named '%s'", templates[cur_template].name, name->text);
    }
    expect_punct('(');
    char **params = NULL;
    size_t nparams = 0;
    if (is_punct(peek(), ')')) {
        next();
    } else {
        for (;;) {
            Token *p = expect_type(T_WORD, "an input name");
            params = xalloc(params, (nparams + 1) * sizeof *params);
            params[nparams++] = p->text;
            Token *t = next();
            if (is_punct(t, ')')) {
                break;
            }
            if (!is_punct(t, ',')) {
                die(t->line, "expected ',' or ')' but found '%s'", describe(t));
            }
        }
    }
    expect_punct(','); /* the body starts here, as after If and For every */
    size_t at = nstmts;
    if (cur_template >= 0) {
        Template *tp = &templates[cur_template];
        tp->methods = xalloc(tp->methods, (tp->nmethods + 1) * sizeof *tp->methods);
        tp->methods[tp->nmethods++] = (Func){name->text, params, nparams, at};
    } else {
        funcs = xalloc(funcs, (nfuncs + 1) * sizeof *funcs);
        funcs[nfuncs++] = (Func){name->text, params, nparams, at};
    }
    Stmt *s = new_stmt(S_DO, kw->line);
    s->name = name->text;
    in_do = 1;
    for (;;) {
        if (peek()->type == T_EOF) {
            die(kw->line, "the function '%s' needs a '!' to end it", name->text);
        }
        if (parse_statement() == '!') {
            break;
        }
    }
    in_do = 0;
    stmts[at].body_end = nstmts;
}

static void add_field(Template *t, char *name) {
    for (size_t i = 0; i < t->nfields; i++) {
        if (strcmp(t->fields[i], name) == 0) {
            return;
        }
    }
    t->fields = xalloc(t->fields, (t->nfields + 1) * sizeof *t->fields);
    t->fields[t->nfields++] = name;
}

/* template Name(inputs),
       A field holds value.
       Create method(inputs), ...!
   !
   The fields are made each time an object is made, from the inputs. A '!' on its own ends the template,
   and so does a field statement that ends with '!'. */
static void parse_template(Token *kw) {
    Token *name = expect_type(T_WORD, "a name for the template");
    for (int i = 0; taken_names[i]; i++) {
        if (strcmp(name->text, taken_names[i]) == 0) {
            die(name->line, "'%s' is already a word in Thing; pick another name for the template", name->text);
        }
    }
    if (find_func(name->text) != NULL || find_template(name->text) >= 0) {
        die(name->line, "there is already a function or template named '%s'", name->text);
    }
    expect_punct('(');
    char **params = NULL;
    size_t nparams = 0;
    if (is_punct(peek(), ')')) {
        next();
    } else {
        for (;;) {
            Token *p = expect_type(T_WORD, "an input name");
            params = xalloc(params, (nparams + 1) * sizeof *params);
            params[nparams++] = p->text;
            Token *t = next();
            if (is_punct(t, ')')) {
                break;
            }
            if (!is_punct(t, ',')) {
                die(t->line, "expected ',' or ')' but found '%s'", describe(t));
            }
        }
    }
    expect_punct(',');
    templates = xalloc(templates, (ntemplates + 1) * sizeof *templates);
    int index = (int)ntemplates++;
    templates[index] = (Template){.name = name->text, .params = params, .nparams = nparams, .start = nstmts};
    size_t at = nstmts;
    Stmt *s = new_stmt(S_TEMPLATE, kw->line);
    s->name = name->text;
    cur_template = index;
    for (;;) {
        if (peek()->type == T_EOF) {
            die(kw->line, "the template '%s' needs a '!' on its own to end it", name->text);
        }
        if (is_punct(peek(), '!')) {
            next();
            break;
        }
        if (is_word(peek(), "Create")) {
            parse_create(next());
            continue;
        }
        size_t keep = nstmts;
        char end = parse_statement();
        for (size_t k = keep; k < nstmts; k++) {
            if (stmts[k].type != S_THING) {
                die(stmts[k].line, "only fields (A name holds value) and Create methods go in a template");
            }
            add_field(&templates[index], stmts[k].name);
        }
        if (end == '!') {
            break;
        }
    }
    cur_template = -1;
    stmts[at].body_end = nstmts;
}

/* <field> of <name> holds <expr>  -- changes a field of an object */
static void parse_setfield(Token *field) {
    Stmt *s = new_stmt(S_SETFIELD, field->line);
    s->name = field->text;
    next(); /* of */
    s->target = expect_type(T_WORD, "the name of an object")->text;
    reject_old_assign(peek());
    if (!is_assign_op(peek())) {
        die(peek()->line, "expected 'holds' but found '%s'", describe(peek()));
    }
    next();
    s->has_expr = 1;
    s->expr = parse_expr();
}

static void parse_step(void) {
    Token *t = next();
    if (is_punct(t, '!')) {
        die(t->line, "this '!' has nothing to end: it closes a function made with Create or a template, and none is open here");
    } else if (is_word(t, "outsource")) {
        die(t->line, "outsource is written on its own, at the start of a statement: outsource \"file.thing\".");
    } else if (is_word(t, "template")) {
        die(t->line, "a template is made on its own, at the start of a statement, with template Name(...), ...!");
    } else if (is_word(t, "Create")) {
        die(t->line, "a function is made on its own, at the start of a statement, with Create name(...), ...!");
    } else if (is_word(t, "Do")) {
        parse_call_step(t);
    } else if (is_keyword(t, "give") && !is_assign_op(peek())) {
        parse_give(t);
    } else if ((is_word(t, "A") || is_word(t, "An")) && peek()->type == T_WORD && !is_assign_op(peek())) {
        parse_thing(t);
    } else if (is_word(t, "Thing")) {
        die(t->line, "Thing is now A, or An before a vowel: write A name holds value, as in A age holds 30");
    } else if (is_keyword(t, "say")) {
        parse_say(t);
    } else if (is_keyword(t, "for")) {
        parse_for(t);
    } else if (is_keyword(t, "if")) {
        parse_if(t);
    } else if (is_word(t, "IfNot")) {
        die(t->line, "IfNot is written if-not now, with a dash and lowercase");
    } else if (is_ifnot(t)) {
        parse_ifnot(t);
    } else if (is_word(t, "instead")) {
        parse_instead(t);
    } else if (is_word(t, "or")) {
        die(t->line, "'or' joins conditions inside an If; an else-if is written \"instead,\"");
    } else if (is_word(t, "Sort") && is_punct(peek(), '(')) {
        parse_sort(t);
    } else if (is_word(t, "append") && is_punct(peek(), '(')) {
        parse_append(t);
    } else if ((is_keyword(t, "write") || is_keyword(t, "add")) && !is_assign_op(peek())) {
        parse_write(t, tolower((unsigned char)t->text[0]) == 'a');
    } else if (t->type == T_WORD && is_punct(peek(), '(')) {
        parse_call_step(t);
    } else if (is_word(t, "List-of")) {
        parse_collection(t, V_LIST);
    } else if (is_word(t, "Book-of")) {
        parse_collection(t, V_DICT);
    } else if ((is_word(t, "List") || is_word(t, "Book")) && is_word(peek(), "of")) {
        die(t->line, "write %s-of, joined with a dash", t->text);
    } else if (t->type == T_WORD && is_word(peek(), "of")) {
        parse_setfield(t);
    } else if (t->type == T_WORD && is_punct(peek(), '@')) {
        die(t->line, "add to a list with append(%s, value) now", t->text);
    } else if (t->type == T_WORD && (is_punct(peek(), '[') || at_colon_index())) {
        parse_setindex(t);
    } else if (t->type == T_WORD && is_old_assign(peek())) {
        reject_old_assign(peek());
    } else if (t->type == T_WORD && is_assign_op(peek())) {
        parse_assign(t);
    } else {
        die(t->line, "unknown step '%s'", describe(t));
    }
}

/* statement: step (',' step)* ('.' | '!')
   The '.' ends the statement and leaves any if or loop it was inside. The '!' does the same
   and also ends the function that the statement is in; it returns which of the two it was. */
static char parse_statement(void) {
    parse_step();
    while (is_punct(peek(), ',') || at_elseif()) {
        if (!at_elseif()) {
            next();
        }
        parse_step();
    }
    if (is_word(peek(), "and")) {
        die(peek()->line, "'and' only joins conditions inside an If; end the statement with '.' and start a new one");
    }
    Token *end = next();
    if (!is_punct(end, '.') && !is_punct(end, '!')) {
        die(end->line, "expected '.' but found '%s'", describe(end));
    }
    return end->text[0];
}

static char *slurp(int line, const char *path);
static void load_library(const char *written, int line);

/* outsource "file.thing".  -- brings in the functions that file makes with Create */
static void parse_outsource(Token *kw) {
    Token *name = expect_type(T_STRING, "a file name in quotes, as in outsource \"statistics.thing\"");
    Token *end = next();
    if (!is_punct(end, '.')) {
        die(end->line, "expected '.' but found '%s'", describe(end));
    }
    load_library(name->text, kw->line);
}

/* program: statement* */
static void parse_program(void) {
    while (peek()->type != T_EOF) {
        if (is_word(peek(), "Create")) {
            parse_create(next());
        } else if (is_word(peek(), "template")) {
            parse_template(next());
        } else if (is_word(peek(), "outsource")) {
            parse_outsource(next());
        } else {
            Token *start = peek();
            if (parse_statement() == '!') {
                die(start->line, "a '!' ends a function, and none is open here; make one with Create name(...), ...!");
            }
        }
    }
}

/* The files packed into a built program, found by the name written in the outsource. */
static struct {
    char *name;
    char *text;
} bundle[MAX_FILES];
static int nbundle;

/* A relative file name is looked for beside the file that outsources it. */
static char *resolve_import(const char *written) {
    const char *base = file_paths[cur_file];
    const char *slash = (base != NULL && written[0] != '/') ? strrchr(base, '/') : NULL;
    if (slash == NULL) {
        return copy_str(written, strlen(written));
    }
    size_t dirlen = (size_t)(slash - base) + 1;
    char *out = xalloc(NULL, dirlen + strlen(written) + 1);
    memcpy(out, base, dirlen);
    strcpy(out + dirlen, written);
    return out;
}

/* Reads another file and keeps only its functions: whatever else it does at the top level
   (a demo, say) is checked for mistakes but not run. Outsourcing a file twice does nothing. */
static void load_library(const char *written, int line) {
    if (nfiles >= MAX_FILES) {
        die(line, "too many files outsourced (the limit is %d)", MAX_FILES - 1);
    }
    char *path = resolve_import(written);
    for (int i = 0; i < nfiles; i++) {
        if (file_paths[i] != NULL && strcmp(file_paths[i], path) == 0) {
            free(path);
            return;
        }
    }
    char *text = NULL;
    for (int i = 0; i < nbundle; i++) {
        if (strcmp(bundle[i].name, written) == 0) {
            text = copy_str(bundle[i].text, strlen(bundle[i].text));
        }
    }
    if (text == NULL) {
        text = slurp(line, path);
    }
    int id = nfiles++;
    file_names[id] = copy_str(written, strlen(written));
    file_paths[id] = path;
    file_text[id] = text;

    Token *saved_tokens = tokens;
    size_t saved_n = ntokens, saved_cap = captokens, saved_pos = pos;
    int saved_file = cur_file, saved_err = err_file, saved_do = in_do;
    long saved_if = last_if;
    tokens = NULL;
    ntokens = captokens = pos = 0;
    cur_file = err_file = id;
    in_do = 0;
    last_if = -1;
    first_line = 1;
    tokenize(text);
    while (peek()->type != T_EOF) {
        if (is_word(peek(), "Create")) {
            parse_create(next());
        } else if (is_word(peek(), "template")) {
            parse_template(next());
        } else if (is_word(peek(), "outsource")) {
            parse_outsource(next());
        } else {
            Token *start = peek();
            size_t keep = nstmts;
            if (parse_statement() == '!') {
                die(start->line, "a '!' ends a function, and none is open here; make one with Create name(...), ...!");
            }
            nstmts = keep; /* only the functions are kept */
        }
    }
    free(tokens);
    tokens = saved_tokens;
    ntokens = saved_n;
    captokens = saved_cap;
    pos = saved_pos;
    cur_file = saved_file;
    err_file = saved_err;
    in_do = saved_do;
    last_if = saved_if;
}

/* ---------- interpreter ---------- */

typedef struct {
    char *name;
    Value value;
} Var;

/* frames[0] holds the program's own Things. Every call of a function gets one more frame, so its
   inputs and Things are its own. It can read frames[0] but never change it. */
typedef struct {
    Var *vars;
    size_t n, cap;
    int tmpl1; /* 1 + the template whose method runs in this frame, or 0 */
} Frame;

#define MAX_DEPTH 200
static Frame frames[MAX_DEPTH + 1];
static int depth;

static Var *find_in(Frame *f, const char *name) {
    for (size_t i = 0; i < f->n; i++) {
        if (strcmp(f->vars[i].name, name) == 0) {
            return &f->vars[i];
        }
    }
    return NULL;
}

static Var *find_var(const char *name) {
    Var *v = find_in(&frames[depth], name);
    if (v == NULL && depth > 0) {
        v = find_in(&frames[0], name);
    }
    return v;
}

/* For changing a name's value: it has to be one of this frame's own. */
static Var *find_var_mut(int line, const char *name) {
    Var *v = find_in(&frames[depth], name);
    if (v != NULL) {
        return v;
    }
    if (depth > 0 && find_in(&frames[0], name) != NULL) {
        die(line, "'%s' belongs outside this function, which can read it but not change it; send results out with give", name);
    }
    die(line, "'%s' is not defined (make it first, as in A %s holds 0)", name, name);
    return NULL;
}

/* Takes ownership of value. Declares the variable if it doesn't exist yet. */
static void declare_var(int line, const char *name, Value value) {
    (void)line;
    Frame *f = &frames[depth];
    Var *v = find_in(f, name);
    if (v == NULL) {
        if (f->n == f->cap) {
            f->cap = f->cap ? f->cap * 2 : 8;
            f->vars = xalloc(f->vars, f->cap * sizeof *f->vars);
        }
        v = &f->vars[f->n++];
        v->name = copy_str(name, strlen(name));
    } else {
        free_value(v->value);
    }
    v->value = value;
}

static void free_frame(Frame *f) {
    for (size_t i = 0; i < f->n; i++) {
        free(f->vars[i].name);
        free_value(f->vars[i].value);
    }
    free(f->vars);
    *f = (Frame){0};
}

static char *read_line(void) {
    char buf[1024];
    if (fgets(buf, sizeof buf, stdin) == NULL) {
        return copy_str("", 0);
    }
    buf[strcspn(buf, "\r\n")] = '\0';
    return copy_str(buf, strlen(buf));
}

static double need_number(Value v, int line, const char *what) {
    if (v.type != V_NUM) {
        die(line, "%s needs a number", what);
    }
    return v.num;
}

static Value eval(Expr *e);

static char *join_text(Expr *e, Value l, Value r) {
    char *a = value_to_text(l);
    char *b = value_to_text(r);
    size_t la = strlen(a), lb = strlen(b);
    char *s = malloc(la + lb + 1);
    if (s == NULL) {
        die(e->line, "out of memory");
    }
    memcpy(s, a, la);
    memcpy(s + la, b, lb + 1);
    free(a);
    free(b);
    return s;
}

static int values_equal(Value l, Value r) {
    if (l.type != r.type) {
        return 0;
    }
    switch (l.type) {
    case V_NIL: return 1;
    case V_NUM: return l.num == r.num;
    case V_STR: return strcmp(l.str, r.str) == 0;
    case V_BOOL: return l.b == r.b;
    case V_LIST:
        if (l.list->len != r.list->len) {
            return 0;
        }
        for (size_t i = 0; i < l.list->len; i++) {
            if (!values_equal(l.list->items[i], r.list->items[i])) {
                return 0;
            }
        }
        return 1;
    case V_DICT:
    case V_OBJ:
        if (l.type == V_OBJ && l.tmpl != r.tmpl) {
            return 0;
        }
        if (l.dict->len != r.dict->len) {
            return 0;
        }
        for (size_t i = 0; i < l.dict->len; i++) {
            long at = dict_find(r.dict, l.dict->keys[i]);
            if (at < 0 || !values_equal(l.dict->vals[i], r.dict->vals[at])) {
                return 0;
            }
        }
        return 1;
    }
    return 0;
}

static Value eval_binary(Expr *e) {
    Value l = eval(e->left);
    Value r = eval(e->right);
    Value out = {0};
    switch (e->op) {
    case OP_PLUS:
        if (l.type != V_STR && r.type != V_STR) {
            die(e->line, "'plus' is for words; use + for numbers");
        }
        out.type = V_STR;
        out.str = join_text(e, l, r);
        break;
    case OP_MINUS: {
        if (l.type != V_STR || r.type != V_STR) {
            die(e->line, "'minus' is for words; use - for numbers");
        }
        const char *hit = r.str[0] ? strstr(l.str, r.str) : NULL;
        size_t before = hit ? (size_t)(hit - l.str) : strlen(l.str);
        size_t skip = hit ? strlen(r.str) : 0;
        out.type = V_STR;
        out.str = malloc(strlen(l.str) + 1);
        if (out.str == NULL) {
            die(e->line, "out of memory");
        }
        memcpy(out.str, l.str, before);
        strcpy(out.str + before, l.str + before + skip);
        break;
    }
    case OP_BIGGER:
    case OP_SMALLER: {
        int cmp;
        if (l.type == V_NUM && r.type == V_NUM) {
            cmp = (l.num > r.num) - (l.num < r.num);
        } else if (l.type == V_STR && r.type == V_STR) {
            cmp = strcmp(l.str, r.str);
        } else {
            die(e->line, "bigger-than and smaller-than compare two numbers or two words");
        }
        out.type = V_BOOL;
        out.b = e->op == OP_BIGGER ? cmp > 0 : cmp < 0;
        break;
    }
    case OP_IN: {
        int found = 0;
        switch (r.type) {
        case V_LIST:
            for (size_t i = 0; i < r.list->len; i++) {
                if (values_equal(l, r.list->items[i])) {
                    found = 1;
                }
            }
            break;
        case V_DICT:
        case V_OBJ:
            if (l.type != V_STR) {
                die(e->line, "an Entry or a field is named with a word");
            }
            found = dict_find(r.dict, l.str) >= 0;
            break;
        case V_STR:
            if (l.type != V_STR) {
                die(e->line, "'in' looks for a word inside a word");
            }
            found = strstr(r.str, l.str) != NULL;
            break;
        default:
            die(e->line, "'in' looks inside a list, a Book or a word");
        }
        out.type = V_BOOL;
        out.b = found;
        break;
    }
    case OP_EQUALS:
    case OP_NOTEQ:
        out.type = V_BOOL;
        out.b = values_equal(l, r) == (e->op == OP_EQUALS);
        break;
    default: {
        double a = need_number(l, e->line, "this sign (use plus or minus for words)");
        double b = need_number(r, e->line, "this sign (use plus or minus for words)");
        if ((e->op == '/' || e->op == '%') && b == 0) {
            die(e->line, "cannot divide by zero");
        }
        out.type = V_NUM;
        switch (e->op) {
        case '+': out.num = a + b; break;
        case '-': out.num = a - b; break;
        case '*': out.num = a * b; break;
        case '/': out.num = a / b; break;
        case '%': out.num = a - b * (double)(long long)(a / b); break;
        }
    }
    }
    free_value(l);
    free_value(r);
    return out;
}

/* and / or stop as soon as the answer is known, so the right side may not run */
static Value eval_join(Expr *e) {
    int is_and = e->op == OP_AND;
    Value l = eval(e->left);
    if (l.type != V_BOOL) {
        die(e->line, "'%s' joins conditions that are Yes or No", is_and ? "and" : "or");
    }
    if (l.b != is_and) {
        return l; /* No and ..., or Yes or ... */
    }
    Value r = eval(e->right);
    if (r.type != V_BOOL) {
        die(e->line, "'%s' joins conditions that are Yes or No", is_and ? "and" : "or");
    }
    return r;
}

/* turns a position into a place in 0..n-1; a negative position counts from the end */
static long place_in(Expr *e, Value pos_v, size_t n, const char *what) {
    if (pos_v.type != V_NUM || pos_v.num != (double)(long)pos_v.num) {
        die(e->line, "a position in %s is a whole number", what);
    }
    long i = (long)pos_v.num;
    if (i < 0) {
        i += (long)n;
    }
    if (i < 0 || i >= (long)n) {
        die(e->line, "position %ld is outside %s, which has %zu item%s", (long)pos_v.num, what, n, n == 1 ? "" : "s");
    }
    return i;
}

static Value eval_index(Expr *e) {
    Value temp = {0};
    Value *base;
    int owned = 0;
    if (e->left->type == E_VAR) { /* look inside the variable itself, without copying it */
        Var *v = find_var(e->left->name);
        if (v == NULL) {
            die(e->left->line, "'%s' is not defined", e->left->name);
        }
        base = &v->value;
    } else {
        temp = eval(e->left);
        base = &temp;
        owned = 1;
    }
    Value where = eval(e->right);
    Value out = {0};
    switch (base->type) {
    case V_LIST:
        out = copy_value(base->list->items[place_in(e, where, base->list->len, "the list")]);
        break;
    case V_STR: {
        long i = place_in(e, where, strlen(base->str), "the word");
        out = (Value){.type = V_STR, .str = copy_str(base->str + i, 1)};
        break;
    }
    case V_DICT: {
        if (where.type != V_STR) {
            die(e->line, "an Entry is named with a word");
        }
        long at = dict_find(base->dict, where.str);
        if (at < 0) {
            die(e->line, "there is no Entry \"%s\" in the Book", where.str);
        }
        out = copy_value(base->dict->vals[at]);
        break;
    }
    default:
        die(e->line, "only a list, a Book or a word can be looked into");
    }
    free_value(where);
    if (owned) {
        free_value(temp);
    }
    return out;
}

static int cmp_num(const void *a, const void *b) {
    double x = ((const Value *)a)->num, y = ((const Value *)b)->num;
    return (x > y) - (x < y);
}

static int cmp_str(const void *a, const void *b) {
    return strcmp(((const Value *)a)->str, ((const Value *)b)->str);
}

/* Numbers go smallest first and words go alphabetically; a mix can't be ordered. */
static void sort_list(int line, List *l, int backward) {
    ValType kind = l->len ? l->items[0].type : V_NUM;
    if (kind != V_NUM && kind != V_STR) {
        die(line, "only numbers or words can be put in order");
    }
    for (size_t i = 0; i < l->len; i++) {
        if (l->items[i].type != kind) {
            die(line, "only numbers, or only words, can be put in order, not a mix (item %zu is different)", i);
        }
    }
    qsort(l->items, l->len, sizeof *l->items, kind == V_NUM ? cmp_num : cmp_str);
    for (size_t i = 0, j = l->len; backward && i + 1 < j; i++, j--) { /* reverse it */
        Value tmp = l->items[i];
        l->items[i] = l->items[j - 1];
        l->items[j - 1] = tmp;
    }
}

static int backward_option(Expr *e) {
    if (e->nargs < 2) {
        return 0;
    }
    Value b = eval(&e->args[1]);
    if (b.type != V_BOOL) {
        die(e->line, "backward is Yes or No");
    }
    return b.b;
}

/* Total, Counted, Smaller, Bigger, Order */
static Value eval_data(Expr *e) {
    Value a = eval(&e->args[0]);
    Value out = {0};
    if (e->op == 'c') {
        out.type = V_NUM;
        switch (a.type) {
        case V_LIST: out.num = (double)a.list->len; break;
        case V_DICT: out.num = (double)a.dict->len; break;
        case V_OBJ: out.num = (double)a.dict->len; break;
        case V_STR: out.num = (double)strlen(a.str); break;
        default: die(e->line, "Counted counts a list, a Book or a word");
        }
        free_value(a);
        return out;
    }
    /* Total, Smaller and Bigger also work on the values of a Book */
    List book_values = {0};
    if (a.type == V_DICT && e->op != 'o') {
        book_values = (List){a.dict->vals, a.dict->len, a.dict->len};
    } else if (a.type != V_LIST) {
        die(e->line, "%s needs a list", e->op == 't' ? "Total" : e->op == 'm' ? "Smaller" : e->op == 'M' ? "Bigger" : "Order");
    }
    List *l = a.type == V_DICT ? &book_values : a.list;
    if (e->op == 'o') {
        sort_list(e->line, l, backward_option(e));
        return a; /* a is already a private copy */
    }
    if (e->op == 't') {
        out.type = V_NUM;
        for (size_t i = 0; i < l->len; i++) {
            if (l->items[i].type != V_NUM) {
                die(e->line, "Total adds up numbers, and item %zu is not one (use number(...) on words first)", i);
            }
            out.num += l->items[i].num;
        }
    } else { /* Smaller or Bigger */
        if (l->len == 0) {
            die(e->line, "%s needs at least one item in the list", e->op == 'm' ? "Smaller" : "Bigger");
        }
        ValType kind = l->items[0].type;
        if (kind != V_NUM && kind != V_STR) {
            die(e->line, "only numbers or words can be compared");
        }
        size_t best = 0;
        for (size_t i = 0; i < l->len; i++) {
            if (l->items[i].type != kind) {
                die(e->line, "only numbers, or only words, can be compared, not a mix (item %zu is different)", i);
            }
            int cmp = kind == V_NUM ? cmp_num(&l->items[i], &l->items[best]) : cmp_str(&l->items[i], &l->items[best]);
            if (e->op == 'm' ? cmp < 0 : cmp > 0) {
                best = i;
            }
        }
        out = copy_value(l->items[best]);
    }
    free_value(a);
    return out;
}

/* Rounds on the decimal digits, so a half always goes up (away from zero): 2.5 becomes 3, and
   2.675 becomes 2.68 even though a computer stores it as 2.67499999999999982. Negative places
   round to tens, hundreds and so on. The mode picks the direction: 'R' round to the nearest,
   'U' fly (towards bigger numbers), 'D' sink (towards smaller numbers). A number with nothing
   to cut off stays put in every mode. */
static double round_places(double x, long places, char mode) {
    if (x != x || x > 1e15 || x < -1e15) {
        return x;
    }
    double mag = x < 0 ? -x : x;
    double scale = 1;
    for (long i = 0; i < (places < 0 ? -places : places); i++) {
        scale *= 10;
    }
    /* does cutting off the rest make the size of the number grow? */
    int grows_if_any = (mode == 'U' && x > 0) || (mode == 'D' && x < 0);
    char buf[64];
    snprintf(buf, sizeof buf, "%.15g", mag);
    char *dot = strchr(buf, '.');
    double result;
    if (places < 0 || strchr(buf, 'e') != NULL) { /* tens and hundreds, or a very small number */
        double v = places < 0 ? mag / scale : mag * scale;
        double whole = (double)(long long)v;
        if (mode == 'R') {
            whole = (double)(long long)(v + 0.5);
        } else if (v > whole && grows_if_any) {
            whole += 1;
        }
        result = places < 0 ? whole * scale : whole / scale;
    } else if (dot == NULL || (long)strlen(dot + 1) <= places) {
        return x;
    } else {
        char digits[64];
        size_t n = 0;
        for (const char *c = buf; *c; c++) {
            if (*c != '.') {
                digits[n++] = *c;
            }
        }
        size_t intlen = (size_t)(dot - buf);
        size_t keep = intlen + (size_t)places;
        int up;
        if (mode == 'R') {
            up = digits[keep] >= '5';
        } else {
            up = 0;
            for (size_t k = keep; k < n; k++) {
                if (digits[k] != '0') {
                    up = grows_if_any;
                }
            }
        }
        long i = (long)keep - 1;
        for (; up && i >= 0; i--) {
            if (digits[i] == '9') {
                digits[i] = '0';
            } else {
                digits[i]++;
                up = 0;
            }
        }
        char out[80];
        size_t m = 0;
        if (up) { /* 9.96 kept to one place is 10.0 */
            out[m++] = '1';
        }
        for (size_t k = 0; k < intlen; k++) {
            out[m++] = digits[k];
        }
        if (places > 0) {
            out[m++] = '.';
            for (size_t k = intlen; k < keep; k++) {
                out[m++] = digits[k];
            }
        }
        out[m] = '\0';
        result = strtod(out, NULL);
    }
    return x < 0 ? -result : result;
}

static Value eval_round(Expr *e) {
    const char *name = e->op == 'R' ? "round" : e->op == 'U' ? "fly" : "sink";
    Value x = eval(&e->args[0]);
    if (x.type != V_NUM) {
        die(e->line, "%s needs a number", name);
    }
    long places = 0;
    if (e->nargs == 2) {
        Value p = eval(&e->args[1]);
        if (p.type != V_NUM || p.num != (double)(long)p.num) {
            die(e->line, "the places in %s are a whole number", name);
        }
        places = (long)p.num;
    }
    if (places < -15 || places > 15) {
        die(e->line, "%s can keep between -15 and 15 places", name);
    }
    x.num = round_places(x.num, places, e->op);
    return x;
}

/* range(end) is 1 to end. range(start, end) and range(start, end, step) include the end too. */
static Value eval_range(Expr *e) {
    double v[3] = {0};
    for (size_t i = 0; i < e->nargs; i++) {
        Value a = eval(&e->args[i]);
        if (a.type != V_NUM || a.num != (double)(long long)a.num) {
            die(e->line, "range works with whole numbers");
        }
        v[i] = a.num;
    }
    double start = 1, end = v[0], step = 1;
    if (e->nargs >= 2) {
        start = v[0];
        end = v[1];
    }
    if (e->nargs == 3) {
        step = v[2];
    }
    if (step == 0) {
        die(e->line, "range cannot step by 0");
    }
    Value out = new_list();
    for (double x = start; step > 0 ? x <= end : x >= end; x += step) {
        if (out.list->len >= 10000000) {
            die(e->line, "range is too long (the limit is 10000000 numbers)");
        }
        list_push(out.list, (Value){.type = V_NUM, .num = x});
    }
    return out;
}

/* The whole file as one string; the caller frees it. */
static char *slurp(int line, const char *path) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        die(line, "could not open \"%s\"", path);
    }
    Buf b = {0};
    char chunk[4096];
    size_t n;
    while ((n = fread(chunk, 1, sizeof chunk, f)) > 0) {
        buf_add(&b, chunk, n);
    }
    int failed = ferror(f);
    fclose(f);
    if (failed) {
        die(line, "could not read \"%s\"", path);
    }
    return b.s ? b.s : copy_str("", 0);
}

static Value word_value(const char *s, size_t n) {
    return (Value){.type = V_STR, .str = copy_str(s, n)};
}

static Value eval_builtin(Expr *e) {
    if (e->op == 'R' || e->op == 'U' || e->op == 'D') {
        return eval_round(e);
    }
    if (e->op == 'G') {
        return eval_range(e);
    }
    if (e->op == 't' || e->op == 'c' || e->op == 'm' || e->op == 'M' || e->op == 'o') {
        return eval_data(e);
    }
    Value a = eval(&e->args[0]);
    Value out = {0};
    if (a.type != V_STR) {
        die(e->line, e->op == 's' ? "split needs a word to split" : "a file name is a word");
    }
    switch (e->op) {
    case 'r': {
        out.type = V_STR;
        out.str = slurp(e->line, a.str);
        break;
    }
    case 'l': {
        char *text = slurp(e->line, a.str);
        out = new_list();
        const char *p = text;
        while (*p) {
            const char *end = strchr(p, '\n');
            size_t len = end ? (size_t)(end - p) : strlen(p);
            size_t keep = len > 0 && p[len - 1] == '\r' ? len - 1 : len;
            list_push(out.list, word_value(p, keep));
            p += len + (end ? 1 : 0);
        }
        free(text);
        break;
    }
    case 's': {
        Value sep = eval(&e->args[1]);
        if (sep.type != V_STR) {
            die(e->line, "split needs a word to split on");
        }
        out = new_list();
        size_t seplen = strlen(sep.str);
        if (seplen == 0) { /* nothing to split on: one item per letter */
            for (const char *c = a.str; *c; c++) {
                list_push(out.list, word_value(c, 1));
            }
        } else {
            const char *p = a.str;
            for (;;) {
                const char *hit = strstr(p, sep.str);
                if (hit == NULL) {
                    list_push(out.list, word_value(p, strlen(p)));
                    break;
                }
                list_push(out.list, word_value(p, (size_t)(hit - p)));
                p = hit + seplen;
            }
        }
        free_value(sep);
        break;
    }
    case 'x': {
        struct stat st;
        out.type = V_BOOL;
        out.b = stat(a.str, &st) == 0;
        break;
    }
    }
    free_value(a);
    return out;
}

static int returning;     /* a give has run, and every body on the way out stops */
static Value ret_value;   /* what that give handed back */

static void exec_range(size_t from, size_t to);

/* An object of a template, with its fields read from the variables of a frame. */
static Value obj_from_frame(Frame *f, int tmpl) {
    Value o = new_dict();
    o.type = V_OBJ;
    o.tmpl = tmpl;
    Template *t = &templates[tmpl];
    for (size_t i = 0; i < t->nfields; i++) {
        Var *v = find_in(f, t->fields[i]);
        dict_set(o.dict, t->fields[i], v ? copy_value(v->value) : (Value){0});
    }
    return o;
}

static Value eval_construct(Expr *e, int tmpl) {
    Template *t = &templates[tmpl];
    if (e->nargs != t->nparams) {
        die(e->line, "'%s' needs %zu input%s but got %zu", t->name, t->nparams, t->nparams == 1 ? "" : "s", e->nargs);
    }
    if (depth + 1 > MAX_DEPTH) {
        die(e->line, "'%s' was made inside itself too deeply (more than %d levels)", t->name, MAX_DEPTH);
    }
    Value *given = xalloc(NULL, (t->nparams ? t->nparams : 1) * sizeof *given);
    for (size_t i = 0; i < t->nparams; i++) {
        given[i] = eval(&e->args[i]);
    }
    depth++;
    for (size_t i = 0; i < t->nparams; i++) {
        declare_var(e->line, t->params[i], given[i]);
    }
    free(given);
    int caller_file = err_file;
    exec_range(t->start + 1, stmts[t->start].body_end); /* the field steps; the methods are skipped */
    err_file = caller_file;
    Value obj = obj_from_frame(&frames[depth], tmpl);
    free_frame(&frames[depth]);
    depth--;
    return obj;
}

/* Do method(inputs) on object. The method sees the object's fields as plain names, and what it does
   to them is kept, but only when the object is a name, so the change has somewhere to go. */
static Value eval_method(Expr *e) {
    Value obj = eval(e->left);
    if (obj.type != V_OBJ) {
        die(e->line, "'on' needs an object made from a template, and this is not one");
    }
    Template *t = &templates[obj.tmpl];
    Func *m = find_method(t, e->name);
    if (m == NULL) {
        die(e->line, "there is no method '%s' on %s", e->name, t->name);
    }
    if (e->nargs != m->nparams) {
        die(e->line, "'%s' needs %zu input%s but got %zu", e->name, m->nparams, m->nparams == 1 ? "" : "s", e->nargs);
    }
    if (depth + 1 > MAX_DEPTH) {
        die(e->line, "'%s' called itself too deeply (more than %d levels)", e->name, MAX_DEPTH);
    }
    Value *given = xalloc(NULL, (m->nparams ? m->nparams : 1) * sizeof *given);
    for (size_t i = 0; i < m->nparams; i++) {
        given[i] = eval(&e->args[i]);
    }
    depth++;
    frames[depth].tmpl1 = obj.tmpl + 1;
    for (size_t i = 0; i < t->nfields; i++) {
        long at = dict_find(obj.dict, t->fields[i]);
        declare_var(e->line, t->fields[i], at >= 0 ? copy_value(obj.dict->vals[at]) : (Value){0});
    }
    for (size_t i = 0; i < m->nparams; i++) {
        declare_var(e->line, m->params[i], given[i]);
    }
    free(given);
    int caller_file = err_file;
    exec_range(m->start + 1, stmts[m->start].body_end);
    err_file = caller_file;
    Value result = returning ? ret_value : (Value){0};
    ret_value = (Value){0};
    returning = 0;
    Value after = obj_from_frame(&frames[depth], obj.tmpl);
    free_frame(&frames[depth]);
    depth--;
    if (!values_equal(obj, after)) {
        if (e->left->type != E_VAR) {
            die(e->line, "this method changed the object, but only a name can keep the change; call it on a name, as in Do add(5) on c");
        }
        if (strcmp(e->left->name, "self") == 0 && frames[depth].tmpl1) { /* a method calling another on its own object */
            Template *own = &templates[frames[depth].tmpl1 - 1];
            for (size_t i = 0; i < own->nfields; i++) {
                long at = dict_find(after.dict, own->fields[i]);
                declare_var(e->line, own->fields[i], at >= 0 ? copy_value(after.dict->vals[at]) : (Value){0});
            }
            free_value(after);
        } else {
            Var *v = find_var_mut(e->line, e->left->name);
            free_value(v->value);
            v->value = after;
        }
    } else {
        free_value(after);
    }
    free_value(obj);
    return result;
}

static Value eval_field(Expr *e) {
    Value obj = eval(e->left);
    if (obj.type != V_OBJ) {
        die(e->line, "'of' needs an object made from a template, and this is not one");
    }
    long at = dict_find(obj.dict, e->name);
    if (at < 0) {
        die(e->line, "'%s' is not a field of %s", e->name, templates[obj.tmpl].name);
    }
    Value out = copy_value(obj.dict->vals[at]);
    free_value(obj);
    return out;
}

static Value eval_call(Expr *e) {
    Func *f = find_func(e->name);
    if (f == NULL) {
        int tmpl = find_template(e->name);
        if (tmpl >= 0) {
            return eval_construct(e, tmpl);
        }
        die(e->line, "there is no function or template named '%s'; make one with Create %s(...), ...! or template %s(...), ...!", e->name, e->name, e->name);
    }
    if (e->nargs != f->nparams) {
        die(e->line, "'%s' needs %zu input%s but got %zu", e->name, f->nparams, f->nparams == 1 ? "" : "s", e->nargs);
    }
    if (depth + 1 > MAX_DEPTH) {
        die(e->line, "'%s' called itself too deeply (more than %d levels)", e->name, MAX_DEPTH);
    }
    Value *given = xalloc(NULL, (f->nparams ? f->nparams : 1) * sizeof *given);
    for (size_t i = 0; i < f->nparams; i++) { /* the inputs are worked out in the caller's frame */
        given[i] = eval(&e->args[i]);
    }
    depth++;
    for (size_t i = 0; i < f->nparams; i++) {
        declare_var(e->line, f->params[i], given[i]);
    }
    free(given);
    int caller_file = err_file;
    exec_range(f->start + 1, stmts[f->start].body_end);
    err_file = caller_file;
    Value result = returning ? ret_value : (Value){0};
    ret_value = (Value){0};
    returning = 0;
    free_frame(&frames[depth]);
    depth--;
    return result;
}

static Value eval_interp(Expr *e) {
    Buf b = {0};
    for (size_t i = 0; i < e->nargs; i++) {
        Value v = eval(&e->args[i]);
        char *text = value_to_text(v);
        buf_add(&b, text, strlen(text));
        free(text);
        free_value(v);
    }
    return (Value){.type = V_STR, .str = b.s ? b.s : copy_str("", 0)};
}

static Value eval_tonum(Expr *e) {
    Value v = eval(e->left);
    if (v.type == V_NUM) {
        return v;
    }
    if (v.type != V_STR) {
        die(e->line, "number() needs a number or a string");
    }
    char *end;
    double n = strtod(v.str, &end);
    while (isspace((unsigned char)*end)) {
        end++;
    }
    if (end == v.str || *end != '\0') {
        die(e->line, "cannot turn \"%s\" into a number", v.str);
    }
    free_value(v);
    return (Value){.type = V_NUM, .num = n};
}

/* The returned Value is owned by the caller. */
static Value eval(Expr *e) {
    switch (e->type) {
    case E_NEG: {
        Value v = eval(e->left);
        return (Value){.type = V_NUM, .num = -need_number(v, e->line, "'-'")};
    }
    case E_NOT: {
        Value v = eval(e->left);
        if (v.type != V_BOOL) {
            die(e->line, "Not needs a Yes or No");
        }
        v.b = !v.b;
        return v;
    }
    case E_BINARY:
        if (e->op == OP_AND || e->op == OP_OR) {
            return eval_join(e);
        }
        return eval_binary(e);
    case E_TONUM:
        return eval_tonum(e);
    case E_INDEX:
        return eval_index(e);
    case E_BUILTIN:
        return eval_builtin(e);
    case E_CALL:
        return eval_call(e);
    case E_INTERP:
        return eval_interp(e);
    case E_LIST: {
        Value v = new_list();
        for (size_t i = 0; i < e->nargs; i++) {
            list_push(v.list, eval(&e->args[i]));
        }
        return v;
    }
    case E_DICT: {
        Value v = new_dict();
        for (size_t i = 0; i + 1 < e->nargs; i += 2) {
            Value name = eval(&e->args[i]);
            if (name.type != V_STR) {
                die(e->line, "an Entry is named with a word");
            }
            dict_set(v.dict, name.str, eval(&e->args[i + 1]));
            free_value(name);
        }
        return v;
    }
    case E_LITERAL:
        return copy_value(e->lit);
    case E_METHOD:
        return eval_method(e);
    case E_FIELD:
        return eval_field(e);
    case E_VAR: {
        if (strcmp(e->name, "self") == 0 && frames[depth].tmpl1) { /* inside a method: the object itself */
            return obj_from_frame(&frames[depth], frames[depth].tmpl1 - 1);
        }
        Var *v = find_var(e->name);
        if (v == NULL) {
            die(e->line, "'%s' is not defined", e->name);
        }
        return copy_value(v->value);
    }
    case E_ASKS:
        fputs(e->prompt, stdout);
        fflush(stdout);
        return (Value){.type = V_STR, .str = read_line()};
    }
    return (Value){0};
}

static void exec_range(size_t from, size_t to) {
    for (size_t i = from; i < to; i++) {
        Stmt *s = &stmts[i];
        err_file = s->file;
        switch (s->type) {
        case S_THING: {
            Value v = {0};
            if (s->has_expr) {
                v = eval(&s->expr);
            } else if (s->want == V_LIST) {
                v = new_list();
            } else if (s->want == V_DICT) {
                v = new_dict();
            }
            if (s->want && v.type != (ValType)s->want) {
                die(s->line, s->want == V_LIST ? "'List-of' needs a list written [ ... ]"
                                               : "'Book-of' needs a Book written { ... }");
            }
            declare_var(s->line, s->name, v);
            break;
        }
        case S_DO:
        case S_TEMPLATE:
            i = s->body_end - 1; /* a function's body, and a template's, only run when they are called */
            break;
        case S_SETFIELD: {
            Value item = eval(&s->expr);
            if (strcmp(s->target, "self") == 0 && frames[depth].tmpl1) {
                die(s->line, "inside a method the fields are plain names: write %s holds ..., not %s of self holds ...", s->name, s->name);
            }
            Var *v = find_var_mut(s->line, s->target);
            if (v->value.type != V_OBJ) {
                die(s->line, "'%s' is not an object, so it has no fields", s->target);
            }
            long at = dict_find(v->value.dict, s->name);
            if (at < 0) {
                die(s->line, "'%s' is not a field of %s", s->name, templates[v->value.tmpl].name);
            }
            free_value(v->value.dict->vals[at]);
            v->value.dict->vals[at] = item;
            break;
        }
        case S_GIVE: {
            Value v = eval(&s->expr);
            free_value(ret_value);
            ret_value = v;
            returning = 1;
            break;
        }
        case S_CALL: {
            Value v = eval(&s->expr);
            free_value(v);
            break;
        }
        case S_SORT: {
            int backward = 0;
            if (s->has_expr) {
                Value b = eval(&s->expr);
                if (b.type != V_BOOL) {
                    die(s->line, "backward is Yes or No");
                }
                backward = b.b;
            }
            Var *v = find_var_mut(s->line, s->name);
            if (v == NULL) {
                die(s->line, "'%s' is not defined", s->name);
            }
            if (v->value.type != V_LIST) {
                die(s->line, "Sort puts a list in order, and '%s' is not one", s->name);
            }
            sort_list(s->line, v->value.list, backward);
            break;
        }
        case S_WRITE: {
            Value val = eval(&s->expr);
            Value path = eval(&s->index);
            if (path.type != V_STR) {
                die(s->line, "a file name is a word");
            }
            FILE *f = fopen(path.str, s->want ? "a" : "w");
            if (f == NULL) {
                die(s->line, "could not open \"%s\" for writing", path.str);
            }
            if (val.type == V_LIST) { /* one item per line */
                for (size_t k = 0; k < val.list->len; k++) {
                    char *text = value_to_text(val.list->items[k]);
                    fprintf(f, "%s\n", text);
                    free(text);
                }
            } else {
                char *text = value_to_text(val);
                fprintf(f, "%s\n", text);
                free(text);
            }
            fclose(f);
            free_value(val);
            free_value(path);
            break;
        }
        case S_APPEND: {
            Value item = eval(&s->expr);
            Var *v = find_var_mut(s->line, s->name);
            if (v == NULL) {
                die(s->line, "'%s' is not defined", s->name);
            }
            if (v->value.type != V_LIST) {
                die(s->line, "append adds to a list, and '%s' is not one", s->name);
            }
            list_push(v->value.list, item);
            break;
        }
        case S_SETINDEX: {
            Value where = eval(&s->index);
            Value item = eval(&s->expr);
            Var *v = find_var_mut(s->line, s->name);
            if (v == NULL) {
                die(s->line, "'%s' is not defined", s->name);
            }
            if (v->value.type == V_LIST) {
                long i = place_in(&s->index, where, v->value.list->len, "the list");
                free_value(v->value.list->items[i]);
                v->value.list->items[i] = item;
            } else if (v->value.type == V_DICT) {
                if (where.type != V_STR) {
                    die(s->line, "an Entry is named with a word");
                }
                dict_set(v->value.dict, where.str, item);
            } else {
                die(s->line, "only a list or a Book can be changed this way, and '%s' is not one", s->name);
            }
            free_value(where);
            break;
        }
        case S_ASSIGN:
            find_var_mut(s->line, s->name);
            declare_var(s->line, s->name, eval(&s->expr));
            break;
        case S_SAY: {
            Value v = eval(&s->expr);
            print_value(v);
            free_value(v);
            break;
        }
        case S_IF: {
            Value cond = eval(&s->expr);
            if (cond.type != V_BOOL) {
                die(s->line, "'If' needs a Yes or No");
            }
            s->taken = cond.b ? 1 : 2;
            if (cond.b) {
                exec_range(i + 1, s->body_end);
            }
            i = s->body_end - 1; /* skip the body either way */
            break;
        }
        case S_IFNOT:
            if (stmts[s->link].taken == 2) {
                exec_range(i + 1, s->body_end);
            }
            i = s->body_end - 1;
            break;
        case S_FOR: {
            Value seq = eval(&s->expr);
            size_t n;
            switch (seq.type) {
            case V_STR: n = strlen(seq.str); break;
            case V_LIST: n = seq.list->len; break;
            case V_DICT: n = seq.dict->len; break;
            case V_OBJ: n = seq.dict->len; break;
            default:
                die(s->line, "can only loop over a word, a list or a Book");
                n = 0;
            }
            for (size_t k = 0; k < n; k++) {
                Value item;
                if (seq.type == V_STR) {
                    item = (Value){.type = V_STR, .str = copy_str(seq.str + k, 1)};
                } else if (seq.type == V_LIST) {
                    item = copy_value(seq.list->items[k]);
                } else { /* a Book gives its Entries */
                    item = (Value){.type = V_STR, .str = copy_str(seq.dict->keys[k], strlen(seq.dict->keys[k]))};
                }
                declare_var(s->line, s->name, item);
                exec_range(i + 1, s->body_end);
                if (returning) {
                    break;
                }
            }
            free_value(seq);
            i = s->body_end - 1; /* the body has already run */
            break;
        }
        }
        if (returning) { /* a give ran: leave every body up to the function */
            return;
        }
    }
}

static void run(void) {
    exec_range(0, nstmts);
}

/* ---------- main ---------- */

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "thing: could not open '%s'\n", path);
        exit(1);
    }
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    size_t n;
    while (buf != NULL && (n = fread(buf + len, 1, cap - len - 1, f)) > 0) {
        len += n;
        if (len == cap - 1) {
            cap *= 2;
            buf = realloc(buf, cap);
        }
    }
    fclose(f);
    if (buf == NULL) {
        die(0, "out of memory");
    }
    buf[len] = '\0';
    return buf;
}

/* thing new <name>: a folder holding a main.thing that already says hello */
static int new_project(const char *name) {
    if (name[0] == '\0' || strchr(name, '/') != NULL) {
        fprintf(stderr, "thing: a project name is a plain folder name, like hello\n");
        return 2;
    }
    if (mkdir(name, 0777) != 0) {
        fprintf(stderr, "thing: could not create \"%s\": %s\n", name, strerror(errno));
        return 1;
    }
    size_t n = strlen(name) + sizeof "/main.thing";
    char *path = malloc(n);
    if (path == NULL) {
        die(0, "out of memory");
    }
    snprintf(path, n, "%s/main.thing", name);
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        fprintf(stderr, "thing: could not create \"%s\": %s\n", path, strerror(errno));
        return 1;
    }
    fputs("say(\"Hello, world\").\n", f);
    fclose(f);
    printf("Created %s\nRun it with:\n    cd %s\n    thing run\n", path, name);
    free(path);
    return 0;
}

static void run_source(const char *src) {
    file_paths[0] = (char *)main_path;
    tokenize(src);
    parse_program();
    run();
}

/* ---------- building a program into its own executable ----------
   A built program is this interpreter followed by the program's text and a small
   trailer (the length, then a marker). On start-up the interpreter looks for the
   trailer in its own file, and if it is there it runs that text instead of reading
   the command line. */

#define PACK_MARK "THINGPK1"
#define PACK_TAIL 16 /* 8 bytes of length, 8 bytes of marker */

static char *read_self(size_t *len) {
    FILE *f = fopen("/proc/self/exe", "rb");
    if (f == NULL) {
        return NULL;
    }
    Buf b = {0};
    char chunk[8192];
    size_t n;
    while ((n = fread(chunk, 1, sizeof chunk, f)) > 0) {
        buf_add(&b, chunk, n);
    }
    fclose(f);
    *len = b.len;
    return b.s;
}

/* The program text packed onto the end of this executable, or NULL if there is none. */
static char *packed_source(size_t *len_out) {
    size_t total;
    char *self = read_self(&total);
    if (self == NULL || total < PACK_TAIL || memcmp(self + total - 8, PACK_MARK, 8) != 0) {
        free(self);
        return NULL;
    }
    unsigned long long len = 0;
    for (int i = 7; i >= 0; i--) {
        len = len * 256 + (unsigned char)self[total - PACK_TAIL + i];
    }
    if (len > total - PACK_TAIL) {
        free(self);
        return NULL;
    }
    char *src = copy_str(self + total - PACK_TAIL - len, (size_t)len);
    free(self);
    *len_out = (size_t)len;
    return src;
}

/* thing build [file]: check the program, then write an executable named after it */
static int build_program(const char *path) {
    char *src = read_file(path);
    main_path = path;
    file_paths[0] = (char *)path;
    tokenize(src); /* mistakes in the program are reported now, not when it is run */
    parse_program();

    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    size_t blen = strlen(base);
    if (blen > 6 && strcmp(base + blen - 6, ".thing") == 0) {
        blen -= 6;
    }
    char name[512];
    if (strncmp(base, "main.thing", blen + 6) == 0 && blen == 4) { /* main.thing: use the folder's name */
        char cwd[1024];
        const char *folder = getcwd(cwd, sizeof cwd) ? strrchr(cwd, '/') : NULL;
        snprintf(name, sizeof name, "%s", folder && folder[1] ? folder + 1 : "program");
    } else if (blen == strlen(base)) { /* no .thing ending: keep the name apart from the source */
        snprintf(name, sizeof name, "%s.out", base);
    } else {
        snprintf(name, sizeof name, "%.*s", (int)blen, base);
    }

    size_t total;
    char *self = read_self(&total);
    if (self == NULL) {
        fprintf(stderr, "thing: could not read the interpreter to build from\n");
        return 1;
    }
    FILE *out = fopen(name, "wb");
    if (out == NULL) {
        fprintf(stderr, "thing: could not create \"%s\": %s\n", name, strerror(errno));
        return 1;
    }
    /* what is packed: the program, then each outsourced file as its name and its text, each ended by a NUL */
    unsigned long long len = strlen(src) + 1;
    for (int i = 1; i < nfiles; i++) {
        len += strlen(file_names[i]) + 1 + strlen(file_text[i]) + 1;
    }
    unsigned char tail[PACK_TAIL];
    for (int i = 0; i < 8; i++) {
        tail[i] = (unsigned char)(len >> (8 * i));
    }
    memcpy(tail + 8, PACK_MARK, 8);
    fwrite(self, 1, total, out);
    fwrite(src, 1, strlen(src) + 1, out);
    for (int i = 1; i < nfiles; i++) {
        fwrite(file_names[i], 1, strlen(file_names[i]) + 1, out);
        fwrite(file_text[i], 1, strlen(file_text[i]) + 1, out);
    }
    fwrite(tail, 1, PACK_TAIL, out);
    fclose(out);
    chmod(name, 0755);
    printf("Built %s\nRun it with:\n    ./%s\n", name, name);
    return 0;
}

/* thing notes [file]: list every Next note, the plan written beside the code */
static int list_notes(const char *path) {
    char *src = read_file(path);
    note_mode = 1;
    note_file = path;
    tokenize(src);
    if (notes_found == 0) {
        printf("No Next notes in %s\n", path);
    }
    return 0;
}

int main(int argc, char **argv) {
    size_t packed_len = 0;
    char *packed = packed_source(&packed_len);
    if (packed != NULL) { /* this is a built program: run it, with the files it outsourced */
        const char *p = packed + strlen(packed) + 1;
        const char *end = packed + packed_len;
        while (p < end && nbundle < MAX_FILES) {
            bundle[nbundle].name = (char *)p;
            p += strlen(p) + 1;
            bundle[nbundle].text = (char *)p;
            p += strlen(p) + 1;
            nbundle++;
        }
        run_source(packed);
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "new") == 0) {
        return new_project(argv[2]);
    }
    if (argc >= 2 && argc <= 3 && (strcmp(argv[1], "run") == 0 || strcmp(argv[1], "build") == 0 || strcmp(argv[1], "notes") == 0)) {
        const char *path = argc == 3 ? argv[2] : "main.thing";
        struct stat st;
        if (argc == 2 && stat(path, &st) != 0) {
            fprintf(stderr, "thing: there is no main.thing in this folder; start a project with: thing new hello\n");
            return 1;
        }
        if (strcmp(argv[1], "build") == 0) {
            return build_program(path);
        }
        if (strcmp(argv[1], "notes") == 0) {
            return list_notes(path);
        }
        main_path = path;
        run_source(read_file(path));
        return 0;
    }
    if (argc != 2) {
        fprintf(stderr, "usage: %s new <name>      start a project with a hello world\n"
                        "       %s run [file]      run main.thing, or the file given\n"
                        "       %s build [file]    make a program that runs on its own\n"
                        "       %s notes [file]    list the Next notes in a program\n"
                        "       %s program.thing   run a program\n", argv[0], argv[0], argv[0], argv[0], argv[0]);
        return 2;
    }
    main_path = argv[1];
    run_source(read_file(argv[1]));
    return 0;
}
