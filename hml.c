/* =========================================================
 *  HML — Hybrid Markup Language  v2.1
 *  单文件编译器/解释器
 *
 *  编译(无图形): gcc hml.c -o hml.exe -DNO_GRAPHICS -O2
 *  编译(带图形): gcc hml.c -o hml.exe -lSDL2 -lm -O2
 *  用法:         hml <file.hml> [args...]
 *
 *  v2.1 变更:
 *    - 修复: 函数内修改全局变量现在生效
 *    - 新增: continue 语句在 while/loop/for/foreach 里生效
 *    - 新增内置: exit() printn() startswith(s,p) endswith(s,p)
 *
 *  语法:
 *    let x = 10
 *    let s = "hello"
 *    let a = [1, 2, 3]
 *    print expr          /  printn expr (不换行)
 *    input name
 *    if cond { } else if cond { } else { }
 *    while cond { }
 *    loop 5 { }
 *    loop i from 1 to 10 { }
 *    for i = 0 to 10 step 2 { }
 *    foreach x in arr { }
 *    break / continue / return expr / exit()
 *    func name(a, b) { return a + b }
 *    x += 1   x -= 1   x *= 2   x /= 2
 *    and  or  not
 *    # 注释
 *
 *  模式头(第一行):
 *    @cmd                    命令行
 *    @gfx 800 600 "标题"      图形
 *    @mix                    混合
 *
 *  声明式 UI:
 *    ui {
 *        label  "分数:"    at 20 20
 *        button "重置"      at 20 60  onclick=reset
 *        rect   x=200 y=20 w=100 h=40 color=0xff5500
 *        circle cx=400 cy=300 r=50 color=0x00ff88
 *        text   "Hello"    at 500 100 color=0xffffff
 *    }
 *
 *  内置函数:
 *    数学 abs sqrt min max pow rand randrange
 *    字符 len substr find upper lower int str startswith endswith
 *    数组 push pop insert remove at
 *    工具 cls args argc sleep now exit printn
 *    图形 gwin gclear grect gcircle gline gpresent gpoll gsleep gquit
 *         mousex mousey key
 * ========================================================= */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <time.h>
#include <math.h>

#ifdef _WIN32
#include <windows.h>
#endif

static int cur_line = 0;
#define ERR(fmt, ...) do { \
    fprintf(stderr, "[HML] 第%d行: " fmt "\n", cur_line, ##__VA_ARGS__); \
    exit(1); \
} while(0)

static int    g_argc = 0;
static char **g_argv = NULL;

/* ============ 值系统 ============ */
typedef enum { V_INT, V_STR, V_ARR } VType;
typedef struct Array Array;
typedef struct {
    VType type;
    union { int i; char *s; Array *a; };
} Value;

struct Array {
    Value *items;
    int n, cap;
};

static void arr_push(Array *a, Value v) {
    if (a->n >= a->cap) {
        a->cap = a->cap ? a->cap * 2 : 8;
        a->items = realloc(a->items, sizeof(Value) * a->cap);
    }
    a->items[a->n++] = v;
}

/* ============ 词法 ============ */
typedef enum {
    T_EOF, T_NUM, T_STR, T_ID,
    T_LET, T_PRINT, T_INPUT, T_IF, T_ELSE, T_WHILE, T_LOOP,
    T_FROM, T_TO, T_STEP, T_FOR, T_FOREACH, T_IN, T_BREAK, T_CONTINUE,
    T_FUNC, T_RETURN, T_TRUE, T_FALSE, T_AND, T_OR, T_NOT,
    T_UI, T_AT, T_ONCLICK, T_LABEL, T_BUTTON, T_RECT, T_CIRCLE, T_TEXT,
    T_LP, T_RP, T_LB, T_RB, T_LBRACE, T_RBRACE,
    T_COMMA, T_SEMI, T_ASSIGN,
    T_PLUS, T_MINUS, T_STAR, T_SLASH,
    T_PLUSEQ, T_MINUSEQ, T_STAREQ, T_SLASHEQ,
    T_EQ, T_NEQ, T_LT, T_GT, T_LE, T_GE
} TokType;

typedef struct {
    TokType type;
    long num;
    char *str;
    int line;
} Token;

typedef struct {
    Token *toks;
    int count, cap;
    const char *src;
} Lexer;

static void lex_push(Lexer *L, Token t) {
    if (L->count >= L->cap) {
        L->cap = L->cap ? L->cap * 2 : 64;
        L->toks = realloc(L->toks, sizeof(Token) * L->cap);
    }
    L->toks[L->count++] = t;
}

static void lex_tokenize(Lexer *L) {
    const char *s = L->src;
    cur_line = 1;
    while (*s) {
        if (*s == '\n') { cur_line++; s++; continue; }
        if (isspace((unsigned char)*s)) { s++; continue; }
        if (*s == '#') { while (*s && *s != '\n') s++; continue; }

        Token t = {0}; t.line = cur_line;

        if (isdigit((unsigned char)*s)) {
            long v = 0;
            if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
                s += 2;
                while (isxdigit((unsigned char)*s)) {
                    int d = isdigit((unsigned char)*s) ? *s - '0'
                          : tolower((unsigned char)*s) - 'a' + 10;
                    v = v * 16 + d;
                    s++;
                }
            } else {
                while (isdigit((unsigned char)*s)) v = v*10 + (*s++ - '0');
            }
            t.type = T_NUM; t.num = v;
            lex_push(L, t); continue;
        }

        if (*s == '"') {
            s++;
            char buf[4096]; int i = 0;
            while (*s && *s != '"') {
                if (*s == '\\' && s[1]) {
                    s++;
                    switch (*s) {
                        case 'n': buf[i++] = '\n'; break;
                        case 't': buf[i++] = '\t'; break;
                        case '"': buf[i++] = '"';  break;
                        case '\\':buf[i++] = '\\'; break;
                        default:  buf[i++] = *s;   break;
                    }
                } else buf[i++] = *s;
                s++;
            }
            if (*s != '"') ERR("字符串未闭合");
            s++;
            buf[i] = 0;
            t.type = T_STR; t.str = strdup(buf);
            lex_push(L, t); continue;
        }

        if (isalpha((unsigned char)*s) || *s == '_') {
            char buf[256]; int i = 0;
            while (isalnum((unsigned char)*s) || *s == '_') buf[i++] = *s++;
            buf[i] = 0;
            t.str = strdup(buf);
            if      (!strcmp(buf,"let"))      t.type = T_LET;
            else if (!strcmp(buf,"print"))    t.type = T_PRINT;
            else if (!strcmp(buf,"input"))    t.type = T_INPUT;
            else if (!strcmp(buf,"if"))       t.type = T_IF;
            else if (!strcmp(buf,"else"))     t.type = T_ELSE;
            else if (!strcmp(buf,"while"))    t.type = T_WHILE;
            else if (!strcmp(buf,"loop"))     t.type = T_LOOP;
            else if (!strcmp(buf,"from"))     t.type = T_FROM;
            else if (!strcmp(buf,"to"))       t.type = T_TO;
            else if (!strcmp(buf,"step"))     t.type = T_STEP;
            else if (!strcmp(buf,"for"))      t.type = T_FOR;
            else if (!strcmp(buf,"foreach"))  t.type = T_FOREACH;
            else if (!strcmp(buf,"in"))       t.type = T_IN;
            else if (!strcmp(buf,"break"))    t.type = T_BREAK;
            else if (!strcmp(buf,"continue")) t.type = T_CONTINUE;
            else if (!strcmp(buf,"func"))     t.type = T_FUNC;
            else if (!strcmp(buf,"return"))   t.type = T_RETURN;
            else if (!strcmp(buf,"true"))     t.type = T_TRUE;
            else if (!strcmp(buf,"false"))    t.type = T_FALSE;
            else if (!strcmp(buf,"and"))      t.type = T_AND;
            else if (!strcmp(buf,"or"))       t.type = T_OR;
            else if (!strcmp(buf,"not"))      t.type = T_NOT;
            else if (!strcmp(buf,"ui"))       t.type = T_UI;
            else if (!strcmp(buf,"at"))       t.type = T_AT;
            else if (!strcmp(buf,"onclick"))  t.type = T_ONCLICK;
            else if (!strcmp(buf,"label"))    t.type = T_LABEL;
            else if (!strcmp(buf,"button"))   t.type = T_BUTTON;
            else if (!strcmp(buf,"rect"))     t.type = T_RECT;
            else if (!strcmp(buf,"circle"))   t.type = T_CIRCLE;
            else if (!strcmp(buf,"text"))     t.type = T_TEXT;
            else                              t.type = T_ID;
            lex_push(L, t); continue;
        }

        #define OP2(a,b,tt) if (s[0]==a && s[1]==b) { t.type=tt; s+=2; lex_push(L,t); continue; }
        OP2('+','=',T_PLUSEQ) OP2('-','=',T_MINUSEQ)
        OP2('*','=',T_STAREQ) OP2('/','=',T_SLASHEQ)
        OP2('=','=',T_EQ)     OP2('!','=',T_NEQ)
        OP2('<','=',T_LE)     OP2('>','=',T_GE)
        #undef OP2
        switch (*s) {
            case '(': t.type=T_LP;     s++; break;
            case ')': t.type=T_RP;     s++; break;
            case '[': t.type=T_LB;     s++; break;
            case ']': t.type=T_RB;     s++; break;
            case '{': t.type=T_LBRACE; s++; break;
            case '}': t.type=T_RBRACE; s++; break;
            case ',': t.type=T_COMMA;  s++; break;
            case ';': t.type=T_SEMI;   s++; break;
            case '=': t.type=T_ASSIGN; s++; break;
            case '+': t.type=T_PLUS;   s++; break;
            case '-': t.type=T_MINUS;  s++; break;
            case '*': t.type=T_STAR;   s++; break;
            case '/': t.type=T_SLASH;  s++; break;
            case '<': t.type=T_LT;     s++; break;
            case '>': t.type=T_GT;     s++; break;
            default: ERR("无法识别的字符 '%c' (%d)", *s, (unsigned char)*s);
        }
        lex_push(L, t);
    }
    Token eof = {.type=T_EOF, .line=cur_line};
    lex_push(L, eof);
}

/* ============ AST ============ */
typedef enum {
    N_NUM, N_STR, N_VAR, N_BINOP, N_CALL, N_INDEX, N_ARRAY,
    N_LET, N_PRINT, N_INPUT, N_IF, N_WHILE, N_LOOP,
    N_FOR, N_FOREACH, N_BREAK, N_CONTINUE,
    N_BLOCK, N_FUNC, N_RETURN, N_EXPRSTMT,
    N_UI, N_UI_WIDGET
} NType;

typedef struct Node Node;
struct Node {
    NType type;
    int line;
    long num;
    char *str;
    int op;
    Node *a, *b, *c, *d;
    Node **list; int list_n;
    char *name;
    char **params; int param_n;
    char *widget_kind;
    Node *x, *y, *w, *h, *color;
    char *onclick_name;
};

typedef struct { Token *toks; int pos, count; } Parser;

static Node *parse_expr(Parser *P);
static Node *parse_stmt(Parser *P);

static Token *peek(Parser *P)    { return &P->toks[P->pos]; }
static Token *advance(Parser *P) { return &P->toks[P->pos++]; }
static int  check(Parser *P, TokType t) { return peek(P)->type == t; }
static Token *expect(Parser *P, TokType t, const char *msg) {
    if (!check(P, t)) { cur_line = peek(P)->line; ERR("期望 %s", msg); }
    return advance(P);
}

static Node *new_node(NType t, int line) {
    Node *n = calloc(1, sizeof(Node));
    n->type = t; n->line = line;
    return n;
}

static Node *parse_primary(Parser *P) {
    Token *t = peek(P);
    cur_line = t->line;

    if (t->type == T_NUM) {
        advance(P);
        Node *n = new_node(N_NUM, t->line); n->num = t->num; return n;
    }
    if (t->type == T_STR) {
        advance(P);
        Node *n = new_node(N_STR, t->line); n->str = t->str; return n;
    }
    if (t->type == T_TRUE || t->type == T_FALSE) {
        advance(P);
        Node *n = new_node(N_NUM, t->line); n->num = (t->type == T_TRUE); return n;
    }
    if (t->type == T_LP) {
        advance(P);
        Node *n = parse_expr(P);
        expect(P, T_RP, "')'");
        return n;
    }
    if (t->type == T_LB) {
        advance(P);
        Node *n = new_node(N_ARRAY, t->line);
        Node **elems = NULL; int en = 0, ec = 0;
        while (!check(P, T_RB)) {
            if (en >= ec) { ec = ec?ec*2:4; elems = realloc(elems, sizeof(Node*)*ec); }
            elems[en++] = parse_expr(P);
            if (check(P, T_COMMA)) advance(P);
            else break;
        }
        expect(P, T_RB, "']'");
        n->list = elems; n->list_n = en;
        return n;
    }
    if (t->type == T_ID) {
        advance(P);
        if (check(P, T_LP)) {
            advance(P);
            Node *n = new_node(N_CALL, t->line); n->name = t->str;
            Node **args = NULL; int an = 0, ac = 0;
            if (!check(P, T_RP)) {
                do {
                    if (an >= ac) { ac = ac ? ac*2 : 4; args = realloc(args, sizeof(Node*)*ac); }
                    args[an++] = parse_expr(P);
                } while (check(P, T_COMMA) && (advance(P), 1));
            }
            expect(P, T_RP, "')'");
            n->list = args; n->list_n = an;
            return n;
        }
        if (check(P, T_LB)) {
            advance(P);
            Node *idx = parse_expr(P);
            expect(P, T_RB, "']'");
            Node *n = new_node(N_INDEX, t->line);
            n->name = t->str; n->a = idx;
            return n;
        }
        Node *n = new_node(N_VAR, t->line); n->name = t->str; return n;
    }
    ERR("表达式语法错误");
    return NULL;
}

static Node *parse_unary(Parser *P) {
    if (check(P, T_MINUS)) {
        Token *t = advance(P);
        Node *n = new_node(N_BINOP, t->line);
        n->op = T_MINUS;
        Node *zero = new_node(N_NUM, t->line); zero->num = 0;
        n->a = zero; n->b = parse_unary(P);
        return n;
    }
    if (check(P, T_NOT)) {
        Token *t = advance(P);
        Node *n = new_node(N_BINOP, t->line);
        n->op = T_NOT;
        Node *zero = new_node(N_NUM, t->line); zero->num = 0;
        n->a = zero; n->b = parse_unary(P);
        return n;
    }
    return parse_primary(P);
}

static Node *parse_mul(Parser *P) {
    Node *l = parse_unary(P);
    while (check(P, T_STAR) || check(P, T_SLASH)) {
        Token *t = advance(P);
        Node *n = new_node(N_BINOP, t->line);
        n->op = t->type; n->a = l; n->b = parse_unary(P);
        l = n;
    }
    return l;
}

static Node *parse_add(Parser *P) {
    Node *l = parse_mul(P);
    while (check(P, T_PLUS) || check(P, T_MINUS)) {
        Token *t = advance(P);
        Node *n = new_node(N_BINOP, t->line);
        n->op = t->type; n->a = l; n->b = parse_mul(P);
        l = n;
    }
    return l;
}

static Node *parse_cmp(Parser *P) {
    Node *l = parse_add(P);
    while (check(P,T_EQ)||check(P,T_NEQ)||check(P,T_LT)||
           check(P,T_GT)||check(P,T_LE)||check(P,T_GE)) {
        Token *t = advance(P);
        Node *n = new_node(N_BINOP, t->line);
        n->op = t->type; n->a = l; n->b = parse_add(P);
        l = n;
    }
    return l;
}

static Node *parse_logic(Parser *P) {
    Node *l = parse_cmp(P);
    while (check(P, T_AND) || check(P, T_OR)) {
        Token *t = advance(P);
        Node *n = new_node(N_BINOP, t->line);
        n->op = t->type; n->a = l; n->b = parse_cmp(P);
        l = n;
    }
    return l;
}

static Node *parse_expr(Parser *P) { return parse_logic(P); }

static Node *parse_block(Parser *P) {
    if (check(P, T_LBRACE)) {
        Token *t = advance(P);
        Node *n = new_node(N_BLOCK, t->line);
        Node **list = NULL; int ln = 0, lc = 0;
        while (!check(P, T_RBRACE) && !check(P, T_EOF)) {
            if (ln >= lc) { lc = lc?lc*2:4; list = realloc(list, sizeof(Node*)*lc); }
            list[ln++] = parse_stmt(P);
        }
        expect(P, T_RBRACE, "'}'");
        n->list = list; n->list_n = ln;
        return n;
    }
    Node *n = new_node(N_BLOCK, peek(P)->line);
    n->list = malloc(sizeof(Node*)); n->list[0] = parse_stmt(P); n->list_n = 1;
    return n;
}

static Node *parse_ui_widget(Parser *P) {
    Token *t = advance(P);
    Node *w = new_node(N_UI_WIDGET, t->line);
    w->widget_kind = t->str;

    if (t->type == T_RECT || t->type == T_CIRCLE) {
        while (!check(P, T_RBRACE) && !check(P, T_EOF)) {
            if (check(P, T_ID)) {
                Token *k = advance(P);
                expect(P, T_ASSIGN, "'='");
                Node *v = parse_expr(P);
                if      (!strcmp(k->str,"x"))     w->x = v;
                else if (!strcmp(k->str,"y"))     w->y = v;
                else if (!strcmp(k->str,"w"))     w->w = v;
                else if (!strcmp(k->str,"h"))     w->h = v;
                else if (!strcmp(k->str,"cx"))    w->x = v;
                else if (!strcmp(k->str,"cy"))    w->y = v;
                else if (!strcmp(k->str,"r"))     w->w = v;
                else if (!strcmp(k->str,"color")) w->color = v;
            } else break;
        }
        return w;
    }

    if (check(P, T_STR)) w->str = advance(P)->str;
    while (!check(P, T_RBRACE) && !check(P, T_EOF)) {
        if (check(P, T_AT)) {
            advance(P);
            w->x = parse_expr(P);
            w->y = parse_expr(P);
        } else if (check(P, T_ONCLICK)) {
            advance(P);
            expect(P, T_ASSIGN, "'='");
            Token *fn = expect(P, T_ID, "函数名");
            w->onclick_name = fn->str;
        } else if (check(P, T_ID)) {
            Token *k = advance(P);
            expect(P, T_ASSIGN, "'='");
            Node *v = parse_expr(P);
            if (!strcmp(k->str,"color")) w->color = v;
        } else break;
    }
    return w;
}

static Node *parse_ui_block(Parser *P) {
    Token *t = expect(P, T_UI, "'ui'");
    Node *n = new_node(N_UI, t->line);
    expect(P, T_LBRACE, "'{'");
    Node **list = NULL; int ln = 0, lc = 0;
    while (!check(P, T_RBRACE) && !check(P, T_EOF)) {
        if (ln >= lc) { lc = lc?lc*2:4; list = realloc(list, sizeof(Node*)*lc); }
        list[ln++] = parse_ui_widget(P);
    }
    expect(P, T_RBRACE, "'}'");
    n->list = list; n->list_n = ln;
    return n;
}

static Node *parse_stmt(Parser *P) {
    Token *t = peek(P);
    cur_line = t->line;

    if (check(P, T_SEMI)) { advance(P); return parse_stmt(P); }
    if (check(P, T_BREAK))    { advance(P); return new_node(N_BREAK, t->line); }
    if (check(P, T_CONTINUE)) { advance(P); return new_node(N_CONTINUE, t->line); }
    if (check(P, T_UI)) return parse_ui_block(P);

    if (check(P, T_LET)) {
        advance(P);
        Token *id = expect(P, T_ID, "变量名");
        if (isdigit((unsigned char)id->str[0])) ERR("变量名不能以数字开头");
        Node *n = new_node(N_LET, t->line);
        n->name = id->str;
        expect(P, T_ASSIGN, "'='");
        n->a = parse_expr(P);
        if (check(P, T_SEMI)) advance(P);
        return n;
    }

    if (check(P, T_PRINT)) {
        advance(P);
        Node *n = new_node(N_PRINT, t->line);
        n->a = parse_expr(P);
        if (check(P, T_SEMI)) advance(P);
        return n;
    }

    if (check(P, T_INPUT)) {
        advance(P);
        Token *id = expect(P, T_ID, "变量名");
        Node *n = new_node(N_INPUT, t->line);
        n->name = id->str;
        if (check(P, T_SEMI)) advance(P);
        return n;
    }

    if (check(P, T_IF)) {
        advance(P);
        Node *n = new_node(N_IF, t->line);
        n->a = parse_expr(P);
        n->b = parse_block(P);
        if (check(P, T_ELSE)) {
            advance(P);
            if (check(P, T_IF)) n->c = parse_stmt(P);
            else                n->c = parse_block(P);
        }
        return n;
    }

    if (check(P, T_WHILE)) {
        advance(P);
        Node *n = new_node(N_WHILE, t->line);
        n->a = parse_expr(P);
        n->b = parse_block(P);
        return n;
    }

    if (check(P, T_LOOP)) {
        advance(P);
        Node *n = new_node(N_LOOP, t->line);
        if (check(P, T_ID) && P->toks[P->pos+1].type == T_FROM) {
            Token *id = advance(P);
            advance(P);
            n->name = id->str;
            n->a = parse_expr(P);
            expect(P, T_TO, "'to'");
            n->b = parse_expr(P);
            n->c = parse_block(P);
        } else {
            n->a = parse_expr(P);
            n->b = parse_block(P);
        }
        return n;
    }

    if (check(P, T_FOR)) {
        advance(P);
        Node *n = new_node(N_FOR, t->line);
        Token *id = expect(P, T_ID, "循环变量");
        n->name = id->str;
        expect(P, T_ASSIGN, "'='");
        n->a = parse_expr(P);
        expect(P, T_TO, "'to'");
        n->b = parse_expr(P);
        if (check(P, T_STEP)) { advance(P); n->c = parse_expr(P); }
        else n->c = NULL;
        n->d = parse_block(P);
        return n;
    }

    if (check(P, T_FOREACH)) {
        advance(P);
        Node *n = new_node(N_FOREACH, t->line);
        Token *id = expect(P, T_ID, "变量名");
        n->name = id->str;
        expect(P, T_IN, "'in'");
        n->a = parse_expr(P);
        n->b = parse_block(P);
        return n;
    }

    if (check(P, T_FUNC)) {
        advance(P);
        Token *id = expect(P, T_ID, "函数名");
        Node *n = new_node(N_FUNC, t->line);
        n->name = id->str;
        expect(P, T_LP, "'('");
        char **params = NULL; int pn = 0, pc = 0;
        if (!check(P, T_RP)) {
            do {
                if (pn >= pc) { pc = pc?pc*2:4; params = realloc(params, sizeof(char*)*pc); }
                Token *p = expect(P, T_ID, "参数名");
                params[pn++] = p->str;
            } while (check(P, T_COMMA) && (advance(P), 1));
        }
        expect(P, T_RP, "')'");
        n->params = params; n->param_n = pn;
        n->a = parse_block(P);
        return n;
    }

    if (check(P, T_RETURN)) {
        advance(P);
        Node *n = new_node(N_RETURN, t->line);
        if (!check(P, T_SEMI) && !check(P, T_RBRACE) && !check(P, T_EOF))
            n->a = parse_expr(P);
        if (check(P, T_SEMI)) advance(P);
        return n;
    }

    Node *e = parse_expr(P);
    if (check(P, T_ASSIGN)) {
        advance(P);
        Node *rhs = parse_expr(P);
        Node *n = new_node(N_LET, t->line);
        if (e->type == N_VAR) { n->name = e->name; n->a = rhs; }
        else if (e->type == N_INDEX) { n->name = e->name; n->b = e->a; n->c = rhs; n->a = rhs; }
        else ERR("赋值左侧必须是变量或数组元素");
        if (check(P, T_SEMI)) advance(P);
        return n;
    }
    if (check(P, T_PLUSEQ) || check(P, T_MINUSEQ) ||
        check(P, T_STAREQ) || check(P, T_SLASHEQ)) {
        TokType op = advance(P)->type;
        Node *rhs = parse_expr(P);
        Node *n = new_node(N_LET, t->line);
        if (e->type != N_VAR) ERR("复合赋值左侧必须是变量");
        n->name = e->name;
        Node *v = new_node(N_VAR, t->line); v->name = e->name;
        Node *b = new_node(N_BINOP, t->line);
        b->op = (op == T_PLUSEQ) ? T_PLUS :
                (op == T_MINUSEQ) ? T_MINUS :
                (op == T_STAREQ) ? T_STAR : T_SLASH;
        b->a = v; b->b = rhs;
        n->a = b;
        if (check(P, T_SEMI)) advance(P);
        return n;
    }
    Node *n = new_node(N_EXPRSTMT, t->line);
    n->a = e;
    if (check(P, T_SEMI)) advance(P);
    return n;
}

/* ============ 运行时 ============ */
typedef struct Var {
    char *name;
    Value val;
    struct Var *next;
} Var;

typedef struct Scope {
    Var *vars;
    struct Scope *parent;
} Scope;

typedef struct Func {
    char *name;
    char **params; int param_n;
    Node *body;
    struct Func *next;
} Func;

static Scope *global_scope = NULL;
static Func  *funcs = NULL;

static Var *scope_find(Scope *s, const char *name) {
    for (; s; s = s->parent)
        for (Var *v = s->vars; v; v = v->next)
            if (!strcmp(v->name, name)) return v;
    return NULL;
}

static Var *scope_find_local(Scope *s, const char *name) {
    for (Var *v = s->vars; v; v = v->next)
        if (!strcmp(v->name, name)) return v;
    return NULL;
}

static Var *scope_declare(Scope *s, const char *name) {
    Var *v = calloc(1, sizeof(Var));
    v->name = strdup(name);
    v->val.type = V_INT;
    v->next = s->vars;
    s->vars = v;
    return v;
}

static Func *find_func(const char *name) {
    for (Func *f = funcs; f; f = f->next)
        if (!strcmp(f->name, name)) return f;
    return NULL;
}

typedef enum { F_NORMAL, F_RETURN, F_BREAK, F_CONTINUE } Flow;
static Value g_return_value;

static Value eval(Node *n, Scope *sc);
static Flow exec(Node *n, Scope *sc);

static Value make_int(int v) { Value x = {0}; x.type = V_INT; x.i = v; return x; }
static Value make_str(const char *s) { Value x = {0}; x.type = V_STR; x.s = strdup(s ? s : ""); return x; }
static Value make_arr(Array *a) { Value x = {0}; x.type = V_ARR; x.a = a; return x; }

static int to_int(Value v) {
    if (v.type == V_INT) return v.i;
    if (v.type == V_STR) return atoi(v.s);
    return 0;
}

static const char *to_str(Value v) {
    static char buf[64];
    if (v.type == V_STR) return v.s;
    if (v.type == V_INT) { snprintf(buf, sizeof buf, "%d", v.i); return buf; }
    return "[数组]";
}

/* ============ SDL2 图形后端 ============ */
#ifndef NO_GRAPHICS
#include <SDL2/SDL.h>

static SDL_Window   *g_win = NULL;
static SDL_Renderer *g_ren = NULL;
static int g_mouse_x = 0, g_mouse_y = 0;
static int g_mouse_click = 0;

static void ensure_gwin(void) {
    if (!g_win) ERR("图形未初始化, 请在 @gfx 模式运行");
}

static void set_color(uint32_t rgb) {
    int r = (rgb >> 16) & 0xFF;
    int g = (rgb >> 8)  & 0xFF;
    int b =  rgb        & 0xFF;
    SDL_SetRenderDrawColor(g_ren, r, g, b, 255);
}

static void gfx_rect(int x, int y, int w, int h, uint32_t color) {
    ensure_gwin(); set_color(color);
    SDL_Rect r = { x, y, w, h };
    SDL_RenderFillRect(g_ren, &r);
}

static void gfx_line(int x1, int y1, int x2, int y2, uint32_t color) {
    ensure_gwin(); set_color(color);
    SDL_RenderDrawLine(g_ren, x1, y1, x2, y2);
}

static void gfx_circle(int cx, int cy, int r, uint32_t color) {
    ensure_gwin(); set_color(color);
    for (int y = -r; y <= r; y++) {
        int dx = (int)sqrt((double)(r*r - y*y));
        SDL_RenderDrawLine(g_ren, cx - dx, cy + y, cx + dx, cy + y);
    }
}

static void gfx_clear(uint32_t color) {
    ensure_gwin(); set_color(color);
    SDL_RenderClear(g_ren);
}

static void gfx_present(void) { if (g_ren) SDL_RenderPresent(g_ren); }
static void gfx_sleep(int ms) { SDL_Delay(ms); }

static void gfx_quit(void) {
    if (g_ren) { SDL_DestroyRenderer(g_ren); g_ren = NULL; }
    if (g_win) { SDL_DestroyWindow(g_win);   g_win = NULL; }
    SDL_Quit();
}

static int gfx_poll(void) {
    if (!g_win) return 0;
    SDL_Event e;
    g_mouse_click = 0;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) return 0;
        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) return 0;
        if (e.type == SDL_MOUSEMOTION) { g_mouse_x = e.motion.x; g_mouse_y = e.motion.y; }
        if (e.type == SDL_MOUSEBUTTONDOWN) {
            g_mouse_x = e.button.x; g_mouse_y = e.button.y;
            g_mouse_click = 1;
        }
    }
    return 1;
}

static int gfx_mousex(void) { int x,y; SDL_GetMouseState(&x,&y); return x; }
static int gfx_mousey(void) { int x,y; SDL_GetMouseState(&x,&y); return y; }
static int gfx_key(int k) {
    const Uint8 *st = SDL_GetKeyboardState(NULL);
    if (k < 0 || k >= SDL_NUM_SCANCODES) return 0;
    return st[k] ? 1 : 0;
}
#else
static void gfx_rect(int x,int y,int w,int h,uint32_t c){(void)x;(void)y;(void)w;(void)h;(void)c;
    fprintf(stderr,"[HML] 图形未启用, 请用 -lSDL2 编译\n");}
static void gfx_line(int a,int b,int c,int d,uint32_t e){(void)a;(void)b;(void)c;(void)d;(void)e;}
static void gfx_circle(int x,int y,int r,uint32_t c){(void)x;(void)y;(void)r;(void)c;}
static void gfx_clear(uint32_t c){(void)c;}
static void gfx_present(void){}
static void gfx_sleep(int ms){(void)ms;}
static void gfx_quit(void){}
static int  gfx_poll(void){return 0;}
static int  gfx_mousex(void){return 0;}
static int  gfx_mousey(void){return 0;}
static int  gfx_key(int k){(void)k;return 0;}
#endif

/* ============ 内置函数 ============ */
typedef Value (*BuiltinFn)(Node *n, Scope *sc);
typedef struct { const char *name; BuiltinFn fn; int arity; } Builtin;

static int arg_int(Node *n, Scope *sc, int i) {
    return to_int(eval(n->list[i], sc));
}

static Value bi_abs(Node *n, Scope *sc)     { int v = arg_int(n,sc,0); return make_int(v<0?-v:v); }
static Value bi_sqrt(Node *n, Scope *sc)    { return make_int((int)sqrt((double)arg_int(n,sc,0))); }
static Value bi_min(Node *n, Scope *sc)     { int a=arg_int(n,sc,0),b=arg_int(n,sc,1); return make_int(a<b?a:b); }
static Value bi_max(Node *n, Scope *sc)     { int a=arg_int(n,sc,0),b=arg_int(n,sc,1); return make_int(a>b?a:b); }
static Value bi_pow(Node *n, Scope *sc)     { int a=arg_int(n,sc,0),b=arg_int(n,sc,1);
                                              int r=1; for(int i=0;i<b;i++) r*=a; return make_int(r); }
static Value bi_rand(Node *n, Scope *sc)    { (void)n;(void)sc; return make_int(rand()); }
static Value bi_randrange(Node *n, Scope *sc) {
    int a=arg_int(n,sc,0), b=arg_int(n,sc,1);
    if (b <= a) return make_int(a);
    return make_int(a + rand() % (b - a));
}
static Value bi_len(Node *n, Scope *sc) {
    Value v = eval(n->list[0], sc);
    if (v.type == V_STR) return make_int((int)strlen(v.s));
    if (v.type == V_ARR) return make_int(v.a->n);
    return make_int(0);
}
static Value bi_substr(Node *n, Scope *sc) {
    Value s = eval(n->list[0], sc);
    int a = arg_int(n,sc,1), b = arg_int(n,sc,2);
    if (s.type != V_STR) return make_str("");
    int len = (int)strlen(s.s);
    if (a < 0) a = 0; if (b > len) b = len; if (b < a) b = a;
    char buf[4096];
    int k = b - a;
    if (k > 4095) k = 4095;
    memcpy(buf, s.s + a, k); buf[k] = 0;
    return make_str(buf);
}
static Value bi_find(Node *n, Scope *sc) {
    Value s = eval(n->list[0], sc);
    Value sub = eval(n->list[1], sc);
    if (s.type != V_STR) return make_int(-1);
    char *p = strstr(s.s, to_str(sub));
    return make_int(p ? (int)(p - s.s) : -1);
}
static Value bi_upper(Node *n, Scope *sc) {
    Value s = eval(n->list[0], sc);
    if (s.type != V_STR) return s;
    char buf[4096]; int i=0;
    for (char *p=s.s; *p && i<4095; p++) buf[i++] = toupper((unsigned char)*p);
    buf[i] = 0; return make_str(buf);
}
static Value bi_lower(Node *n, Scope *sc) {
    Value s = eval(n->list[0], sc);
    if (s.type != V_STR) return s;
    char buf[4096]; int i=0;
    for (char *p=s.s; *p && i<4095; p++) buf[i++] = tolower((unsigned char)*p);
    buf[i] = 0; return make_str(buf);
}
static Value bi_int(Node *n, Scope *sc) {
    Value v = eval(n->list[0], sc);
    return make_int(to_int(v));
}
static Value bi_str(Node *n, Scope *sc) {
    Value v = eval(n->list[0], sc);
    return make_str(to_str(v));
}
static Value bi_push(Node *n, Scope *sc) {
    Value a = eval(n->list[0], sc);
    if (a.type != V_ARR) ERR("push 第一个参数必须是数组");
    arr_push(a.a, eval(n->list[1], sc));
    return make_int(a.a->n);
}
static Value bi_pop(Node *n, Scope *sc) {
    Value a = eval(n->list[0], sc);
    if (a.type != V_ARR || a.a->n == 0) return make_int(0);
    return a.a->items[--a.a->n];
}
static Value bi_insert(Node *n, Scope *sc) {
    Value a = eval(n->list[0], sc);
    int i = arg_int(n,sc,1);
    if (a.type != V_ARR || i < 0 || i > a.a->n) return make_int(0);
    Value v = eval(n->list[2], sc);
    arr_push(a.a, v);
    for (int k = a.a->n - 1; k > i; k--) a.a->items[k] = a.a->items[k-1];
    a.a->items[i] = v;
    return make_int(a.a->n);
}
static Value bi_remove(Node *n, Scope *sc) {
    Value a = eval(n->list[0], sc);
    int i = arg_int(n,sc,1);
    if (a.type != V_ARR || i < 0 || i >= a.a->n) return make_int(0);
    Value old = a.a->items[i];
    for (int k = i; k < a.a->n - 1; k++) a.a->items[k] = a.a->items[k+1];
    a.a->n--;
    return old;
}
static Value bi_at(Node *n, Scope *sc) {
    Value a = eval(n->list[0], sc);
    int i = arg_int(n,sc,1);
    if (a.type != V_ARR || i < 0 || i >= a.a->n) return make_int(0);
    return a.a->items[i];
}
static Value bi_cls(Node *n, Scope *sc) {
    (void)n;(void)sc;
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif
    return make_int(0);
}
static Value bi_args(Node *n, Scope *sc) {
    (void)n;(void)sc;
    Array *a = calloc(1, sizeof(Array));
    for (int i = 0; i < g_argc; i++) arr_push(a, make_str(g_argv[i]));
    return make_arr(a);
}
static Value bi_argc(Node *n, Scope *sc) { (void)n;(void)sc; return make_int(g_argc); }
static Value bi_sleep(Node *n, Scope *sc) {
    int ms = arg_int(n,sc,0);
#ifdef _WIN32
    Sleep(ms);
#else
    struct timespec ts = { ms/1000, (ms%1000)*1000000L };
    nanosleep(&ts, NULL);
#endif
    return make_int(0);
}
static Value bi_now(Node *n, Scope *sc) { (void)n;(void)sc; return make_int((int)time(NULL)); }

/* v2.1 新增内置 */
static Value bi_exit(Node *n, Scope *sc) {
    (void)n;(void)sc;
    exit(0);
    return make_int(0);
}

static Value bi_printn(Node *n, Scope *sc) {
    Value v = eval(n->list[0], sc);
    if (v.type == V_STR)      printf("%s", v.s);
    else if (v.type == V_INT) printf("%d", v.i);
    else {
        printf("[");
        for (int i = 0; i < v.a->n; i++) {
            if (i) printf(", ");
            if (v.a->items[i].type == V_STR) printf("\"%s\"", v.a->items[i].s);
            else printf("%d", v.a->items[i].i);
        }
        printf("]");
    }
    fflush(stdout);
    return make_int(0);
}

static Value bi_startswith(Node *n, Scope *sc) {
    Value s = eval(n->list[0], sc);
    Value p = eval(n->list[1], sc);
    if (s.type != V_STR || p.type != V_STR) return make_int(0);
    return make_int(strncmp(s.s, p.s, strlen(p.s)) == 0);
}

static Value bi_endswith(Node *n, Scope *sc) {
    Value s = eval(n->list[0], sc);
    Value p = eval(n->list[1], sc);
    if (s.type != V_STR || p.type != V_STR) return make_int(0);
    size_t slen = strlen(s.s), plen = strlen(p.s);
    if (plen > slen) return make_int(0);
    return make_int(strcmp(s.s + slen - plen, p.s) == 0);
}

/* 图形 builtin */
static Value bi_gwin(Node *n, Scope *sc) {
#ifndef NO_GRAPHICS
    if (g_win) return make_int(0);
    int w = arg_int(n,sc,0), h = arg_int(n,sc,1);
    Value t = eval(n->list[2], sc);
    if (SDL_Init(SDL_INIT_VIDEO) != 0) ERR("SDL 初始化失败: %s", SDL_GetError());
    g_win = SDL_CreateWindow(t.type==V_STR?t.s:"HML",
                             SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             w, h, SDL_WINDOW_SHOWN);
    if (!g_win) ERR("创建窗口失败: %s", SDL_GetError());
    g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED);
    if (!g_ren) ERR("创建渲染器失败: %s", SDL_GetError());
#else
    (void)n;(void)sc;
    fprintf(stderr, "[HML] 图形未启用\n");
#endif
    return make_int(0);
}
static Value bi_gclear(Node *n, Scope *sc)  { gfx_clear((uint32_t)arg_int(n,sc,0)); return make_int(0); }
static Value bi_grect(Node *n, Scope *sc) {
    gfx_rect(arg_int(n,sc,0), arg_int(n,sc,1), arg_int(n,sc,2),
             arg_int(n,sc,3), (uint32_t)arg_int(n,sc,4));
    return make_int(0);
}
static Value bi_gcircle(Node *n, Scope *sc) {
    gfx_circle(arg_int(n,sc,0), arg_int(n,sc,1), arg_int(n,sc,2),
               (uint32_t)arg_int(n,sc,3));
    return make_int(0);
}
static Value bi_gline(Node *n, Scope *sc) {
    gfx_line(arg_int(n,sc,0), arg_int(n,sc,1), arg_int(n,sc,2),
             arg_int(n,sc,3), (uint32_t)arg_int(n,sc,4));
    return make_int(0);
}
static Value bi_gpresent(Node *n, Scope *sc) { (void)n;(void)sc; gfx_present(); return make_int(0); }
static Value bi_gpoll(Node *n, Scope *sc)    { (void)n;(void)sc; return make_int(gfx_poll()); }
static Value bi_gsleep(Node *n, Scope *sc)   { gfx_sleep(arg_int(n,sc,0)); return make_int(0); }
static Value bi_gquit(Node *n, Scope *sc)    { (void)n;(void)sc; gfx_quit(); return make_int(0); }
static Value bi_mousex(Node *n, Scope *sc)   { (void)n;(void)sc; return make_int(gfx_mousex()); }
static Value bi_mousey(Node *n, Scope *sc)   { (void)n;(void)sc; return make_int(gfx_mousey()); }
static Value bi_key(Node *n, Scope *sc)      { return make_int(gfx_key(arg_int(n,sc,0))); }

static Builtin builtins[] = {
    {"abs", bi_abs, 1}, {"sqrt", bi_sqrt, 1}, {"min", bi_min, 2}, {"max", bi_max, 2},
    {"pow", bi_pow, 2}, {"rand", bi_rand, 0}, {"randrange", bi_randrange, 2},
    {"len", bi_len, 1}, {"substr", bi_substr, 3}, {"find", bi_find, 2},
    {"upper", bi_upper, 1}, {"lower", bi_lower, 1}, {"int", bi_int, 1}, {"str", bi_str, 1},
    {"startswith", bi_startswith, 2}, {"endswith", bi_endswith, 2},
    {"push", bi_push, 2}, {"pop", bi_pop, 1}, {"insert", bi_insert, 3},
    {"remove", bi_remove, 2}, {"at", bi_at, 2},
    {"cls", bi_cls, 0}, {"args", bi_args, 0}, {"argc", bi_argc, 0},
    {"sleep", bi_sleep, 1}, {"now", bi_now, 0},
    {"exit", bi_exit, 0}, {"printn", bi_printn, 1},
    {"gwin", bi_gwin, 3}, {"gclear", bi_gclear, 1}, {"grect", bi_grect, 5},
    {"gcircle", bi_gcircle, 4}, {"gline", bi_gline, 5}, {"gpresent", bi_gpresent, 0},
    {"gpoll", bi_gpoll, 0}, {"gsleep", bi_gsleep, 1}, {"gquit", bi_gquit, 0},
    {"mousex", bi_mousex, 0}, {"mousey", bi_mousey, 0}, {"key", bi_key, 1},
    {NULL,NULL,0}
};

static Builtin *find_builtin(const char *name) {
    for (Builtin *b = builtins; b->name; b++)
        if (!strcmp(b->name, name)) return b;
    return NULL;
}

/* ============ 求值 ============ */
static Value eval(Node *n, Scope *sc) {
    cur_line = n->line;
    switch (n->type) {
        case N_NUM: return make_int(n->num);
        case N_STR: return make_str(n->str);
        case N_ARRAY: {
            Array *a = calloc(1, sizeof(Array));
            for (int i = 0; i < n->list_n; i++) arr_push(a, eval(n->list[i], sc));
            return make_arr(a);
        }
        case N_VAR: {
            Var *v = scope_find(sc, n->name);
            if (!v) ERR("未定义变量 '%s'", n->name);
            return v->val;
        }
        case N_INDEX: {
            Var *v = scope_find(sc, n->name);
            if (!v || v->val.type != V_ARR) ERR("'%s' 不是数组", n->name);
            int idx = to_int(eval(n->a, sc));
            if (idx < 0 || idx >= v->val.a->n) ERR("下标越界 '%s[%d]'", n->name, idx);
            return v->val.a->items[idx];
        }
        case N_BINOP: {
            if (n->op == T_NOT) {
                Value R = eval(n->b, sc);
                return make_int(!to_int(R));
            }
            if (n->op == T_AND) {
                Value L = eval(n->a, sc);
                if (!to_int(L)) return make_int(0);
                return make_int(to_int(eval(n->b, sc)) ? 1 : 0);
            }
            if (n->op == T_OR) {
                Value L = eval(n->a, sc);
                if (to_int(L)) return make_int(1);
                return make_int(to_int(eval(n->b, sc)) ? 1 : 0);
            }
            Value L = eval(n->a, sc), R = eval(n->b, sc);
            if (L.type == V_STR || R.type == V_STR) {
                if (n->op == T_PLUS) {
                    char out[16384];
                    snprintf(out, sizeof out, "%s%s", to_str(L), to_str(R));
                    return make_str(out);
                }
                if (n->op == T_EQ) return make_int(strcmp(to_str(L), to_str(R)) == 0);
                if (n->op == T_NEQ) return make_int(strcmp(to_str(L), to_str(R)) != 0);
                ERR("字符串不支持该运算");
            }
            int a = to_int(L), b = to_int(R);
            switch (n->op) {
                case T_PLUS:  return make_int(a + b);
                case T_MINUS: return make_int(a - b);
                case T_STAR:  return make_int(a * b);
                case T_SLASH: if (b==0) ERR("除以零"); return make_int(a / b);
                case T_EQ:    return make_int(a == b);
                case T_NEQ:   return make_int(a != b);
                case T_LT:    return make_int(a <  b);
                case T_GT:    return make_int(a >  b);
                case T_LE:    return make_int(a <= b);
                case T_GE:    return make_int(a >= b);
            }
            ERR("未知运算符");
        }
        case N_CALL: {
            Builtin *b = find_builtin(n->name);
            if (b) {
                if (n->list_n != b->arity)
                    ERR("内置函数 '%s' 需要 %d 个参数", n->name, b->arity);
                return b->fn(n, sc);
            }
            Func *f = find_func(n->name);
            if (!f) ERR("未定义函数 '%s'", n->name);
            if (n->list_n != f->param_n)
                ERR("函数 '%s' 需要 %d 个参数", n->name, f->param_n);
            Scope *child = calloc(1, sizeof(Scope));
            child->parent = global_scope;
            for (int i = 0; i < f->param_n; i++) {
                Var *v = scope_declare(child, f->params[i]);
                v->val = eval(n->list[i], sc);
            }
            Flow fl = exec(f->body, child);
            if (fl == F_RETURN) return g_return_value;
            return make_int(0);
        }
        default: ERR("非法表达式");
    }
    return make_int(0);
}

static void exec_ui(Node *n, Scope *sc) {
    for (int i = 0; i < n->list_n; i++) {
        Node *w = n->list[i];
        int x = w->x ? to_int(eval(w->x, sc)) : 0;
        int y = w->y ? to_int(eval(w->y, sc)) : 0;
        int ww = w->w ? to_int(eval(w->w, sc)) : 0;
        int hh = w->h ? to_int(eval(w->h, sc)) : 0;
        int c = w->color ? to_int(eval(w->color, sc)) : 0xFFFFFF;
        if (!strcmp(w->widget_kind, "rect")) {
            gfx_rect(x, y, ww, hh, (uint32_t)c);
        } else if (!strcmp(w->widget_kind, "circle")) {
            gfx_circle(x, y, ww, (uint32_t)c);
        } else if (!strcmp(w->widget_kind, "label") || !strcmp(w->widget_kind, "text")) {
            gfx_rect(x, y, 10, 10, (uint32_t)c);
        } else if (!strcmp(w->widget_kind, "button")) {
            gfx_rect(x, y, 80, 30, 0x444444);
            gfx_rect(x + 2, y + 2, 76, 26, 0x888888);
        }
    }
}

static Flow exec(Node *n, Scope *sc) {
    cur_line = n->line;
    switch (n->type) {
        case N_BLOCK: {
            for (int i = 0; i < n->list_n; i++) {
                Flow f = exec(n->list[i], sc);
                if (f != F_NORMAL) return f;
            }
            return F_NORMAL;
        }
        case N_UI:
            exec_ui(n, sc);
            return F_NORMAL;
        case N_LET: {
            if (n->b && n->c) {
                Var *v = scope_find(sc, n->name);
                if (!v || v->val.type != V_ARR) ERR("未定义数组 '%s'", n->name);
                int idx = to_int(eval(n->b, sc));
                if (idx < 0 || idx >= v->val.a->n) ERR("下标越界");
                v->val.a->items[idx] = eval(n->c, sc);
                return F_NORMAL;
            }
            Var *v = scope_find(sc, n->name);
            if (!v) v = scope_declare(sc, n->name);
            v->val = eval(n->a, sc);
            return F_NORMAL;
        }
        case N_PRINT: {
            Value v = eval(n->a, sc);
            if (v.type == V_STR) printf("%s\n", v.s);
            else if (v.type == V_INT) printf("%d\n", v.i);
            else {
                printf("[");
                for (int i = 0; i < v.a->n; i++) {
                    if (i) printf(", ");
                    if (v.a->items[i].type == V_STR) printf("\"%s\"", v.a->items[i].s);
                    else printf("%d", v.a->items[i].i);
                }
                printf("]\n");
            }
            return F_NORMAL;
        }
        case N_INPUT: {
            char buf[1024];
            if (!fgets(buf, sizeof buf, stdin)) return F_NORMAL;
            buf[strcspn(buf, "\n")] = 0;
            Var *v = scope_find_local(sc, n->name);
            if (!v) v = scope_declare(sc, n->name);
            char *end;
            long iv = strtol(buf, &end, 10);
            if (*end == 0 && end != buf) v->val = make_int((int)iv);
            else                          v->val = make_str(buf);
            return F_NORMAL;
        }
        case N_IF: {
            if (to_int(eval(n->a, sc))) return exec(n->b, sc);
            else if (n->c)              return exec(n->c, sc);
            return F_NORMAL;
        }
        case N_WHILE: {
            while (to_int(eval(n->a, sc))) {
                Flow f = exec(n->b, sc);
                if (f == F_RETURN) return f;
                if (f == F_BREAK) return F_NORMAL;
                if (f == F_CONTINUE) continue;
            }
            return F_NORMAL;
        }
        case N_LOOP: {
            if (n->name) {
                int A = to_int(eval(n->a, sc));
                int B = to_int(eval(n->b, sc));
                Var *v = scope_find_local(sc, n->name);
                if (!v) v = scope_declare(sc, n->name);
                for (int i = A; i <= B; i++) {
                    v->val = make_int(i);
                    Flow f = exec(n->c, sc);
                    if (f == F_RETURN) return f;
                    if (f == F_BREAK) return F_NORMAL;
                    if (f == F_CONTINUE) continue;
                }
            } else {
                int cnt = to_int(eval(n->a, sc));
                for (int i = 0; i < cnt; i++) {
                    Flow f = exec(n->b, sc);
                    if (f == F_RETURN) return f;
                    if (f == F_BREAK) return F_NORMAL;
                    if (f == F_CONTINUE) continue;
                }
            }
            return F_NORMAL;
        }
        case N_FOR: {
            int A = to_int(eval(n->a, sc));
            int B = to_int(eval(n->b, sc));
            int S = n->c ? to_int(eval(n->c, sc)) : 1;
            if (S == 0) ERR("for 步长不能为 0");
            Var *v = scope_find_local(sc, n->name);
            if (!v) v = scope_declare(sc, n->name);
            if (S > 0) {
                for (int i = A; i <= B; i += S) {
                    v->val = make_int(i);
                    Flow f = exec(n->d, sc);
                    if (f == F_RETURN) return f;
                    if (f == F_BREAK) return F_NORMAL;
                    if (f == F_CONTINUE) continue;
                }
            } else {
                for (int i = A; i >= B; i += S) {
                    v->val = make_int(i);
                    Flow f = exec(n->d, sc);
                    if (f == F_RETURN) return f;
                    if (f == F_BREAK) return F_NORMAL;
                    if (f == F_CONTINUE) continue;
                }
            }
            return F_NORMAL;
        }
        case N_FOREACH: {
            Value arr = eval(n->a, sc);
            if (arr.type != V_ARR) ERR("foreach 需要数组");
            Var *v = scope_find_local(sc, n->name);
            if (!v) v = scope_declare(sc, n->name);
            for (int i = 0; i < arr.a->n; i++) {
                v->val = arr.a->items[i];
                Flow f = exec(n->b, sc);
                if (f == F_RETURN) return f;
                if (f == F_BREAK) return F_NORMAL;
                if (f == F_CONTINUE) continue;
            }
            return F_NORMAL;
        }
        case N_BREAK:    return F_BREAK;
        case N_CONTINUE: return F_CONTINUE;
        case N_FUNC:     return F_NORMAL;
        case N_RETURN:
            g_return_value = n->a ? eval(n->a, sc) : make_int(0);
            return F_RETURN;
        case N_EXPRSTMT:
            eval(n->a, sc);
            return F_NORMAL;
        default: ERR("未知语句类型");
    }
    return F_NORMAL;
}

static void collect_funcs(Node *n) {
    if (!n) return;
    if (n->type == N_FUNC) {
        Func *f = calloc(1, sizeof(Func));
        f->name = n->name; f->params = n->params;
        f->param_n = n->param_n; f->body = n->a;
        f->next = funcs; funcs = f;
        return;
    }
    if (n->type == N_BLOCK)
        for (int i = 0; i < n->list_n; i++) collect_funcs(n->list[i]);
}

/* ============ 模式检测 ============ */
typedef enum { MODE_CMD, MODE_GFX, MODE_MIX } RunMode;

static RunMode detect_mode(const char *src, int *w, int *h, char **title) {
    *w = 800; *h = 600; *title = strdup("HML");
    if (src[0] != '@') return MODE_CMD;

    const char *line_end = strchr(src, '\n');
    if (!line_end) line_end = src + strlen(src);
    char head[256];
    int n = (int)(line_end - src);
    if (n > 255) n = 255;
    memcpy(head, src, n);
    head[n] = 0;

    if (strncmp(head, "@gfx", 4) == 0) {
        sscanf(head + 4, "%d %d", w, h);
        char *q1 = strchr(head, '"');
        if (q1) {
            char *q2 = strrchr(head, '"');
            if (q2 && q2 > q1) {
                free(*title);
                int len = (int)(q2 - q1 - 1);
                *title = malloc(len + 1);
                memcpy(*title, q1 + 1, len);
                (*title)[len] = 0;
            }
        }
        return MODE_GFX;
    }
    if (strncmp(head, "@mix", 4) == 0) return MODE_MIX;
    return MODE_CMD;
}

static char *strip_mode_header(const char *src) {
    if (src[0] != '@') return strdup(src);
    const char *nl = strchr(src, '\n');
    if (!nl) return strdup("\n");
    int prefix = (int)(nl - src);
    char *out = malloc(strlen(src) + 1);
    memset(out, ' ', prefix);
    strcpy(out + prefix, nl);
    return out;
}

/* ============ main ============ */
int main(int argc, char **argv) {
#ifdef _WIN32
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
#endif
    srand((unsigned)time(NULL));

    if (argc < 2) {
        printf("HML — Hybrid Markup Language v2.1\n");
        printf("用法: hml <file.hml> [args...]\n\n");
        printf("模式头(文件第一行):\n");
        printf("  @cmd                  命令行\n");
        printf("  @gfx 800 600 \"标题\"    图形\n");
        printf("  @mix                  混合\n\n");
        printf("v2.1 新增: continue / exit() / printn() / startswith() / endswith()\n");
        return 1;
    }

    const char *filename = argv[1];
    const char *ext = strrchr(filename, '.');
    if (!ext || strcmp(ext, ".hml") != 0) {
        fprintf(stderr, "[HML] 只支持 .hml 后缀\n");
        return 1;
    }

    g_argc = argc - 1;
    g_argv = argv + 1;

    FILE *fp = fopen(filename, "rb");
    if (!fp) { perror("打开文件失败"); return 1; }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *raw = malloc(sz + 1);
    if (fread(raw, 1, sz, fp) != (size_t)sz) { }
    raw[sz] = 0;
    fclose(fp);

    char *src0 = raw;
    if (sz >= 3 && (unsigned char)raw[0] == 0xEF
                && (unsigned char)raw[1] == 0xBB
                && (unsigned char)raw[2] == 0xBF) {
        src0 = raw + 3;
    }

    int W, H; char *TITLE;
    RunMode mode = detect_mode(src0, &W, &H, &TITLE);

#ifndef NO_GRAPHICS
    if (mode == MODE_GFX || mode == MODE_MIX) {
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            fprintf(stderr, "[HML] SDL 初始化失败: %s\n", SDL_GetError());
            return 1;
        }
        g_win = SDL_CreateWindow(TITLE,
                                 SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                 W, H, SDL_WINDOW_SHOWN);
        if (!g_win) { fprintf(stderr, "[HML] 创建窗口失败\n"); return 1; }
        g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED);
        if (!g_ren) { fprintf(stderr, "[HML] 创建渲染器失败\n"); return 1; }
    }
#else
    if (mode == MODE_GFX || mode == MODE_MIX) {
        fprintf(stderr, "[HML] 本次编译未启用图形, 请用 -lSDL2 重新编译\n");
        return 1;
    }
#endif

    char *src = strip_mode_header(src0);

    Lexer L = { .src = src };
    lex_tokenize(&L);

    Parser P = { .toks = L.toks, .count = L.count };
    Node *prog = new_node(N_BLOCK, 0);
    Node **list = NULL; int ln = 0, lc = 0;
    while (!check(&P, T_EOF)) {
        if (ln >= lc) { lc = lc?lc*2:8; list = realloc(list, sizeof(Node*)*lc); }
        list[ln++] = parse_stmt(&P);
    }
    prog->list = list; prog->list_n = ln;

    global_scope = calloc(1, sizeof(Scope));
    collect_funcs(prog);
    exec(prog, global_scope);

#ifndef NO_GRAPHICS
    gfx_quit();
#endif
    free(TITLE);
    return 0;
}