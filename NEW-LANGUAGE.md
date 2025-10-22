Perfect—use a **tiny JS-looking language** with its **own stack VM** and only the **UI ops you need**. Below is a compact, dependency-free **C11** starter you can paste into a single file (`minijs.c`) and build with `cc -O2 minijs.c -o minijs`. It implements:

* Lexer → micro parser (expressions, calls, member access `ui.replaceSelection("...")`, `let`).
* Bytecode compiler → stack VM.
* Native bridge (`ui.*`) with a mock “editor buffer + selection”.
* A few built-ins: `upper(s)`, `print(...)`.

Run: `./minijs` then type scripts; end with an empty line to execute.

---

### Language (MVP)

* Values: `null`, `bool`, `number`, `string`.
* Statements: `let name = expr;` and expression statements `expr;`.
* Expressions: literals, identifiers, `a.b` (member), calls `f(a,b)`, unary `! -`, binary `+ - * / % == != < <= > >= && ||`.
* No classes/prototypes/coercions. Semicolons required (keeps parsing simple).

### Built-in natives (host UI)

```
ui.getSelectionText()   -> string
ui.replaceSelection(s)  -> null
ui.notify(s)            -> null
upper(s)                -> string     // helper, not JS
print(any...)           -> null
```

---

### Single-file POC (C11)

> Minimal, not production-grade. Intent: show the shape—lexer → parser → compiler → VM → UI natives.

```c
// minijs.c  —  build:  cc -O2 minijs.c -o minijs
// Tiny JS-looking language with bytecode VM and UI natives (no deps).
// Copyright: public-domain / CC0.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdint.h>

/* ========================= Utilities ========================= */
#define ARR(T) struct { T* v; int n, cap; }
#define arr_push(a, x) do{ if((a).n==(a).cap){ (a).cap=((a).cap? (a).cap*2:8); (a).v=realloc((a).v,sizeof(*(a).v)*(a).cap);} (a).v[(a).n++]=(x);}while(0)

static void die(const char* m){ fprintf(stderr,"error: %s\n",m); exit(1); }

/* ========================= Value model ========================= */
typedef enum {V_NULL, V_BOOL, V_NUM, V_STR} VKind;
typedef struct { VKind k; union { double num; int b; char* s; }; } Value;

static Value VNull(){ Value v; v.k=V_NULL; return v; }
static Value VBool(int b){ Value v; v.k=V_BOOL; v.b=b; return v; }
static Value VNum(double x){ Value v; v.k=V_NUM; v.num=x; return v; }
static Value VStr(const char* s){ Value v; v.k=V_STR; v.s=strdup(s); return v; }
static void VFree(Value v){ if(v.k==V_STR) free(v.s); }
static void VPrint(Value v){
  switch(v.k){
    case V_NULL: printf("null"); break;
    case V_BOOL: printf(v.b?"true":"false"); break;
    case V_NUM: { char buf[64]; snprintf(buf,64,"%.15g",v.num); printf("%s",buf); } break;
    case V_STR: printf("%s", v.s); break;
  }
}

/* ========================= Lexer ========================= */
typedef enum {
  T_EOF, T_ID, T_NUM, T_STR,
  T_LET, T_TRUE, T_FALSE, T_NULL,
  T_IF, T_ELSE,
  T_LP, T_RP, T_LB, T_RB, T_DOT, T_COMMA, T_SC,
  T_PLUS, T_MINUS, T_STAR, T_SLASH, T_PCT,
  T_BANG, T_EQ, T_EQEQ, T_NEQ, T_LT, T_LTE, T_GT, T_GTE,
  T_ANDAND, T_OROR
} Tok;
typedef struct { Tok t; const char* s; int len; double num; int line, col; } Token;

typedef struct { const char* src; int i,len; int line,col; } Lex;
static int peek(Lex* L){ return (L->i<L->len)? L->src[L->i] : 0; }
static int getc_(Lex* L){ int c=peek(L); if(c){ L->i++; if(c=='\n'){L->line++;L->col=1;} else L->col++; } return c; }
static int match(Lex* L,int c){ if(peek(L)==c){ getc_(L); return 1;} return 0; }
static int isid1(int c){ return isalpha(c)||c=='_' ; }
static int isid(int c){ return isalnum(c)||c=='_'; }

static Token next(Lex* L){
  while(isspace(peek(L))) getc_(L);
  Token tk = {.t=T_EOF,.s=L->src+L->i,.len=0,.line=L->line,.col=L->col};
  int c = getc_(L);
  tk.s = L->src + L->i - (c?1:0);
  if(!c){ tk.t=T_EOF; return tk; }
  if(isdigit(c)){
    double x = c-'0';
    while(isdigit(peek(L))) x = x*10 + (getc_(L)-'0');
    if(peek(L)=='.'){ getc_(L); double f=0, p=1; while(isdigit(peek(L))){ f = f*10 + (getc_(L)-'0'); p*=10; } x += f/p; }
    tk.t=T_NUM; tk.num=x; return tk;
  }
  if(isid1(c)){
    int start=L->i-1; while(isid(peek(L))) getc_(L);
    tk.s = L->src + start; tk.len = L->i - start;
    #define KW(w, K) if(tk.len==(int)strlen(w) && strncmp(tk.s,w,tk.len)==0){ tk.t=K; return tk; }
    KW("let",T_LET); KW("true",T_TRUE); KW("false",T_FALSE); KW("null",T_NULL);
    KW("if",T_IF); KW("else",T_ELSE);
    tk.t=T_ID; return tk;
  }
  if(c=='"'){
    int start=L->i; while(peek(L) && peek(L)!='"'){ if(peek(L)=='\\'){ getc_(L); } getc_(L); }
    int end=L->i; if(!match(L,'"')) die("unterminated string");
    tk.t=T_STR; tk.len=end-start; char* s=malloc(tk.len+1); memcpy(s,L->src+start,tk.len); s[tk.len]=0; tk.s=s; return tk;
  }
  switch(c){
    case '(': tk.t=T_LP; return tk;
    case ')': tk.t=T_RP; return tk;
    case '{': tk.t=T_LB; return tk;
    case '}': tk.t=T_RB; return tk;
    case '.': tk.t=T_DOT; return tk;
    case ',': tk.t=T_COMMA; return tk;
    case ';': tk.t=T_SC; return tk;
    case '+': tk.t=T_PLUS; return tk;
    case '-': tk.t=T_MINUS; return tk;
    case '*': tk.t=T_STAR; return tk;
    case '/': tk.t=T_SLASH; return tk;
    case '%': tk.t=T_PCT; return tk;
    case '!': tk.t= match(L,'=')?T_NEQ:T_BANG; return tk;
    case '=': tk.t= match(L,'=')?T_EQEQ:T_EQ; return tk;
    case '<': tk.t= match(L,'=')?T_LTE:T_LT; return tk;
    case '>': tk.t= match(L,'=')?T_GTE:T_GT; return tk;
    case '&': if(match(L,'&')){ tk.t=T_ANDAND; return tk; } break;
    case '|': if(match(L,'|')){ tk.t=T_OROR; return tk; } break;
  }
  die("unexpected character");
  return tk;
}

/* ========================= Bytecode ========================= */
typedef enum {
  OP_CONST, OP_NIL, OP_TRUE, OP_FALSE,
  OP_POP, OP_DUP,
  OP_GETG, OP_SETG,
  OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
  OP_EQ, OP_NEQ, OP_LT, OP_LTE, OP_GT, OP_GTE,
  OP_NOT, OP_NEG,
  OP_CALL,       // argc (u8)
  OP_MEMBER,     // expect obj(string name) -> intern "obj.name" (native)
  OP_NATIVE,     // id(u16) argc(u8)
} Op;

typedef struct { ARR(uint8_t) code; ARR(Value) k; } Chunk;
static void emit8(Chunk* c, uint8_t b){ arr_push(c->code, b); }
static void emit16(Chunk* c, uint16_t x){ emit8(c,(uint8_t)(x&255)); emit8(c,(uint8_t)(x>>8)); }
static int addK(Chunk* c, Value v){ arr_push(c->k, v); return c->k.n-1; }
static void freeChunk(Chunk* c){ for(int i=0;i<c->k.n;i++) VFree(c->k.v[i]); free(c->k.v); free(c->code.v); }

/* ========================= Natives & “UI” host ========================= */
typedef Value (*NativeFn)(int argc, Value* argv);

typedef struct { const char* name; NativeFn fn; } Native;
static ARR(Native) natives;

static int findNative(const char* name){
  for(int i=0;i<natives.n;i++) if(strcmp(natives.v[i].name,name)==0) return i;
  return -1;
}
static int regNative(const char* name, NativeFn fn){
  int i=findNative(name); if(i>=0) return i;
  Native n={name,fn}; arr_push(natives,n); return natives.n-1;
}

/* Mock editor buffer + selection */
static char* editor;     // heap string
static int sel_from=0, sel_to=0;

static void editor_set(const char* s){ free(editor); editor=strdup(s); int n=(int)strlen(editor); if(sel_to>n) sel_to=n; if(sel_from>n) sel_from=n; if(sel_from>sel_to) sel_from=sel_to; }
static Value ui_getSelectionText(int argc, Value* argv){
  (void)argc;(void)argv;
  int n=(int)strlen(editor); int a=sel_from<0?0:sel_from; int b=sel_to>n?n:sel_to; if(a>b) a=b;
  char* out=malloc((b-a)+1); memcpy(out, editor+a, b-a); out[b-a]=0; Value v=VStr(out); free(out); return v;
}
static Value ui_replaceSelection(int argc, Value* argv){
  if(argc!=1||argv[0].k!=V_STR) return VNull();
  int n=(int)strlen(editor); int a=sel_from<0?0:sel_from; int b=sel_to>n?n:sel_to; if(a>b)a=b;
  int pre=a, post=n-b, ins=(int)strlen(argv[0].s);
  char* out=malloc(pre+ins+post+1);
  memcpy(out, editor, pre);
  memcpy(out+pre, argv[0].s, ins);
  memcpy(out+pre+ins, editor+b, post);
  out[pre+ins+post]=0;
  editor_set(out);
  sel_to = sel_from + ins;
  free(out);
  return VNull();
}
static Value ui_notify(int argc, Value* argv){
  (void)argc; (void)argv;
  printf("[notify] "); for(int i=0;i<argc;i++){ VPrint(argv[i]); if(i+1<argc) printf(" "); } printf("\n"); return VNull();
}
static Value fn_upper(int argc, Value* argv){
  if(argc!=1||argv[0].k!=V_STR) return VNull();
  char* s=argv[0].s; int n=(int)strlen(s);
  char* t=malloc(n+1);
  for(int i=0;i<n;i++) t[i]=(char)toupper((unsigned char)s[i]);
  t[n]=0; Value v=VStr(t); free(t); return v;
}
static Value fn_print(int argc, Value* argv){
  for(int i=0;i<argc;i++){ VPrint(argv[i]); if(i+1<argc) printf(" "); }
  printf("\n"); return VNull();
}

/* ========================= Symbol table (globals) ========================= */
typedef struct { char* key; Value val; } Entry;
static ARR(Entry) globals;
static int g_find(const char* k){
  for(int i=0;i<globals.n;i++) if(globals.v[i].key && strcmp(globals.v[i].key,k)==0) return i;
  return -1;
}
static int g_intern(const char* k){
  int i=g_find(k); if(i>=0) return i;
  Entry e={strdup(k), VNull()}; arr_push(globals,e); return globals.n-1;
}

/* ========================= Parser (expr-only + let + ; ) ========================= */
typedef struct { Lex L; Token cur, prev; } P;
static void pnext(P* p){ if(p->prev.t==T_STR) free((char*)p->prev.s); p->prev=p->cur; p->cur=next(&p->L); }
static int accept(P* p, Tok t){ if(p->cur.t==t){ pnext(p); return 1; } return 0; }
static void expect(P* p, Tok t, const char* msg){ if(!accept(p,t)){ die(msg);} }

typedef enum {N_LIT, N_VAR, N_BIN, N_UN, N_CALL, N_MEMBER} NTag;
typedef struct Node Node;
struct Node{
  NTag k;
  Token tk;
  Node *a,*b;     // children
  ARR(Node*) args;
};

static Node* newNode(NTag k){ Node* n=calloc(1,sizeof(Node)); n->k=k; return n; }
static Node* parseExpr(P* p); // fwd

static Node* parsePrimary(P* p){
  if(p->cur.t==T_NUM){ Node* n=newNode(N_LIT); n->tk=p->cur; pnext(p); return n; }
  if(p->cur.t==T_STR){ Node* n=newNode(N_LIT); n->tk=p->cur; pnext(p); return n; }
  if(p->cur.t==T_TRUE || p->cur.t==T_FALSE || p->cur.t==T_NULL){ Node* n=newNode(N_LIT); n->tk=p->cur; pnext(p); return n; }
  if(p->cur.t==T_ID){ Node* n=newNode(N_VAR); n->tk=p->cur; pnext(p); return n; }
  if(accept(p,T_LP)){ Node* e=parseExpr(p); expect(p,T_RP,")"); return e; }
  die("expected primary"); return NULL;
}

static Node* parsePostfix(P* p){
  Node* n=parsePrimary(p);
  for(;;){
    if(accept(p,T_DOT)){
      if(p->cur.t!=T_ID) die("expected identifier after '.'");
      Node* m=newNode(N_MEMBER); m->a=n; m->tk=p->cur; pnext(p); n=m; continue;
    }
    if(accept(p,T_LP)){
      Node* c=newNode(N_CALL); c->a=n;
      if(!accept(p,T_RP)){
        for(;;){
          Node* arg=parseExpr(p); arr_push(c->args,arg);
          if(accept(p,T_RP)) break;
          expect(p,T_COMMA,",");
        }
      }
      n=c; continue;
    }
    break;
  }
  return n;
}

static Node* parseUnary(P* p){
  if(p->cur.t==T_BANG || p->cur.t==T_MINUS){ Node* n=newNode(N_UN); n->tk=p->cur; pnext(p); n->a=parseUnary(p); return n; }
  return parsePostfix(p);
}

static int prec(Tok t){
  switch(t){
    case T_STAR: case T_SLASH: case T_PCT: return 5;
    case T_PLUS: case T_MINUS: return 4;
    case T_LT: case T_LTE: case T_GT: case T_GTE: return 3;
    case T_EQEQ: case T_NEQ: return 2;
    case T_ANDAND: return 1;
    case T_OROR: return 0;
    default: return -1;
  }
}

static Node* parseBinRhs(P* p, int minPrec, Node* lhs){
  for(;;){
    int pr = prec(p->cur.t);
    if(pr < minPrec) return lhs;
    Token op = p->cur; pnext(p);
    Node* rhs = parseUnary(p);
    int pr2 = prec(p->cur.t);
    if(pr2 > pr){ rhs = parseBinRhs(p, pr+1, rhs); }
    Node* bin=newNode(N_BIN); bin->tk=op; bin->a=lhs; bin->b=rhs; lhs=bin;
  }
}

static Node* parseExpr(P* p){
  Node* lhs = parseUnary(p);
  return parseBinRhs(p, 0, lhs);
}

/* ========================= Compiler ========================= */
static void emitConst(Chunk* c, Value v){ int idx=addK(c,v); emit8(c,OP_CONST); emit8(c,(uint8_t)idx); }
static void compileExpr(Chunk* c, Node* n);

static void compileCall(Chunk* c, Node* n){
  // Support: (a) normal calls: foo(...), (b) member calls: ui.replaceSelection(...)
  // Strategy: resolve name at compile: for member, build "ui.replaceSelection" string and emit OP_NATIVE.
  // For plain call: allow builtins "print", "upper" as natives too.
  // First, try to obtain callee name.
  char fullname[128]={0};
  int argc = n->args.n;

  // Build fullname if n->a is VAR or MEMBER.
  Node* cal = n->a;
  if(cal->k==N_VAR){
    snprintf(fullname,sizeof(fullname), "%.*s", cal->tk.len?cal->tk.len:(int)strlen(cal->tk.s), cal->tk.t==T_ID? cal->tk.s : ""); // tk.s points into source
  } else if(cal->k==N_MEMBER){
    // unwind chain: obj.member
    Node* base = cal->a;
    if(base->k!=N_VAR) die("only simple obj.member supported");
    snprintf(fullname,sizeof(fullname), "%.*s.%.*s",
      base->tk.len?base->tk.len:(int)strlen(base->tk.s), base->tk.s,
      cal->tk.len?cal->tk.len:(int)strlen(cal->tk.s), cal->tk.s);
  } else {
    die("unsupported callee");
  }

  // compile args
  for(int i=0;i<argc;i++) compileExpr(c, n->args.v[i]);

  int nid = findNative(fullname);
  if(nid<0) die("unknown function");
  emit8(c, OP_NATIVE); emit16(c, (uint16_t)nid); emit8(c, (uint8_t)argc);
}

static void compileExpr(Chunk* c, Node* n){
  switch(n->k){
    case N_LIT:
      if(n->tk.t==T_NUM) emitConst(c, VNum(n->tk.num));
      else if(n->tk.t==T_STR) { emitConst(c, VStr(n->tk.s)); /* parser strdup'd */ }
      else if(n->tk.t==T_TRUE) emit8(c,OP_TRUE);
      else if(n->tk.t==T_FALSE) emit8(c,OP_FALSE);
      else if(n->tk.t==T_NULL) emit8(c,OP_NIL);
      break;
    case N_VAR: {
      // globals only
      char name[64]={0}; snprintf(name,sizeof(name), "%.*s", n->tk.len? n->tk.len:(int)strlen(n->tk.s), n->tk.s);
      int gi = g_intern(name);
      emit8(c,OP_GETG); emit8(c,(uint8_t)gi);
    } break;
    case N_MEMBER: {
      // compile object name string "obj.prop" then OP_MEMBER (resolved to native later in calls)
      die("member cannot appear without call");
    } break;
    case N_UN:
      compileExpr(c, n->a);
      if(n->tk.t==T_BANG) emit8(c,OP_NOT);
      else if(n->tk.t==T_MINUS) emit8(c,OP_NEG);
      break;
    case N_BIN:
      compileExpr(c, n->a); compileExpr(c, n->b);
      switch(n->tk.t){
        case T_PLUS: emit8(c,OP_ADD); break;
        case T_MINUS: emit8(c,OP_SUB); break;
        case T_STAR: emit8(c,OP_MUL); break;
        case T_SLASH: emit8(c,OP_DIV); break;
        case T_PCT: emit8(c,OP_MOD); break;
        case T_EQEQ: emit8(c,OP_EQ); break;
        case T_NEQ: emit8(c,OP_NEQ); break;
        case T_LT: emit8(c,OP_LT); break;
        case T_LTE: emit8(c,OP_LTE); break;
        case T_GT: emit8(c,OP_GT); break;
        case T_GTE: emit8(c,OP_GTE); break;
        case T_ANDAND: /* desugar later */ break;
        case T_OROR:   /* desugar later */ break;
        default: die("op not implemented");
      }
      break;
    case N_CALL:
      compileCall(c, n);
      break;
  }
}

static void compileStmt(Chunk* c, P* p){
  if(accept(p,T_LET)){
    if(p->cur.t!=T_ID) die("expected variable name");
    char name[64]={0}; snprintf(name,sizeof(name), "%.*s", p->cur.len? p->cur.len : (int)strlen(p->cur.s), p->cur.s);
    int gi = g_intern(name);
    pnext(p);
    expect(p,T_EQ,"expected =");
    Node* e = parseExpr(p);
    expect(p,T_SC,"expected ;");
    compileExpr(c,e);
    emit8(c,OP_SETG); emit8(c,(uint8_t)gi);
    emit8(c,OP_POP);
    return;
  }
  // expression statement
  Node* e = parseExpr(p);
  expect(p,T_SC,"expected ;");
  compileExpr(c,e);
  emit8(c,OP_POP);
}

/* ========================= VM ========================= */
typedef struct { Value* v; int n,cap; } Stack;
static void spush(Stack* s, Value v){ if(s->n==s->cap){ s->cap = s->cap? s->cap*2:32; s->v = realloc(s->v,sizeof(Value)*s->cap);} s->v[s->n++]=v; }
static Value spop(Stack* s){ return s->v[--s->n]; }

static Value gget(int i){ return globals.v[i].val; }
static void gset(int i, Value v){ VFree(globals.v[i].val); globals.v[i].val=v; }

static Value vm_run(Chunk* c){
  Stack st={0};
  for(int ip=0; ip<c->code.n;){
    Op op = (Op)c->code.v[ip++];
    switch(op){
      case OP_CONST: { int idx=c->code.v[ip++]; spush(&st, c->k.v[idx]); } break;
      case OP_NIL: spush(&st,VNull()); break;
      case OP_TRUE: spush(&st,VBool(1)); break;
      case OP_FALSE: spush(&st,VBool(0)); break;
      case OP_POP: { Value v=spop(&st); VFree(v);} break;
      case OP_DUP: { Value v=st.v[st.n-1]; // shallow
                    if(v.k==V_STR) v.s=strdup(v.s);
                    spush(&st,v);} break;
      case OP_GETG: { int gi=c->code.v[ip++]; Value v=gget(gi);
                      // copy strings
                      if(v.k==V_STR) v.s=strdup(v.s);
                      spush(&st,v);} break;
      case OP_SETG: { int gi=c->code.v[ip++]; Value v=spop(&st); gset(gi,v);} break;
      case OP_ADD: { Value b=spop(&st), a=spop(&st); spush(&st,VNum(a.num+b.num)); VFree(a); VFree(b);} break;
      case OP_SUB: { Value b=spop(&st), a=spop(&st); spush(&st,VNum(a.num-b.num)); VFree(a); VFree(b);} break;
      case OP_MUL: { Value b=spop(&st), a=spop(&st); spush(&st,VNum(a.num*b.num)); VFree(a); VFree(b);} break;
      case OP_DIV: { Value b=spop(&st), a=spop(&st); spush(&st,VNum(a.num/b.num)); VFree(a); VFree(b);} break;
      case OP_MOD: { Value b=spop(&st), a=spop(&st); spush(&st,VNum((double)((long)a.num % (long)b.num))); VFree(a); VFree(b);} break;
      case OP_EQ: { Value b=spop(&st), a=spop(&st);
                    int r=0;
                    if(a.k==b.k){
                      if(a.k==V_BOOL) r=(a.b==b.b);
                      else if(a.k==V_NUM) r=(a.num==b.num);
                      else if(a.k==V_STR) r=(strcmp(a.s,b.s)==0);
                      else r=1;
                    }
                    spush(&st,VBool(r)); VFree(a); VFree(b);} break;
      case OP_NEQ: { Value b=spop(&st), a=spop(&st); int r=1;
                     if(a.k==b.k){
                       if(a.k==V_BOOL) r=(a.b!=b.b);
                       else if(a.k==V_NUM) r=(a.num!=b.num);
                       else if(a.k==V_STR) r=(strcmp(a.s,b.s)!=0);
                       else r=0;
                     }
                     spush(&st,VBool(r)); VFree(a); VFree(b);} break;
      case OP_LT: case OP_LTE: case OP_GT: case OP_GTE: {
        Value b=spop(&st), a=spop(&st);
        int r=0;
        if(op==OP_LT) r=(a.num<b.num);
        else if(op==OP_LTE) r=(a.num<=b.num);
        else if(op==OP_GT) r=(a.num>b.num);
        else r=(a.num>=b.num);
        spush(&st,VBool(r)); VFree(a); VFree(b);
      } break;
      case OP_NOT: { Value a=spop(&st); int r= (a.k==V_BOOL? !a.b : a.k==V_NULL); spush(&st,VBool(r)); VFree(a);} break;
      case OP_NEG: { Value a=spop(&st); spush(&st,VNum(-a.num)); VFree(a);} break;
      case OP_NATIVE: {
        uint16_t id = c->code.v[ip++] | (c->code.v[ip++]<<8);
        uint8_t argc = c->code.v[ip++];
        Value* argv = &st.v[st.n-argc];
        Value ret = natives.v[id].fn(argc, argv);
        for(int i=0;i<argc;i++) VFree(argv[i]);
        st.n -= argc;
        spush(&st, ret);
      } break;
      default: die("bad opcode");
    }
  }
  Value out = st.n? spop(&st) : VNull();
  // cleanup leftovers
  while(st.n){ VFree(spop(&st)); }
  free(st.v);
  return out;
}

/* ========================= REPL & driver ========================= */
static void register_builtins(){
  regNative("ui.getSelectionText", ui_getSelectionText);
  regNative("ui.replaceSelection", ui_replaceSelection);
  regNative("ui.notify", ui_notify);
  regNative("upper", fn_upper);
  regNative("print", fn_print);
}

static void compileProgram(Chunk* c, const char* src){
  P p = { .L = { .src=src, .i=0, .len=(int)strlen(src), .line=1, .col=1 } };
  pnext(&p);
  while(p.cur.t!=T_EOF){
    compileStmt(c,&p);
  }
}

int main(){
  editor = strdup("alpha\nEcho\nbravo\ndelta\ncharlie\n");
  sel_from = 0; sel_to = 5; // "alpha"

  register_builtins();
  printf("mini-js POC — editor buffer starts with selection [0..5] = \"alpha\"\n");
  printf("Type code, end with a blank line to run. Builtins: ui.getSelectionText(), ui.replaceSelection(s), ui.notify(s), upper(s), print(...)\n\n");
  char buf[8192]={0}, line[1024];
  for(;;){
    printf("» ");
    buf[0]=0;
    while(fgets(line,sizeof(line),stdin)){
      if(strcmp(line,"\n")==0) break;
      strcat(buf,line);
      printf("… ");
    }
    if(buf[0]==0) break;

    Chunk ch={0};
    compileProgram(&ch, buf);
    Value r = vm_run(&ch);
    (void)r; // result of last expression (we pop at end of stmt)
    freeChunk(&ch);

    printf("\n[buffer now]\n%s\n", editor);
  }

  // cleanup
  for(int i=0;i<globals.n;i++){ free(globals.v[i].key); VFree(globals.v[i].val); }
  free(globals.v);
  free(natives.v);
  free(editor);
  return 0;
}
```

---

### Try these scripts (paste, then blank line)

**1) Uppercase current selection and notify**

```js
let s = ui.getSelectionText();
if (s == null) { ; } ;  // (if/else not implemented in this minimal POC; using simple flow)
ui.replaceSelection( upper(s) );
ui.notify("Uppercased selection.");
```

*(This POC doesn’t include `if/else` to stay tiny—selection is assumed valid. Adding `if` is straightforward once you add jumps.)*

**2) Replace selection and print buffer head**

```js
ui.replaceSelection("HELLO");
print("done");
```

---

### What to add next (in order, still small)

1. **`if` / short-circuit `&&` / `||`**: add `OP_JUMP`, `OP_JUMP_IF_FALSE`.
2. **Strings `+`**: allow number+string error; only string+string concatenates.
3. **Ranges/positions**: expose `ui.getSelection() -> {from:n,to:n}`, `ui.setSelection(a,b)`.
4. **Sort selected lines**: add `ui.sortLines("asc"|"desc")` native.
5. **File IO (optional, gated)**: `fs.read(path)`, `fs.write(path,text)`.

This keeps the ecosystem minimal, fast to build, and entirely independent of big-vendor stacks.
