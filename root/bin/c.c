// c -- c compiler
//
// Usage:  c [-v] [-s] [-Ipath] [-o exefile] file ...
//
// Description:
//   c is the c compiler.  It takes a single source file and creates an executable
//   file or else executes the compiled code immediately.  The compiler does not
//   reach full standards compliance, so some programs need minor adjustment.
//   There is no preprocessor, although the #include keyword is allowed
//   supporting a single level of file inclusion.
//
//   The following options are supported:
//
//   -v  Verbose output.  Useful for finding undeclared function calls.
//   -s  Print source and generated code.
//   -I  Path to include files (otherwise source directory or /lib/.)
//   -o  Create executable file and terminate normally.  If -o and -s are omitted,
//       the compiled code is executed immediately (if there were no compile
//       errors) with the command line arguments passed after the source file
//       parameter.  Thus,
//           c -o echo echo.c
//           ./echo hello world
//       is equivalent to,
//           c echo.c hello world
//       is also equivalent to,
//           c c.c c.c c.c echo.c hello world
//
// Written by Robert Swierczek

#include <u.h>
#include <libc.h>

enum {
  SEG_SZ    = 8*1024*1024, // max size of text+data+bss seg
  EXPR_SZ   =      4*1024, // size of expression stack
  VAR_SZ    =     64*1024, // size of symbol table
  PSTACK_SZ =     64*1024, // size of patch stacks
  LSTACK_SZ =      4*1024, // size of locals stack
  HASH_SZ   =      8*1024, // number of hash table entries
  BSS_TAG   =  0x10000000, // tag for patching global offsets
};

typedef struct ident_s {
  uint class;
  uint type;
  int val;
  int local;
  uint tk;
  char *name;
  int hash;
  struct ident_s *next;
} ident_t;

typedef struct {
  uint class; // layout must be same as ident_t
  uint type;
  int val;
  ident_t *id;
} loc_t;

typedef struct struct_s {
  ident_t *id;
  int size;
  int align;
  struct member_s *member;
  struct struct_s *next;
  int pad;
} struct_t;

typedef struct member_s {
  int offset;
  uint type;
  ident_t *id;
  struct member_s *next;
} member_t;

typedef struct {
  uint type;
  int size;
} array_t;

int tk,       // current token
    ts, ip,   // text segment, current pointer
    gs, data, // data segment, current offset
    bss,      // bss offset
    loc,      // locals frame offset
    line,     // line number
    ival,     // current token integer value
    errs,     // number of errors
    verbose,  // print additional verbiage
    debug,    // print source and object code
    ffun,     // unresolved forward function counter
    va, vp,   // variable pool, current pointer
    *e,       // expression tree pointer
    *pdata,   // data segment patchup pointer
    *pbss;    // bss segment patchup pointer

ident_t *id;  // current parsed identifier
double fval;  // current token double value
uint ty,      // current parsed subexpression type
     rt,      // current parsed function return type
     bigend;  // big-endian machine

char *file,   // input file name
     *cmd,    // command name
     *incl,   // include path
     *pos;    // input file position

loc_t *ploc;  // local variable stack pointer

char ops[] =
  "HALT,ENT ,LEV ,JMP ,JMPI,JSR ,JSRA,LEA ,LEAG,CYC ,MCPY,MCMP,MCHR,MSET," // system
  "LL  ,LLS ,LLH ,LLC ,LLB ,LLD ,LLF ,LG  ,LGS ,LGH ,LGC ,LGB ,LGD ,LGF ," // load a
  "LX  ,LXS ,LXH ,LXC ,LXB ,LXD ,LXF ,LI  ,LHI ,LIF ,"
  "LBL ,LBLS,LBLH,LBLC,LBLB,LBLD,LBLF,LBG ,LBGS,LBGH,LBGC,LBGB,LBGD,LBGF," // load b
  "LBX ,LBXS,LBXH,LBXC,LBXB,LBXD,LBXF,LBI ,LBHI,LBIF,LBA ,LBAD,"
  "SL  ,SLH ,SLB ,SLD ,SLF ,SG  ,SGH ,SGB ,SGD ,SGF ,"                     // store
  "SX  ,SXH ,SXB ,SXD ,SXF ,"
  "ADDF,SUBF,MULF,DIVF,"                                                   // arithmetic
  "ADD ,ADDI,ADDL,SUB ,SUBI,SUBL,MUL ,MULI,MULL,DIV ,DIVI,DIVL,"
  "DVU ,DVUI,DVUL,MOD ,MODI,MODL,MDU ,MDUI,MDUL,AND ,ANDI,ANDL,"
  "OR  ,ORI ,ORL ,XOR ,XORI,XORL,SHL ,SHLI,SHLL,SHR ,SHRI,SHRL,"
  "SRU ,SRUI,SRUL,EQ  ,EQF ,NE  ,NEF ,LT  ,LTU ,LTF ,GE  ,GEU ,GEF ,"      // logical
  "BZ  ,BZF ,BNZ ,BNZF,BE  ,BEF ,BNE ,BNEF,BLT ,BLTU,BLTF,BGE ,BGEU,BGEF," // conditional
  "CID ,CUD ,CDI ,CDU ,"                                                   // conversion
  "CLI ,STI ,RTI ,BIN ,BOUT,NOP ,SSP ,PSHA,PSHI,PSHF,PSHB,POPB,POPF,POPA," // misc
  "IVEC,PDIR,SPAG,TIME,LVAD,TRAP,LUSP,SUSP,LCL ,LCA ,PSHC,POPC,MSIZ,"
  "PSHG,POPG,NET1,NET2,NET3,NET4,NET5,NET6,NET7,NET8,NET9,"
  "POW ,ATN2,FABS,ATAN,LOG ,LOGT,EXP ,FLOR,CEIL,HYPO,SIN ,COS ,TAN ,ASIN," // math
  "ACOS,SINH,COSH,TANH,SQRT,FMOD,"
  "IDLE,";

// types and type masks. specific bit patterns and orderings needed by expr()
enum {
  CHAR   = 1, SHORT, INT, UCHAR, USHORT,
  UINT   = 8,
  FLOAT  = 16, DOUBLE,
  STRUCT = 26, VOID, FUN,
  ARRAY  = 32,
  PTR    = 64,    // pointer increment
  PMASK  = 0x3C0, // pointer mask
  TMASK  = 0x3FF, // base type mask
  PAMASK = (PMASK | ARRAY), // pointer or array mask
  TSHIFT = 10,
};

// tokens and node types ( >= 128 so not to collide with ascii-valued tokens)
typedef enum {
  Num = 128, // low ordering of Num and Auto needed by nodc()

  // keyword grouping needed by main()  XXX missing extern and register
  Asm, Auto, Break, Case, Char, Continue, Default, Do, Double, Else, Enum, Float, For, Goto, If, Int, Long, Return, Short,
  Sizeof, Static, Struct, Switch, Typedef, Union, Unsigned, Void, While, Va_list, Va_start, Va_arg,

  Id, Numf, Ptr, Not, Notf, Nzf, Lea, Leag, Fun, FFun, Fcall, Label, FLabel,
  Cid ,Cud ,Cdi ,Cdu ,Cic ,Cuc ,Cis ,Cus,
  Dots,
  Addaf, Subaf, Mulaf, Dvua, Divaf, Mdua, Srua,
  Eqf, Nef, Ltu, Ltf, Geu, Gef, Sru,
  Addf, Subf, Mulf, Dvu, Divf, Mdu,

  // operator precedence order needed by expr()
  Comma,
  Assign,
  Adda, Suba, Mula, Diva, Moda, Anda, Ora, Xora, Shla, Shra,
  Cond,
  Lor, Lan,
  Or,  Xor, And,
  Eq,  Ne,
  Lt,  Gt,  Le,  Ge,
  Shl, Shr,
  Add, Sub,
  Mul, Div, Mod,
  Inc, Dec, Dot, Arrow, Brak, Paren
}TOKEN;

void *new(int size)
{
  void *p;
  if ((p = sbrk((size + 7) & -8)) == (void *)-1) { dprintf(2,"%s : fatal: unable to sbrk(%d)\n", cmd, size); exit(-1); }
  return (void *)(((int)p + 7) & -8);
}

void err(char *msg)
{
  dprintf(2,"%s : [%s:%d] error: %s\n", cmd, file, line, msg); // XXX need errs to power past tokens (validate for each err case.)
  if (++errs > 10) { dprintf(2,"%s : fatal: maximum errors exceeded\n", cmd); exit(-1); }
}

char *mapfile(char *name, int size) // XXX replace with mmap
{
  int f; char *p;
  if ((f = open(name, O_RDONLY)) < 0) { dprintf(2,"%s : [%s:%d] error: can't open file %s\n", cmd, file, line, name); exit(-1); }
  p = new(size+1);
  if (read(f, p, size) != size) { dprintf(2,"%s : [%s:%d] error: can't read file %s\n", cmd, file, line, name); exit(-1); }
  close(f);
  p[size] = 0; // XXX redundant but need to map file!
  return p;
}

// instruction emitter
void em(int i)
{
  if (debug) printf("%08x  %08x%6.4s\n", ip-ts, i, &ops[i*5]);
  *(int *)ip = i;
  ip += 4;
}
void emi(int i, int c)
{
  if (debug) {
    if (i == 3 || i == 5) //show JMP, JSR
      printf("%08x  %08x%6.4s  0x%x (TO 0x%x)\n", ip - ts, i | (c << 8), &ops[i * 5], c, ip-ts+c+4);
    else
      printf("%08x  %08x%6.4s  0x%x (D %d)\n", ip - ts, i | (c << 8), &ops[i * 5], c, c);
  }
  if (c<<8>>8 != c) err("emi() constant out of bounds");
  *(int *)ip = i | (c << 8);
  ip += 4;
}
void emj(int i, int c) { emi(i, c - ip - 4); } // jump
void eml(int i, int c) { emi(i, c - loc); } // local
void emg(int i, int c) { if (c < BSS_TAG) *pdata++ = ip; else { *pbss++ = ip; c -= BSS_TAG; } emi(i, c); } // global
int emf(int i, int c) // forward
{
  if (debug) printf("%08x  %08x%6.4s  <fwd>\n", ip-ts, i | (c << 8), &ops[i*5]);
  if (c<<8>>8 != c) err("emf() offset out of bounds");
  *(int *)ip = i | (c << 8);
  ip += 4;
  return ip - 4 - ts;
}
void patch(int t, int a)
{
  int n;
  while (t) {
    n = *(int *)(t += ts);
    *(int *)t = (n & 0xff) | ((a - t - 4) << 8);
    t = n >> 8;
  }
}

// parser
void dline()
{
  char *p;
  for (p = pos; *p && *p != '\n' && *p != '\r'; p++);
  printf("%s  %d: %.*s\n", file, line, p - pos, pos);
}

void next()
{
  char *p; int b; ident_t **hm;
  struct stat st;
  static char iname[512], *ifile, *ipos; // XXX 512
  static int iline;
  static ident_t *ht[HASH_SZ];

  for (;;) {
    switch (tk = *pos++) {
    case ' ': case '\t': case '\v': case '\r': case '\f':
      continue;

    case '\n':
      line++; if (debug) dline();
      continue;

    case '#':
      if (!memcmp(pos,"include",7)) {
        if (ifile) { err("can't nest include files"); exit(-1); } // include errors bail out otherwise it gets messy
        pos += 7;
        while (*pos == ' ' || *pos == '\t') pos++;
        if (*pos != '"' && *pos != '<') { err("bad include file name"); exit(-1); }
        ipos = pos++;
        if (*pos == '/')
          b = 0;
        else if (incl) {
          memcpy(iname, incl, b = strlen(incl)); iname[b++] = '/';
        } else {
          for (b = strlen(file); b; b--) if (file[b-1] == '/') { memcpy(iname, file, b); break; }
        }
        while (*pos && *pos != '>' && *pos != '"' && b < sizeof(iname)-1) iname[b++] = *pos++;
        iname[b] = 0;
        if (stat(iname, &st)) {
          if (*ipos == '"' || ipos[1] == '/')
            { dprintf(2,"%s : [%s:%d] error: can't stat file %s\n", cmd, file, line, iname); exit(-1); }
          memcpy(iname, "/lib/", b = 5);
          pos = ipos + 1;
          while (*pos && *pos != '>' && *pos != '"' && b < sizeof(iname)-1) iname[b++] = *pos++;
          iname[b] = 0;
          if (stat(iname, &st)) { dprintf(2,"%s : [%s:%d] error: can't stat file %s\n", cmd, file, line, iname); exit(-1); }
        }
        while (*pos && *pos != '\n') pos++;
        ipos = pos; pos = mapfile(iname, st.st_size);
        ifile = file; file = iname;
        iline = line; line = 1;
        if (debug) dline();
        continue;
      }
      while (*pos && *pos != '\n') pos++;
      continue;

    case 'a' ... 'z': case 'A' ... 'Z': case '_': case '$':
      p = pos - 1;
      for (;;) {
        switch (*pos) {
        case 'a' ... 'z': case 'A' ... 'Z': case '0' ... '9': case '_': case '$':
          tk = tk * 147 + *pos++;
          continue;
        }
        break;
      }
      id = *(hm = &ht[tk & (HASH_SZ - 1)]);
      tk ^= (b = pos - p);
      while (id) {
        if (tk == id->hash && (b < 5 || !memcmp(id->name, p, b))) { tk = id->tk; return; } // b < 5 dependant on hash func and size
        id = id->next;
      }
      id = (ident_t *) vp; vp += sizeof(ident_t);
      id->name = p;
      id->hash = tk;
      id->next = *hm;
      tk = id->tk = Id;
      *hm = id;
      return;

    case '0' ... '9':
      if (*pos == '.') { pos++; fval = tk - '0'; goto frac; }
      else if ((ival = tk - '0')) {
        p = pos;
        while (*pos >= '0' && *pos <= '9') ival = ival * 10 + *pos++ - '0';
        if (*pos == '.') {
          pos = p;
          fval = tk - '0';
          while (*pos >= '0' && *pos <= '9') fval = fval * 10.0 + (*pos++ - '0');
          pos++;
frac:     b = 10;
          while (*pos >= '0' && *pos <= '9') { fval += (double)(*pos++ - '0')/b; b *= 10; }
          if (*pos == 'f') pos++; // XXX should floats be treated different?
          ty = DOUBLE;
          tk = Numf; // XXX look into inferno/1e1src.tgz libk/charstod.c
          return;
        }
      } else if (*pos == 'x' || *pos == 'X') {
        pos++;
        for (;;) {
          switch (*pos) {
          case '0' ... '9': ival = ival * 16 + *pos++ - '0'; continue;
          case 'a' ... 'f': ival = ival * 16 + *pos++ - 'a' + 10; continue;
          case 'A' ... 'F': ival = ival * 16 + *pos++ - 'A' + 10; continue;
          }
          break;
        }
      }
      else if (*pos == 'b' || *pos == 'B') { pos++; while (*pos == '0' || *pos == '1') ival = ival * 2 + *pos++ - '0'; }
      else { while (*pos >= '0' && *pos <= '7') ival = ival * 8 + *pos++ - '0'; }
      if (*pos == 'u' || *pos == 'U') { pos++; ty = UINT; } else ty = INT;
      if (*pos == 'l' || *pos == 'L') pos++;
      tk = Num;
      return;

    case '/':
      if (*pos == '/') { // single line comment
        while (*++pos != '\n' && *pos);
        continue;
      } else if (*pos == '*') { // comment
        while (*++pos) {
          if (*pos == '*' && pos[1] == '/') { pos += 2; break; }
          else if (*pos == '\n') { line++; if (debug) { pos++; dline(); pos--; } }
        }
        continue;
      }
      else if (*pos == '=') { pos++; tk = Diva; }
      else tk = Div;
      return;

    case '\'': case '"':
      ival = data;
      while ((b = *pos++) != tk) {
        if (b == '\\') {
          switch (b = *pos++) {
          case '\'': case '"': case '?': case '\\': break;
          case 'a': b = '\a'; break; // alert
          case 'b': b = '\b'; break; // backspace
          case 'f': b = '\f'; break; // form feed
          case 'n': b = '\n'; break; // new line
          case 'r': b = '\r'; break; // carriage return
          case 't': b = '\t'; break; // horizontal tab
          case 'v': b = '\v'; break; // vertical tab
          case 'e': b = '\e'; break; // escape
          case '\r': while (*pos == '\r' || *pos == '\n') pos++; // XXX not sure if this is right
          case '\n': line++; if (debug) dline(); continue;
          case 'x':
//            b = (*pos - '0') * 16 + pos[1] - '0'; pos += 2; // XXX this is broke!!! 0xFF needs to become -1 also
            switch (*pos) {
            case '0' ... '9': b = *pos++ - '0'; break;
            case 'a' ... 'f': b = *pos++ - 'a' + 10; break;
            case 'A' ... 'F': b = *pos++ - 'A' + 10; break;
            default: b = 0; pos++; break;                     // XXX you can try a few in a reg c compiler?!
            }
            switch (*pos) {
            case '0' ... '9': b = b*16 + *pos++ - '0'; break;
            case 'a' ... 'f': b = b*16 + *pos++ - 'a' + 10; break;
            case 'A' ... 'F': b = b*16 + *pos++ - 'A' + 10; break;
            default: break;
            }
// XXX			b = (char) b; // make sure 0xFF becomes -1 XXX do some other way!
            break;
          case '0' ... '7':
            b -= '0';
            if (*pos >= '0' && *pos <= '7') {
              b = b*8 + *pos++ - '0';
              if (*pos >= '0' && *pos <= '7') b = b*8 + *pos++ - '0';
            }
            break;
          default: err("bad escape sequence");
          }
        }
        else if (!b) { tk = 0; err("unexpected eof"); return; }
        *(char *)(gs + data++) = b;
      }
      if (tk == '\'') {
        b = data - ival;
        memcpy(&ival, (char *)(gs + (data = ival)), 4);
        memset((char *)(gs + data), 0, b);
        ty = INT; tk = Num;
      }
      return;

    case '=': if (*pos == '=') { pos++; tk = Eq;   } else tk = Assign; return;
    case '+': if (*pos == '+') { pos++; tk = Inc;  } else if (*pos == '=') { pos++; tk = Adda; } else tk = Add; return;
    case '-': if (*pos == '-') { pos++; tk = Dec;  }
         else if (*pos == '>') { pos++; tk = Arrow; } else if (*pos == '=') { pos++; tk = Suba; } else tk = Sub; return;
    case '*': if (*pos == '=') { pos++; tk = Mula; } else tk = Mul; return;
    case '<': if (*pos == '=') { pos++; tk = Le;   }
         else if (*pos == '<') { if (*++pos == '=') { pos++; tk = Shla; } else tk = Shl; } else tk = Lt; return;
    case '>': if (*pos == '=') { pos++; tk = Ge;   }
         else if (*pos == '>') { if (*++pos == '=') { pos++; tk = Shra; } else tk = Shr; } else tk = Gt; return;
    case '|': if (*pos == '|') { pos++; tk = Lor;  } else if (*pos == '=') { pos++; tk = Ora;  } else tk = Or;  return;
    case '&': if (*pos == '&') { pos++; tk = Lan;  } else if (*pos == '=') { pos++; tk = Anda; } else tk = And; return;
    case '!': if (*pos == '=') { pos++; tk = Ne;   } return;
    case '%': if (*pos == '=') { pos++; tk = Moda; } else tk = Mod; return;
    case '^': if (*pos == '=') { pos++; tk = Xora; } else tk = Xor; return;
    case ',': tk = Comma; return;
    case '?': tk = Cond; return;
    case '.':
      if (*pos == '.' && pos[1] == '.') { pos += 2; tk = Dots; }
      else if (*pos >= '0' && *pos <= '9') { fval = 0.0; goto frac; }
      else tk = Dot; return;
      // XXX eventually test for float? is this structure access x.y or floating point .5?
      // XXX lookup strtod() for guidance/implementation

    case '(': tk = Paren; return;
    case '[': tk = Brak; return;
    case '~':
    case ';':
    case ':':
    case '{':
    case '}':
    case ')':
    case ']': return;
    case 0:
      if (!ifile) { pos--; return; }
      file = ifile; ifile = 0;
      pos = ipos;
      line = iline;
      continue;

    default: err("bad token"); continue;
    }
  }
}

void skip(int c)
{
  if (tk != c) { dprintf(2,"%s : [%s:%d] error: '%c' expected\n", cmd, file, line, c); errs++; }
  next();
}

void expr(int lev);

int imm() /// XXX move these back down once I validate prototypes working for double immf()
{
  int *b = e, c;
  expr(Cond);
  if (*e == Num) c = e[2];
  else if (*e == Numf) c = (int) *(double *)(e+2);
  else { err("bad constant expression"); c = 0; }
  e = b;
  return c;
}

double immf()
{
  int *b = e; double c;
  expr(Cond);
  if (*e == Num) c = e[2];
  else if (*e == Numf) c = *(double *)(e+2);
  else { err("bad float constant expression"); c = 0.0; }
  e = b;
  return c;
}

int tsize(uint t) // XXX return unsigned? or error checking on size
{
  array_t *a; struct_t *s;
  switch (t & TMASK) {
  case ARRAY:  a = (array_t *)(va+(t>>TSHIFT)); return a->size * tsize(a->type);
  case STRUCT:
    if ((s = (struct_t *)(va+(t>>TSHIFT)))->align) return s->size;
    err("can't compute size of incomplete struct");
  case CHAR:
  case UCHAR:
  case VOID:
  case FUN: return 1;
  case SHORT:
  case USHORT: return 2;
  case DOUBLE: return 8;
  default: return 4;
  }
}

int tinc(uint t) // XXX return unsigned?
{
  if (t & PMASK) return tsize(t - PTR);
  else if (t & ARRAY) return tsize(((array_t *)(va+(t>>TSHIFT)))->type);  // XXX need to test this!
  else return 1;
}

int talign(uint t)
{
  int a;
  switch (t & TMASK) {
  case ARRAY:  return talign(((array_t *)(va+(t>>TSHIFT)))->type);
  case STRUCT:
    if ((a = ((struct_t *)(va+(t>>TSHIFT)))->align)) return a;
    err("can't compute alignment of incomplete struct");
  case CHAR:
  case UCHAR: return 1;
  case SHORT:
  case USHORT: return 2;
  case DOUBLE: return 8;
  default: return 4;
  }
}

void member(int stype, struct_t *s);

uint basetype()
{
  int m; ident_t *n; struct_t *s;
  static struct_t *structs;

  switch (tk) {
  case Void:    next(); return VOID; // XXX
  case Va_list: next(); return CHAR + PTR;

  case Unsigned: // not standard, but reasonable
    next();
    if (tk == Char) { next(); return UCHAR; }
    if (tk == Short) { next(); if (tk == Int) next(); return USHORT; }
    if (tk == Long) next();
    if (tk == Int) next();
    return UINT;

  case Char:   next(); return CHAR;
  case Short:  next(); if (tk == Int) next(); return SHORT;
  case Long:   next(); if (tk == Int) next(); return INT;
  case Int:    next(); return INT;
  case Float:  next(); return FLOAT;
  case Double: next(); return DOUBLE;

  case Union:
  case Struct:
    m = tk;
    next();
    if (tk == Id) {
      for (s = structs; s; s = s->next) if (s->id == id) goto found;
      s = (struct_t *) vp; vp += sizeof(struct_t);
      s->id = id; // XXX redefinitions
      s->next = structs; structs = s;
found:
      next();
      if (tk != '{') return STRUCT | (((int)s-va)<<TSHIFT);
      if (s->align) err("struct or union redefinition");
      next();
    } else {
      skip('{');
      s = (struct_t *) vp; vp += sizeof(struct_t);
      s->next = structs; structs = s;
    }
    member(m,s);
    skip('}');
    return STRUCT | (((int)s-va)<<TSHIFT);

  case Enum:
    next();
    if (tk != '{') next();
    if (tk == '{') {
      next();
      m = 0;
      while (tk && tk != '}') {
        if (tk != Id) { err("bad enum identifier"); break; }
        n = id; // XXX redefinitions
        next();
        if (tk == Assign) { next(); m = imm(); }
        n->class = Num;
        n->type = INT;
        n->val = m++;
        if (tk != Comma) break;
        next();
      }
      skip('}');
    }
    return INT;

  case Id:
    if (id->class == Typedef) {
      m = id->type; next();
      return m;
    }
  default:
    return 0;
  }
}

uint *type(uint *t, ident_t **v, uint bt)
{
  uint p, a, pt, d; ident_t *n; array_t *ap;

  while (tk == Mul) { next(); bt += PTR; }
  if (tk == Paren) {
    next();
    if (tk == ')') {
      if (v) err("bad abstract function type");
      next();
      *t |= FUN | ((vp-va)<<TSHIFT);
      t = (uint *) vp;
      vp += 8; // XXX sizeof(uint);
      *t = bt;
      return t;
    }
    t = type(t, v, 0);
    skip(')');
  } else if (tk == Id) { // type identifier
    if (v) *v = id; else err("bad abstract type");
    next();
  }

  if (tk == Paren) { // function
    next();
    for (pt=p=0; tk != ')'; p++) {
      n = 0;
      if ((a = basetype())) {
        if (tk == ')' && a == VOID) break;
        d = 0;
        type(&d, &n, a); // XXX should accept both arg/non arg if v == 0  XXX i.e. function declaration!
        if (d == FLOAT) d = DOUBLE; // XXX not ANSI
      } else if (tk == Id) { //ASSERT
        d = INT;
        n = id;
        next();
      } else {
        err("bad function parameter");
        next();
        continue;
      }
      if (n) {
        if (n->class && n->local) err("duplicate definition");
        if (*v == n) *v = (ident_t *) ploc; // hack if function name same as parameter
        ploc->class = n->class; // XXX make sure these are unwound for abstract types and forward decls
        ploc->type = n->type;
        ploc->val = n->val;
        ploc->id = n;
        ploc++;
        if ((d & TMASK) == ARRAY) d = ((array_t *)(va+(d>>TSHIFT)))->type + PTR; // convert array to pointer
        n->local = 1;
        n->class = Auto;
        n->type = d;
        n->val = p*8 + 8;
        if (bigend) {
          switch (d) {
          case CHAR: case UCHAR: n->val += 3; break;
          case SHORT: case USHORT: n->val += 2;
          }
        }
      }
      pt |= (d == DOUBLE ? 1 : (d < UINT ? 2 : 3)) << p*2;
      if (tk == Comma) next(); // XXX desparately need to flag an error if not a comma or close paren!
      if (tk == Dots) { next(); if (tk != ')') err("expecting close parens after dots"); break; }
    }
    next(); // skip ')'
    *t |= FUN | ((vp-va)<<TSHIFT);
    t = (uint *) vp;
    *(uint *)(vp + 4) = pt;
    vp += 8;
  } else while (tk == Brak) { // array
    next();
    a = 0;
    if (tk != ']') {
      // XXX need constant in vc...not tree!  I think this is going go be a bug unless you push vs,ty
      if ((int)(a = imm()) < 0) err("bad array size");
    }
    skip(']');
    *t |= ARRAY | ((vp-va)<<TSHIFT);
    ap = (array_t *) vp; vp += sizeof(array_t);
    ap->size = a;
    t = &ap->type;
  }
  *t += bt;
  return t;
}

void rv(int *a);
void stmt();
void node(int n, int *a, int *b);
void cast(uint t);

void decl(TOKEN bc)
{
  int sc, size, align, hglo, *b, *c = 0; uint bt, t; ident_t *v; loc_t *sp;

  for (;;) {
    if (tk == Static || tk == Typedef || (tk == Auto && bc == Auto))
      { sc = tk; next(); if (!(bt = basetype())) bt = INT; } // XXX typedef inside function?  probably bad!
    else { if (!(bt = basetype())) { if (bc == Auto) break; bt = INT; } sc = bc; }
    if (!tk) break;
    if (tk == ';') { next(); continue; } // XXX is this valid?
    for (;;) {
      v = 0; t = 0;
      sp = ploc;
      type(&t, &v, bt);
      if (!v) err("bad declaration");
      else if (tk == '{') {
        if (bc != Static || sc != Static) err("bad nested function");
        if ((t & TMASK) != FUN) err("bad function definition");
        rt = *(uint *)(va+(t>>TSHIFT));
        if (v->class == FFun) {
          patch(v->val,ip); ffun--;
          if (rt != *(uint *)(va+(v->type>>TSHIFT)) || ((bt  = *(uint *)(va+(v->type>>TSHIFT)+4)) &&
            bt != *(uint *)(va+(t>>TSHIFT)+4))) err("conflicting forward function declaration");
        }
        else if (v->class) err("duplicate function definition");
        v->class = Fun;
        v->type = t;
        v->val = ip;
        loc = 0;
        next();
        b = e;
        decl(Auto);
        loc &= -8;
        if (loc) emi(ENT,loc);
        if (e != b) { rv(e); e = b; }
        while (tk != '}') stmt(); // XXX null check
        next();
        emi(LEV,-loc);
        while (ploc != sp) {
          ploc--;
          v = ploc->id;
          v->val = ploc->val;
          v->type = ploc->type;
          if (v->class == FLabel) err("unresoved label");
          v->class = ploc->class;
          v->local = 0;
        }
        break;
      } else if ((t & TMASK) == FUN) {
//        if (bc != Static || sc != Static) err("bad nested function declaration");
        if (v->class) err("duplicate function declaration");
        v->class = FFun;
        v->type = t;
        ffun++;
        while (ploc != sp) {
          ploc--;
          v = ploc->id;
          v->val = ploc->val;
          v->type = ploc->type;
          v->class = ploc->class;
          v->local = 0;
        }
      } else {
        if (bc == Auto) {
          if (v->class && v->local) err("duplicate definition");
          ploc->class = v->class;
          ploc->type = v->type;
          ploc->val = v->val;
          ploc->id = v;
          ploc++;
          v->local = 1;
        }
        else if (v->class) err("duplicate definition");

        v->class = sc;
        v->type = t;
        if (sc != Typedef) { // XXX typedefs local to functions?
          if ((t & TMASK) == ARRAY) v->class = (sc == Auto) ? Lea : Leag; // not lvalue if array
          size = tsize(t);
          align = talign(t);
          if (sc == Auto) {
            v->val = loc = (loc - size) & -align; // allocate stack space
            if (tk == Assign) {
              node(Auto,(int *)t,(int *)v->val); b = e;
              next();
              expr(Cond);
              cast(t < UINT ? INT : t);
              { *(e-=2) = Assign; e[1] = (int)b; }
              if (c) { *(e-=2) = Comma; e[1] = (int)c; }
              c = e;
            }
          } else {
            if (tk == Assign) {
              v->val = data = (data + align - 1) & -align; // allocate data space
              hglo = data;
              next();
              if (tk == '"') {
                if ((t & TMASK) != ARRAY) err("bad string initializer");
                next(); while (tk == '"') next();
                data = size ? hglo + size : data + 1;
              } else if (tk == '{') { // XXX finish this mess!
                if ((t & TMASK) != ARRAY) err("bad array initializer");
                next();
                while (tk != '}') {
                  switch (((array_t *)(va+(t>>TSHIFT)))->type) {
                  case UCHAR:
                  case CHAR:   *(char *)  (gs + data) = imm(); data++; break;
                  case USHORT:
                  case SHORT:  *(short *) (gs + data) = imm();  data += 2; break;
                  case FLOAT:  *(float *) (gs + data) = immf(); data += 4; break;
                  case DOUBLE: *(double *)(gs + data) = immf(); data += 8; break;
                  default:     *(int *)   (gs + data) = imm();  data += 4; break;
                  }
                  if (tk == Comma) next();
                }
                next();
                if (size) data = hglo + size * align; // XXX need to zero fill if size > initialized part  XXX but may default since using sbrk vs malloc?
                // XXX else set array size if []
              } else {
                switch (t) { // XXX redundant code
                case UCHAR:
                case CHAR:   *(char *)  (gs + data) = imm(); data++; break;
                case USHORT:
                case SHORT:  *(short *) (gs + data) = imm();  data += 2; break;
                case FLOAT:  *(float *) (gs + data) = immf(); data += 4; break;
                case DOUBLE: *(double *)(gs + data) = immf(); data += 8; break;
                default:     *(int *)   (gs + data) = imm();  data += 4; break;
                }
              }
            } else {
              bss = (bss + align - 1) & -align; // allocate bss space
              v->val = bss + BSS_TAG;
              bss += size; // XXX check for zero size
            }
          }
        }
      }
      if (tk != Comma) {
        skip(';');
        break;
      }
      next();
    }
  }
}

void member(int stype, struct_t *s)
{
  int size, align, ssize = 0, salign = 1; uint bt, t; ident_t *v; member_t *m, **mp;

  while (tk && tk != '}') {
    if (!(bt = basetype())) bt = INT;
    if (!tk) break;
    if (tk == ';') { next(); continue; } // XXX
    for (;;) {
      v = 0; t = 0;
      type(&t, &v, bt);
      if (!v) err("bad member declaration");
      else {
        for (mp = &s->member; (m = *mp); mp = &m->next) if (m->id == v) err("duplicate member declaration");
        *mp = m = (member_t *) vp; vp += sizeof(member_t);
        m->id = v;
        m->type = t;
        size = tsize(t);
        align = talign(t);
        if (stype == Struct)
          ssize = (m->offset = (ssize + align - 1) & -align) + size;
        else if (size > ssize)
          ssize = size;
        if (align > salign) salign = align;
      }
      if (tk != Comma) {
        skip(';');
        break;
      }
      next();
    }
  }
  s->align = salign;
  s->size = (ssize + salign - 1) & -salign;
}

// expression parsing
void node(int n, int *a, int *b) { *(e-=4) = n; e[1] = (int)a; e[2] = (int)b; }
void nodc(int n, int *a, int *b) // commutative
{
  *(e-=4) = n;
  if (*a < *b) { e[1] = (int)b; e[2] = (int)a; } else { e[1] = (int)a; e[2] = (int)b; } // put simpler expression in rhs
}

void mul(int *b) // XXX does this handle unsigned correctly?
{
  if (*b == Num) {
    if (*e == Num) { e[2] *= b[2]; return; }
    if (b[2] == 1) return;
  }
  if (*e == Num && e[2] == 1) { e = b; return; } // XXX reliable???
  nodc(Mul,e,b);
}

void add(uint *b)  // XXX make sure to optimize (a + 9 + 2) -> (a + 11)    and    (a + 9 - 2) -> (a + 7)
{
  if (*b == Num) {
    if (*e == Num || *e == Lea || *e == Leag) { e[2] += b[2]; return; } // XXX  <<>> check
    if (!b[2]) return;
  }
  if (*e == Num) {
    if (*b == Lea) { e[2] += b[2]; *e = Lea; return; } // XXX structure offset optimizations
    if (*b == Leag) { e[2] += b[2]; *e = Leag; return; }
    if (!e[2]) { e = (int *)b; return; } // XXX reliable???
  }
  nodc(Add,(int *)b,e);
}

int *flot(int *b, uint t)
{
  if (t == DOUBLE || t == FLOAT) return b;
  if (*b == Num) {
    *b = Numf;
    *(double *)(b + 2) = t < UINT ? (double)b[2] : (double)(uint)b[2];
    return b;
  }
  *(e-=2) = t < UINT ? Cid : Cud;
  e[1] = (int)b;
  return e;
}

void ind()
{
  if (ty & PMASK)
    ty -= PTR;
  else if (ty & ARRAY) {
    ty = ((array_t *)(va+(ty>>TSHIFT)))->type;
    if ((ty & TMASK) == ARRAY) return; // was a ref, still a ref
  } else
    err("dereferencing a non-pointer");

  if ((ty & TMASK) == FUN) return; // XXX
  switch (*e) {
  case Leag: *e = Static; e[1] = ty; return;
  case Lea:  *e = Auto; e[1] = ty; return;
  default: *(e-=2) = Ptr; e[1] = ty; return;
  }
}

void addr()
{
  ty += PTR;
  switch (*e) {
  case Fun: return; // XXX dont think ty is going to be right?
  case Leag: case Lea: return; // XXX
  case Static: *e = Leag; return;
  case Auto: *e = Lea; return;
  case Ptr: e += 2; return;
  default: err("lvalue expected");
  }
}

void assign(int n, int *b)
{
  *(e-=2) = n; e[1] = (int)b;
  switch (ty) { // post-cast usually removed by trim()
  case CHAR:   *(e-=2) = Cic; break;
  case UCHAR:  *(e-=2) = Cuc; break;
  case SHORT:  *(e-=2) = Cis; break;
  case USHORT: *(e-=2) = Cus; break;
  }
}

void trim() // trim dead code from expression statements (just the common cases)
{
  if (*e >= Cic && *e <= Cus) e += 2; // remove conversion after assignment
  if (*e == Add && *(int *)e[2] == Num) e = (int *)e[1]; // convert x++ into ++x
}

void cast(uint t)
{
  if (t == DOUBLE || t == FLOAT) {
    if (ty < UINT)
      { if (*e == Num) { *e = Numf; *(double *)(e+2) = e[2]; } else { *(e-=2) = Cid; e[1] = (int)(e+2); } }
    else if (ty != DOUBLE && ty != FLOAT)
      { if (*e == Num) { *e = Numf; *(double *)(e+2) = (uint)e[2]; } else { *(e-=2) = Cud; e[1] = (int)(e+2); } }
  } else if (t < UINT) {
    if (ty == DOUBLE || ty == FLOAT) {
      if (*e == Numf) {
        *e = Num; e[2] = (int)*(double *)(e+2);
      } else {
        *(e-=2) = Cdi;
      }
    }
    switch (t) {
    case CHAR:   if (*e == Num) e[2] = (char)   e[2]; else *(e-=2) = Cic; break;
    case UCHAR:  if (*e == Num) e[2] = (uchar)  e[2]; else *(e-=2) = Cuc; break;
    case SHORT:  if (*e == Num) e[2] = (short)  e[2]; else *(e-=2) = Cis; break;
    case USHORT: if (*e == Num) e[2] = (ushort) e[2]; else *(e-=2) = Cus; break;
    }
  } else if (ty == DOUBLE || ty == FLOAT) {
    if (*e == Numf) { *e = Num; e[2] = (uint)*(double *)(e+2); } else *(e-=2) = Cdu;
  }
}

void expr(int lev)
{
  int *b, *d, *dd; uint t, tt; member_t *m;

  switch (tk) {
  case Num:
    *(e-=4) = Num; e[2] = ival;
    next();
    break;

  case Numf:
    *(e-=4) = Numf; *(double*)(e+2) = fval;
    next();
    break;

  case '"':
    ty = PTR | CHAR;
    *(e-=4) = Leag; e[2] = ival;
    next();
    while (tk == '"') next();
    data++;
    break;

  case Id:
    if (id->class) {
      node(id->class, (int *)(ty = id->type), (id->class == FFun) ? (int *)id : (int *)id->val);
      next();
      break;
    }
    id->class = FFun;
    ffun++;
    ty = id->type = FUN | ((vp-va)<<TSHIFT);
    *(uint *)vp = INT; vp += 8;
    node(FFun,0,(int *)id);
    next();
    if (tk != Paren) err("undefined symbol");
    else if (verbose) dprintf(2,"%s : [%s:%d] warning: undeclared function called here\n", cmd, file, line);
    break;

  case Va_arg: // va_arg(list,mode) *(mode *)(list += 8)
    next();
    skip(Paren);
    expr(Assign);
    skip(Comma);
    b = e; *(e-=4) = Num; e[2] = 8; *(e-=2) = Adda; e[1] = (int)b;
    if (!(tt = basetype())) { err("bad va_arg"); tt = INT; }
    t = 0;
    type(&t, 0, tt);
    skip(')');
    ty = t + PTR;
    ind();
    break;

  case Paren:
    next();
    if ((tt = basetype())) {
      t = 0;
      type(&t, 0, tt);
      skip(')');
      expr(Inc);
      cast(t);
      ty = t;
      break;
    }
    expr(Comma);
    skip(')');
    break;

  // pre operations
  case Mul:
    next(); expr(Inc); ind();
    break;

  case And:
    next(); expr(Inc); addr();
    break;

  case '!':
    next(); expr(Inc);
    switch (*e) {
    case Eq: *e = Ne; break;
    case Ne: *e = Eq; break;
    case Lt: *e = Ge; break;
    case Ge: *e = Lt; break;
    case Ltu: *e = Geu; break;
    case Geu: *e = Ltu; break;
    case Eqf: *e = Nef; break;
    case Nef: *e = Eqf; break;
    case Ltf: *e = Gef; break;
    case Gef: *e = Ltf; break;
    default:
      if (ty < FLOAT || (ty & PMASK)) *(e-=2) = Not;
      else if (ty >= STRUCT) err("bad operand to !");
      else *(e-=2) = Notf;
      ty = INT;
    }
    break;

  case '~':
    next(); expr(Inc);
    if (ty >= FLOAT) err("bad operand to ~");
    else { if (*e == Num) e[2] = ~e[2]; else { *(e-=4) = Num; e[2] = -1; nodc(Xor,e+4,e); } ty = ty < UINT ? INT : UINT; }
    break;

  case Add:
    next(); expr(Inc);
    if (ty >= STRUCT) err("bad operand to +");
    break;

  case Sub:
    next(); expr(Inc);
    if (ty >= STRUCT) err("bad operand to -");
    else if (ty & FLOAT)
      { if (*e == Numf) *(double *)(e+2) *= -1.0; else { *(e-=4) = Numf; *(double *)(e+2) = -1; nodc(Mulf,e+4,e); } ty = DOUBLE; }
    else { if (*e == Num) e[2] *= -1; else { *(e-=4) = Num; e[2] = -1; nodc(Mul,e+4,e); } ty = ty < UINT ? INT : UINT; }
    break;

  case Inc:
    next(); expr(Inc);
    if (!(ty & PMASK) && ty >= FLOAT) err("bad operand to ++");
    else { *(e-=4) = Num; e[2] = tinc(ty); assign(Adda,e+4); }
    break;

  case Dec:
    next(); expr(Inc);
    if (!(ty & PMASK) && ty >= FLOAT) err("bad operand to --");
    else { *(e-=4) = Num; e[2] = tinc(ty); assign(Suba,e+4); }
    break;

  case Sizeof:
    next();
    if ((t = (tk == Paren))) next();
    if (t && (tt = basetype())) { ty = 0; type(&ty, 0, tt); }
    else { b = e; expr(Dot); e = b; } // XXX backout any data seg allocs
    *(e-=4) = Num; e[2] = tsize(ty);
    ty = INT;
    if (t) skip(')');
    break;

  default: next(); err("bad expression"); return;
  }

  while (tk >= lev) {
    b = e; t = ty;
    switch (tk) {
    case Comma: trim(); b = e; next(); expr(Assign); *(e-=2) = Comma; e[1] = (int)b; continue;
    case Assign: next(); expr(Assign); cast(t < UINT ? INT : t); ty = t; assign(Assign,b); continue;

    case Adda:
      next(); expr(Assign);
      if ((t & PAMASK) && ty <= UINT) { if ((tt = tinc(t)) > 1) { *(e-=4) = Num; e[2] = tt; mul(e+4); } ty = t; assign(Adda,b); }
      else if ((tt=t|ty) >= STRUCT) err("bad operands to +=");
      else if (tt & FLOAT) { e = flot(e,ty); ty = t; assign(Addaf,b); }
      else { ty = t; assign(Adda,b); }
      continue;

    case Suba:
      next(); expr(Assign);
      if ((t & PAMASK) && ty <= UINT) { if ((tt = tinc(t)) > 1) { *(e-=4) = Num; e[2] = tt; mul(e+4); } ty = t; assign(Suba,b); }
      else if ((tt=t|ty) >= STRUCT) err("bad operands to -=");
      else if (tt & FLOAT) { e = flot(e,ty); ty = t; assign(Subaf,b); }
      else { ty = t; assign(Suba,b); }
      continue;

    case Mula:
      next(); expr(Assign);
      if ((tt=t|ty) >= STRUCT) err("bad operands to *=");
      else if (tt & FLOAT) { e = flot(e,ty); ty = t; assign(Mulaf,b); }
      else { ty = t; assign(Mula,b); }
      continue;

    case Diva:
      next(); expr(Assign);
      if ((tt=t|ty) >= STRUCT) err("bad operands to /=");
      else if (tt & FLOAT) { e = flot(e,ty); ty = t; assign(Divaf,b); }
      else { ty = t; assign((tt & UINT) ? Dvua : Diva, b); }
      continue;

    case Moda:
      next(); expr(Assign);
      if ((tt=t|ty) >= FLOAT) err("bad operands to %="); else { ty = t; assign((tt & UINT) ? Mdua : Moda, b); }
      continue;

    case Anda:
      next(); expr(Assign);
      if ((t|ty) >= FLOAT) err("bad operands to &="); else { ty = t; assign(Anda,b); }
      continue;

    case Ora:
      next(); expr(Assign);
      if ((t|ty) >= FLOAT) err("bad operands to |="); else { ty = t; assign(Ora,b); }
      continue;

    case Xora:
      next(); expr(Assign);
      if ((t|ty) >= FLOAT) err("bad operands to ^="); else { ty = t; assign(Xora,b); }
      continue;

    case Shla:
      next(); expr(Assign);
      if ((t|ty) >= FLOAT) err("bad operands to <<="); else { ty = t; assign(Shla,b); }
      continue;

    case Shra:
      next(); expr(Assign);
      if ((tt=t|ty) >= FLOAT) err("bad operands to >>="); else { ty = t; assign((tt & UINT) ? Srua : Shra, b); }
      continue;

    case Cond:
      if (ty == DOUBLE || ty == FLOAT) *(b = e -= 2) = Nzf;
      next(); expr(Comma);
      d = e; t = ty; skip(':'); expr(Cond); dd = e;
      if (!((ty & PAMASK) && ((t & PAMASK) || t <= UINT))) {
        if ((t & PAMASK) && ty <= UINT) ty = t;
        else if ((tt=t|ty) >= STRUCT) err("bad conditional expression types");
        else if (tt & FLOAT) { dd = flot(dd,ty); d = flot(d,t); ty = DOUBLE; }
        else { ty = (tt & UINT) ? UINT : INT; }
      }
      node(Cond,b,d); e[3] = (int)dd;
      continue;

    case Lor:
      if (ty == DOUBLE || ty == FLOAT) *(b = e-=2) = Nzf;
      next(); expr(Lan);
      if (ty == DOUBLE || ty == FLOAT) *(e-=2) = Nzf;
      *(e-=2) = Lor; e[1] = (int)b; ty = INT;
      continue;

    case Lan:
      if (ty == DOUBLE || ty == FLOAT) *(b = e-=2) = Nzf;
      next(); expr(Or);
      if (ty == DOUBLE || ty == FLOAT) *(e-=2) = Nzf;
      *(e-=2) = Lan; e[1] = (int)b; ty = INT;
      continue;

    case Or:
      next(); expr(Xor);
      if ((tt=t|ty) >= FLOAT) err("bad operands to |");
      else { if (*b == Num && *e == Num) e[2] |= b[2]; else nodc(Or,b,e);  ty = (tt & UINT) ? UINT : INT; }
      continue;

    case Xor:
      next(); expr(And);
      if ((tt=t|ty) >= FLOAT) err("bad operands to ^");
      else { if (*b == Num && *e == Num) e[2] ^= b[2]; else nodc(Xor,b,e); ty = (tt & UINT) ? UINT : INT; }
      continue;

    case And:
      next(); expr(Eq);
      if ((tt=t|ty) >= FLOAT) err("bad operands to &");
      else { if (*b == Num && *e == Num) e[2] &= b[2]; else nodc(And,b,e); ty = (tt & UINT) ? UINT : INT; }
      continue;

    case Eq:
      next(); expr(Lt);
      if ((t < FLOAT || (t & PAMASK)) && (ty < FLOAT || (ty & PAMASK)))
        { if (*b == Num && *e == Num) e[2] = b[2] == e[2]; else nodc(Eq,b,e); }
      else if ((tt=t|ty) >= STRUCT) err("bad operands to ==");
      else if (tt & FLOAT) {
        d = flot(e,ty); b = flot(b,t);
        if (*b == Numf && *d == Numf) { *e = Num; e[2] = *(double *)(b+2) == *(double *)(d+2); } else nodc(Eqf,b,d);
      } else { if (*b == Num && *e == Num) e[2] = b[2] == e[2]; else nodc(Eq,b,e); }
      ty = INT;
      continue;

    case Ne:
      next(); expr(Lt);
      if ((t < FLOAT || (t & PAMASK)) && (ty < FLOAT || (ty & PAMASK)))
        { if (*b == Num && *e == Num) e[2] = b[2] != e[2]; else nodc(Ne,b,e); }
      else if ((tt=t|ty) >= STRUCT) err("bad operands to !=");
      else if (tt & FLOAT) {
        d = flot(e,ty); b = flot(b,t);
        if (*b == Numf && *d == Numf) { *e = Num; e[2] = *(double *)(b+2) != *(double *)(d+2); } else nodc(Nef,b,d);
      } else { if (*b == Num && *e == Num) e[2] = b[2] != e[2]; else nodc(Ne,b,e); }
      ty = INT;
      continue;

    case Lt:
      next(); expr(Shl);
      if ((t & PAMASK) && (ty & PAMASK)) { if (*b == Num && *e == Num) e[2] = (uint) b[2] < e[2]; else node(Ltu,b,e); }
      else if ((tt=t|ty) >= STRUCT) err("bad operands to <");
      else if (tt & FLOAT) {
        d = flot(e,ty); b = flot(b,t);
        if (*b == Numf && *d == Numf) { *e = Num; e[2] = *(double *)(b+2) < *(double *)(d+2); } else node(Ltf,b,d);
      } else if (tt & UINT) { if (*b == Num && *e == Num) e[2] = (uint) b[2] < e[2]; else node(Ltu,b,e); }
      else { if (*b == Num && *e == Num) e[2] = b[2] < e[2]; else node(Lt,b,e); }
      ty = INT;
      continue;

    case Gt:
      next(); expr(Shl);
      if ((t & PAMASK) && (ty & PAMASK)) { if (*b == Num && *e == Num) e[2] = (uint) b[2] > e[2]; else node(Ltu,e,b); }
      else if ((tt=t|ty) >= STRUCT) err("bad operands to >");
      else if (tt & FLOAT) {
        d = flot(e,ty); b = flot(b,t);
        if (*b == Numf && *d == Numf) { *e = Num; e[2] = *(double *)(b+2) > *(double *)(d+2); } else node(Ltf,d,b);
      } else if (tt & UINT) { if (*b == Num && *e == Num) e[2] = (uint) b[2] > e[2]; else node(Ltu,e,b); }
      else { if (*b == Num && *e == Num) e[2] = b[2] > e[2]; else node(Lt,e,b); }
      ty = INT;
      continue;

    case Le:
      next(); expr(Shl);
      if ((t & PAMASK) && (ty & PAMASK)) { if (*b == Num && *e == Num) e[2] = (uint) b[2] <= e[2]; else node(Geu,e,b); }
      else if ((tt=t|ty) >= STRUCT) err("bad operands to <=");
      else if (tt & FLOAT) {
        d = flot(e,ty); b = flot(b,t);
        if (*b == Numf && *d == Numf) { *e = Num; e[2] = *(double *)(b+2) <= *(double *)(d+2); } else node(Gef,d,b);
      } else if (tt & UINT) { if (*b == Num && *e == Num) e[2] = (uint) b[2] <= e[2]; else node(Geu,e,b); }
      else { if (*b == Num && *e == Num) e[2] = b[2] <= e[2]; else node(Ge,e,b); }
      ty = INT;
      continue;

    case Ge:
      next(); expr(Shl);
      if ((t & PAMASK) && (ty & PAMASK)) { if (*b == Num && *e == Num) e[2] = (uint) b[2] >= e[2]; else node(Geu,b,e); }
      else if ((tt=t|ty) >= STRUCT) err("bad operands to >=");
      else if (tt & FLOAT) {
        d = flot(e,ty); b = flot(b,t);
        if (*b == Numf && *d == Numf) { *e = Num; e[2] = *(double *)(b+2) >= *(double *)(d+2); } else node(Gef,b,d);
      } else if (tt & UINT) { if (*b == Num && *e == Num) e[2] = (uint) b[2] >= e[2]; else node(Geu,b,e); }
      else { if (*b == Num && *e == Num) e[2] = b[2] >= e[2]; else node(Ge,b,e); }
      ty = INT;
      continue;

    case Shl:
      next(); expr(Add);
      if ((tt=t|ty) >= FLOAT) err("bad operands to <<");
      else { if (*b == Num && *e == Num) e[2] = b[2] << e[2]; else node(Shl,b,e); ty = (tt & UINT) ? UINT : INT; }
      continue;

    case Shr:
      next(); expr(Add);
      if ((tt=t|ty) >= FLOAT) err("bad operands to >>");
      else if (tt & UINT) { if (*b == Num && *e == Num) e[2] = (uint)b[2] >> e[2]; else node(Sru,b,e); ty = UINT; }
      else { if (*b == Num && *e == Num) e[2] = b[2] >> e[2]; else node(Shr,b,e); ty = INT; }
      continue;

    case Add:
      next(); expr(Mul);
      if ((t & PAMASK) && ty <= UINT) { if ((tt = tinc(t)) > 1) { *(e-=4) = Num; e[2] = tt; mul(e+4); } add((uint *)b); ty = t; }
      else if ((ty & PAMASK) && t <= UINT)
        { if ((tt = tinc(ty)) > 1) { d = e; *(e-=4) = Num; e[2] = tt; mul(b); add((uint *)d); } else add((uint *)b); } // XXX refactor?
      else if ((tt=t|ty) >= STRUCT) err("bad operands to +");
      else if (tt & FLOAT) {
        d = flot(e,ty); b = flot(b,t);
        if (*b == Numf && *d == Numf) {
          *e = Numf; *(double *)(e+2) = *(double *)(b+2) + *(double *)(d+2);
        } else nodc(Addf,b,d);
        ty = DOUBLE;
      } else { add((uint *)b); ty = (tt & UINT) ? UINT : INT; }
      continue;

    case Sub:
      next(); expr(Mul);
      if ((t & PAMASK) && (ty & PAMASK) && (tt=tinc(t)) == tinc(ty)) {
        node(Sub,b,e);
        d = e;
        *(e-=4) = Num; e[2] = tt;
        node(Div,d,e);
        ty = INT;
      } else if ((t & PAMASK) && ty <= UINT) {
        if ((tt = tinc(t)) > 1) { *(e-=4) = Num; e[2] = tt; mul(e+4); }
        if (*e == Num) { e[2] *= -1; add((uint *)b); } else node(Sub,b,e);
        ty = t;
      } else if ((tt=t|ty) >= STRUCT) err("bad operands to -");
      else if (tt & FLOAT) {
        d = flot(e,ty); b = flot(b,t);
        if (*b == Numf && *d == Numf) {
          *e = Numf; *(double *)(e+2) = *(double *)(b+2) - *(double *)(d+2);
        } else node(Subf,b,d);
        ty = DOUBLE;
      } else {
        if (*e == Num) { e[2] *= -1; add((uint *)b); } else node(Sub,b,e);
        ty = (tt & UINT) ? UINT : INT;
      }
      continue;

    case Mul:
      next(); expr(Inc);
      if ((tt=t|ty) >= STRUCT) err("bad operands to *");
      else if (tt & FLOAT) {
        d = flot(e,ty); b = flot(b,t);
        if (*b == Numf && *d == Numf) {
          *e = Numf; *(double *)(e+2) = *(double *)(b+2) * *(double *)(d+2);
        } else nodc(Mulf,b,d);
        ty = DOUBLE;
      } else { mul(b); ty = (tt & UINT) ? UINT : INT; }
      continue;

    case Div:
      next(); expr(Inc);
      if ((tt=t|ty) >= STRUCT) err("bad operands to /");
      else if (tt & FLOAT) {
        d = flot(e,ty); b = flot(b,t);
        if (*b == Numf && *d == Numf && *(double *)(d+2)) {
          *e = Numf; *(double *)(e+2) = *(double *)(b+2) / *(double *)(d+2);
        } else node(Divf,b,d);
        ty = DOUBLE;
      }
      else if (tt & UINT) { if (*b == Num && *e == Num && e[2]) e[2] = b[2] / (uint)e[2]; else node(Dvu,b,e); ty = UINT; }
      else { if (*b == Num && *e == Num && e[2]) e[2] = b[2] / e[2]; else node(Div,b,e); ty = INT; }
      continue;

    case Mod:
      next(); expr(Inc);
      if ((tt=t|ty) >= FLOAT) err("bad operands to %");
      else if (tt & UINT) { if (*b == Num && *e == Num && e[2]) e[2] = b[2] % (uint)e[2]; else node(Mdu,b,e); ty = UINT; }
      else { if (*b == Num && *e == Num && e[2]) e[2] = b[2] % e[2]; else node(Mod,b,e); ty = INT; }
      continue;

    case Inc:
      next();
      if (!(ty & PMASK) && ty >= FLOAT) err("bad operand to ++"); // XXX doesn't support floats
      else { *(e-=4) = Num; e[2] = -tinc(ty); *(e-=2) = Suba; e[1] = (int)b; add((uint *)(e+2)); }
      continue;

    case Dec:
      next();
      if (!(ty & PMASK) && ty >= FLOAT) err("bad operand to --"); // XXX doesn't support floats
      else { *(e-=4) = Num; e[2] = tinc(ty); *(e-=2) = Suba; e[1] = (int)b; add((uint *)(e+2)); }
      continue;

    case Dot: // XXX do some optimization for x.y on stack or global, then work on x.y.z (cause it wont be done in rval or lval)
      addr(); //  a.b --> (&a)->b --> *((&a) + b)
    case Arrow:
      if ((ty & TMASK) != (STRUCT | PTR)) err("expected structure or union");
      next();
      if (tk != Id) { err("expected structure or union member"); continue; }
      for (m = ((struct_t *)(va+(ty>>TSHIFT)))->member; m; m = m->next) if (m->id == id) goto found;
      err("struct or union member not found");
      next();
      continue;
found:
      *(e-=4) = Num; e[2] = m->offset;
      add((uint *)(e+4));
      if ((m->type & TMASK) == ARRAY) ty = m->type;
      else { ty = m->type + PTR; ind(); }
      next();
      continue;

    case Brak: // XXX these dont quite work when used with pointers?  still? test?
      next();  // addr(); b = e; t = ty; // XXX
      expr(Comma);
      skip(']');
      d = e;
      *(e-=4) = Num; e[2] = tinc(t);
      mul(d);
      add((uint *)b);
      ty = t;
      ind();
      continue;

    case Paren: // function call
      if ((ty & TMASK) != FUN && (ty & TMASK) != (PTR|FUN)) err("bad function call type");
      else { t = *(uint *)(va+(ty>>TSHIFT)); tt = *(uint *)(va+(ty>>TSHIFT)+4); }
      next();
      d = e;
      b = 0;
      while (tk != ')') {
        expr(Assign);
        switch (tt & 3) {
        case 1: cast(DOUBLE); ty = DOUBLE; break;
        case 2: cast(INT); ty = INT; break;
        case 3: cast(UINT); ty = UINT;
        }
        tt >>= 2;
        *(e-=2) = (int)b; e[1] = ty; b = e;
        if (tk == Comma) next();
      }
      skip(')');
      node(Fcall,d,b);
      ty = t;
      continue;

    default:
      dprintf(2,"fatal compiler error expr() tk=%d\n",tk); exit(-1);
    }
  }
}

// expression generation
int lmod(int t)
{
  switch (t) {
  default: if (!(t & PMASK) && (t & TMASK) != FUN) err("can't dereference that type");
  case INT:
  case UINT:   return 0;
  case SHORT:  return LLS - LL;
  case USHORT: return LLH - LL;
  case CHAR:   return LLC - LL;
  case UCHAR:  return LLB - LL;
  case DOUBLE: return LLD - LL;
  case FLOAT:  return LLF - LL;
  }
}

int smod(int t)
{
  switch (t) {
  default: if (!(t & PMASK)) err("can't dereference that type");
  case INT:
  case UINT:   return 0;
  case SHORT:
  case USHORT: return SLH - SL;
  case CHAR:
  case UCHAR:  return SLB - SL;
  case DOUBLE: return SLD - SL;
  case FLOAT:  return SLF - SL;
  }
}

void lbf(int *b)
{
  double d;
  switch (*b) {
  case Auto: eml(LBL+lmod(b[1]), b[2]); return;
  case Static: emg(LBG+lmod(b[1]), b[2]); return;
  case Numf:
    d = *(double*)(b+2);
    if (((int)(d*256.0)<<8>>8)/256.0 == d) emi(LBIF, d*256.0);
    else { data = (data+7)&-8; *(double*)(gs + data) = d; emg(LBGD, data); data += 8; }
    return;
  default: rv(b); em(LBAD); return;
  }
}

void opf(int *a)
{
  int *b = (int *)a[2];
  switch (*b) {
  case Auto:
  case Static:
  case Numf: rv((int *)(a[1])); lbf(b); return;
  default:
    rv(b);
    switch (*(a=(int *)a[1])) {
    case Auto:
    case Static:
    case Numf: em(LBAD); rv(a); return;
    default: loc -= 8; em(PSHF); rv(a); em(POPG); loc += 8; return;
    }
  }
}
void opaf(int *a, int o, int comm)
{
  int t, *b = a + 2;
  a = (int *)a[1];
  t = a[1] == FLOAT || a[1] == DOUBLE; // XXX need more testing before confident
  switch (*a) {
  case Auto: // loc fop= expr
    if (comm && t) { rv(b); eml(LBL+lmod(a[1]),a[2]); }
    else { lbf(b); eml(LL+lmod(a[1]),a[2]); if (!t) em(a[1] < UINT ? CID : CUD); }
    em(o); if (!t) em(a[1] < UINT ? CDI : CDU); eml(SL+smod(a[1]),a[2]);
    return;
  case Static: // glo fop= expr
    if (comm && t) { rv(b); emg(LBG+lmod(a[1]),a[2]); }
    else { lbf(b); emg(LG+lmod(a[1]),a[2]); if (!t) em(a[1] < UINT ? CID : CUD); }
    em(o); if (!t) em(a[1] < UINT ? CDI : CDU); emg(SG+smod(a[1]),a[2]);
    return;
  case Ptr:
    switch (*b) {
    case Auto:
    case Static:
    case Numf: rv(a+2); lbf(b); loc -= 8; break; // *expr fop= simple
    default: rv(b); loc -= 8; em(PSHF); rv(a+2); em(POPG); break; // *expr fop= expr
    }
    em(PSHA); em(LX+lmod(a[1]));
    if (!t) em(a[1] < UINT ? CID : CUD);
    em(o); em(POPB); loc += 8; if (!t) em(a[1] < UINT ? CDI : CDU); em(SX+smod(a[1]));
    return;
  default: err("lvalue expected");
  }
}

void lbi(int i) { if (i<<8>>8 == i) emi(LBI,i); else { emi(LBI,i>>24); emi(LBHI,i<<8>>8); } }

void lb(int *b)
{
  switch (*b) {
  case Auto: eml(LBL+lmod(b[1]), b[2]); return;
  case Static: emg(LBG+lmod(b[1]), b[2]); return;
  case Num: lbi(b[2]); return;
  default: rv(b); em(LBA); return;
  }
}

void opt(int *a)
{
  int *b = (int *)a[2];
  switch (*b) {
  case Auto:
  case Static:
  case Num: rv((int *)(a[1])); lb(b); return;
  default:
    rv(b);
    switch (*(a=(int *)a[1])) {
    case Auto:
    case Static:
    case Num: em(LBA); rv(a); return;
    default: loc -= 8; em(PSHA); rv(a); em(POPB); loc += 8; return;
    }
  }
}

enum { OPI = ADDI - ADD, OPL = ADDL - ADD };

void opi(int o, int i) { if (i<<8>>8 == i) emi(o+OPI, i); else { emi(LBI,i>>24); emi(LBHI,i<<8>>8); em(o); } }

void op(int *a, int o)
{
  int t, *b = (int *)a[2];
  switch (*b) {
  case Auto: rv((int *)(a[1])); if ((t = lmod(b[1]))) { eml(LBL+t, b[2]); em(o); } else eml(o+OPL, b[2]); return;
  case Static: rv((int *)(a[1])); emg(LBG+lmod(b[1]), b[2]); em(o); return;
  case Num: rv((int *)(a[1])); opi(o,b[2]); return;
  default:
    rv(b);
    switch (*(a=(int *)a[1])) {
    case Auto:
    case Static:
    case Num: em(LBA); rv(a); em(o); return;
    default: loc -= 8; em(PSHA); rv(a); em(POPB); em(o); loc += 8; return;
    }
  }
}

void opa(int *a, int o, int comm)
{
  int t, *b = a + 2;
  a = (int *)a[1];
  switch (*a) {
  case Auto:
    if (*b == Num && b[2]<<8>>8 == b[2]) { eml(LL+lmod(a[1]),a[2]); emi(o+OPI,b[2]); } // loc op= num
    else if (*b == Auto && !lmod(b[1]))  { eml(LL+lmod(a[1]),a[2]); eml(o+OPL,b[2]); } // loc op= locint
    else if (comm) { rv(b); if ((t = lmod(a[1]))) { eml(LBL+t,a[2]); em(o); } else eml(o+OPL,a[2]); } // loc comm= expr
    else { lb(b); eml(LL+lmod(a[1]),a[2]); em(o); } // loc op= expr
    eml(SL+smod(a[1]),a[2]);
    return;
  case Static:
    if (*b == Num && b[2]<<8>>8 == b[2]) { emg(LG+lmod(a[1]),a[2]); emi(o+OPI,b[2]); } // glo op= num
    else if (*b == Auto && !lmod(b[1]))  { emg(LG+lmod(a[1]),a[2]); eml(o+OPL,b[2]); } // glo op= locint
    else if (comm) { rv(b); emg(LBG+lmod(a[1]),a[2]); em(o); } // glo comm= expr
    else { lb(b); emg(LG+lmod(a[1]),a[2]); em(o); } // glo op= expr
    emg(SG+smod(a[1]),a[2]);
    return;
  case Ptr:
    if (*b == Num && b[2]<<8>>8 == b[2]) { rv(a+2); em(LBA); em(LX+lmod(a[1])); emi(o+OPI,b[2]); } // *expr op= num
    else if (*b == Auto && !lmod(b[1]))  { rv(a+2); em(LBA); em(LX+lmod(a[1])); eml(o+OPL,b[2]); } // *expr op= locint
    else {
      switch (*b) {
      case Auto:
      case Static:
      case Num: rv(a+2); lb(b); loc -= 8; em(PSHA); em(LX+lmod(a[1])); em(o); em(POPB); loc += 8; break; // *expr op= simple
      default: rv(b); loc -= 8; em(PSHA); rv(a+2); em(LBA); em(LX+lmod(a[1])); em(o+OPL); emi(ENT,8); loc += 8; // *expr op= expr
      }
    }
    em(SX+smod(a[1])); // XXX many more (SX,imm) optimizations possible (here and elsewhere)
    return;
  default: err("lvalue expected");
  }
}

int testnot(int *a, int t);

int test(int *a, int t)
{
  int b;
  switch (*a) {
  case Eq:  opt(a); return emf(BE,  t);
  case Ne:  opt(a); return emf(BNE, t);
  case Lt:  opt(a); return emf(BLT, t);
  case Ge:  opt(a); return emf(BGE, t);
  case Ltu: opt(a); return emf(BLTU,t);
  case Geu: opt(a); return emf(BGEU,t);
  case Eqf: opf(a); return emf(BEF, t);
  case Nef: opf(a); return emf(BNEF,t);
  case Ltf: opf(a); return emf(BLTF,t);
  case Gef: opf(a); return emf(BGEF,t);
  case Lor: return test(a+2,test((int *)a[1],t));
  case Lan: b = testnot((int *)(a[1]),0); t = test(a+2,t); patch(b,ip); return t;
  case Not: return testnot(a+2,t);
  case Notf: rv(a+2); return emf(BZF,t);
  case Nzf: rv(a+2); return emf(BNZF,t);
  case Num: if (a[2]) return emf(JMP,t); return t;
  case Numf: if (*(double*)(a+2)) return emf(JMP,t); return t;
  default: rv(a); return emf(BNZ,t);
  }
}

int testnot(int *a, int t)
{
  int b;
  switch (*a) {
  case Eq:  opt(a); return emf(BNE, t);
  case Ne:  opt(a); return emf(BE,  t);
  case Lt:  opt(a); return emf(BGE, t);
  case Ge:  opt(a); return emf(BLT, t);
  case Ltu: opt(a); return emf(BGEU,t);
  case Geu: opt(a); return emf(BLTU,t);
  case Eqf: opf(a); return emf(BNEF,t);
  case Nef: opf(a); return emf(BEF, t);
  case Ltf: opf(a); return emf(BGEF,t);
  case Gef: opf(a); return emf(BLTF,t);
  case Lor: b = test((int *)a[1],0); t = testnot(a+2,t); patch(b,ip); return t;
  case Lan: return testnot(a+2,testnot((int *)a[1],t));
  case Not: return test(a+2,t);
  case Notf: rv(a+2); return emf(BNZF,t);
  case Nzf: rv(a+2); return emf(BZF,t);
  case Num: if (!a[2]) return emf(JMP,t); return t;
  case Numf: if (!*(double*)(a+2)) return emf(JMP,t); return t;
  default: rv(a); return emf(BZ,t);
  }
}

void rv(int *a)
{
  int c, t, *b; double d;
  ident_t *n;

  switch (*a) {
  case Addaf: opaf(a, ADDF, 1); return;
  case Subaf: opaf(a, SUBF, 0); return;
  case Mulaf: opaf(a, MULF, 1); return;
  case Divaf: opaf(a, DIVF, 0); return;

  case Adda: opa(a, ADD, 1); return;
  case Suba: opa(a, SUB, 0); return;
  case Mula: opa(a, MUL, 1); return;
  case Diva: opa(a, DIV, 0); return;
  case Dvua: opa(a, DVU, 0); return;
  case Moda: opa(a, MOD, 0); return;
  case Mdua: opa(a, MDU, 0); return;
  case Anda: opa(a, AND, 1); return;
  case Ora:  opa(a, OR,  1); return;
  case Xora: opa(a, XOR, 1); return;
  case Shla: opa(a, SHL, 0); return;
  case Shra: opa(a, SHR, 0); return;
  case Srua: opa(a, SRU, 0); return;

  case Assign:
    b = a+2;
    a = (int *)a[1];
    switch (*a) {
    case Auto:   rv(b); eml(SL+smod(a[1]), a[2]); return; // loc = expr
    case Static: rv(b); emg(SG+smod(a[1]), a[2]); return; // glo = expr
    case Ptr:
      switch (a[2]) {
      case Auto:   rv(b); eml(LBL+lmod(a[3]), a[4]); break; // *loc = expr // XXX all good? in the presense of multiple casts?
      case Static: rv(b); emg(LBG+lmod(a[3]), a[4]); break; // *glo = expr
      default:
        rv(a+2);
        switch (*b) {
        case Static:
        case Auto:
        case Num:
        case Numf: em(LBA); rv(b); break; // *expr = simple
        default:  loc -= 8; em(PSHA); rv(b); em(POPB); loc += 8; // *expr = expr
        }
      }
      em(SX+smod(a[1]));
      return;
    default: err("lvalue expected"); return;
    }

  case Addf: opf(a); em(ADDF); return;
  case Subf: opf(a); em(SUBF); return;
  case Mulf: opf(a); em(MULF); return;
  case Divf: opf(a); em(DIVF); return;

  case Add: op(a, ADD); return;
  case Sub: op(a, SUB); return;
  case Mul: op(a, MUL); return;
  case Div: op(a, DIV); return;
  case Dvu: op(a, DVU); return;
  case Mod: op(a, MOD); return;
  case Mdu: op(a, MDU); return;
  case And: op(a, AND); return;
  case Or:  op(a, OR);  return;
  case Xor: op(a, XOR); return;
  case Shl: op(a, SHL); return;
  case Shr: op(a, SHR); return;
  case Sru: op(a, SRU); return;

  case Eq:  opt(a); em(EQ); return;
  case Ne:  opt(a); em(NE); return;
  case Lt:  opt(a); em(LT); return;
  case Ge:  opt(a); em(GE); return;
  case Ltu: opt(a); em(LTU); return;
  case Geu: opt(a); em(GEU); return;
  case Eqf: opf(a); em(EQF); return;
  case Nef: opf(a); em(NEF); return;
  case Ltf: opf(a); em(LTF); return;
  case Gef: opf(a); em(GEF); return;

  case Cid: rv((int *)a[1]); em(CID); return;
  case Cud: rv((int *)a[1]); em(CUD); return;
  case Cdi: rv(a+2); em(CDI); return;
  case Cdu: rv(a+2); em(CDU); return;
  case Cic: rv(a+2); emi(SHLI,24); emi(SHRI,24); return;
  case Cuc: rv(a+2); emi(ANDI,255); return;
  case Cis: rv(a+2); emi(SHLI,16); emi(SHRI,16); return;
  case Cus: rv(a+2); emi(ANDI,0xffff); return;

  case Comma: rv((int *)a[1]); rv(a+2); return;
  case Cond: t = testnot((int *)a[1],0); rv((int *)a[2]); c = emf(JMP,0); patch(t,ip); rv((int *)a[3]); patch(c,ip); return;

  case Lor:
    t = test(a+2,test((int *)a[1],0));
    emi(LI,0);
    c = emf(JMP,0);
    patch(t,ip);
    emi(LI,1);
    patch(c,ip);
    return;

  case Lan:
    t = testnot(a+2,testnot((int *)a[1],0));
    emi(LI,1);
    c = emf(JMP,0);
    patch(t,ip);
    emi(LI,0);
    patch(c,ip);
    return;

  case Not:
    rv(a+2);
    emi(LBI,0);
    em(EQ);
    return;

  case Notf:
    rv(a+2);
    emi(LBIF,0);
    em(EQF);
    return;

  case Num:
    if (a[2]<<8>>8 == a[2]) emi(LI,a[2]); else { emi(LI,a[2]>>24); emi(LHI,a[2]<<8>>8); }
    return;

  case Numf:
    d = *(double*)(a+2);
    if (((int)(d*256.0)<<8>>8)/256.0 == d) emi(LIF, d*256.0);
    else { data = (data+7)&-8; *(double*)(gs + data) = d; emg(LGD, data); data += 8; }
    return;

  case Ptr:
    t = a[1];
    if (a[2] == Add && *(int *)a[4] == Num && (c = ((int *)a[4])[2], c<<8>>8 == c)) {
      rv((int *)a[3]);
      emi(LX+lmod(t),c);
      return;
    }
    rv(a+2);
    em(LX+lmod(t));
    return;

  case Lea:  eml(LEA,  a[2]); return;
  case Leag: emg(LEAG, a[2]); return;

  case Auto:   eml(LL+lmod(a[1]), a[2]); return;
  case Static: emg(LG+lmod(a[1]), a[2]); return;

  case Fun: emj(LEAG, a[2]); return;

  case FFun:
    n = (ident_t *)a[2];
    n->val = emf(LEAG, n->val);
    return;

  case Fcall:
    b = (int *)a[2];
    a = (int *)a[1];
    for (t = 0; b; t += 8) {
      if (b[1] == DOUBLE || b[1] == FLOAT) { rv(b+2); loc -= 8; em(PSHF); }
      else if (b[2] == Num && b[4]<<8>>8 == b[4]) { loc -= 8; emi(PSHI,b[4]); }
      else { rv(b+2); loc -= 8; em(PSHA); }
      b = (int *)*b;
    }
    if (*a == FFun) { n = (ident_t *)a[2]; n->val = emf(JSR, n->val); }
    else if (*a == Fun) emj(JSR, a[2]);
    else { rv(a); em(JSRA); } // function address
    if (t) { emi(ENT,t); loc += t; }
    return;

  default:
    dprintf(2,"fatal compiler error rv(int *a) *a=%d\n",*a); exit(-1);
  }
}

// statement
void stmt()
{
  static int brk, cont, def;
  int a, b, c, d, *es, *et, cmin, cmax;

  switch (tk) {
  case If:
    next(); skip(Paren);
    es = e;
    expr(Comma); if (ty == DOUBLE || ty == FLOAT) *(e-=2) = Nzf;
    a = testnot(e,0);
    e = es;
    skip(')');
    stmt();
    if (tk == Else) {
      next();
      b = emf(JMP,0);
      patch(a,ip);
      stmt();
      a = b;
    }
    patch(a,ip);
    return;

  case While:
    b = brk; brk = 0;
    c = cont; cont = emf(JMP, 0);
    a = ip;
    es = e;
    next(); skip(Paren);
    expr(Comma); if (ty == DOUBLE || ty == FLOAT) *(e-=2) = Nzf;
    skip(')');
    stmt();
    patch(cont,ip); cont = c;
    patch(test(e,0), a);
    e = es;
    patch(brk,ip); brk = b;
    return;

  case Return:
    next();
    if (tk != ';') {
      es = e;
      expr(Comma);
      cast(rt);
      rv(e);
      e = es;
    }
    emi(LEV,-loc);
    skip(';');
    return;

  case Break:
    brk = emf(JMP, brk);
    next(); skip(';');
    return;

  case Continue:
    cont = emf(JMP, cont);
    next(); skip(';');
    return;

  case For:
    next(); skip(Paren);
    if (tk != ';') {
      es = e;
      expr(Comma);
      trim();
      rv(e);
      e = es;
    }
    skip(';');
    es = et = 0;
    if (tk != ';') { es = e; expr(Comma); if (ty == DOUBLE || ty == FLOAT) *(e-=2) = Nzf; }
    skip(';');
    if (tk != ')') { et = e; expr(Comma); }
    skip(')');
    if (es) d = emf(JMP, 0);
    a = ip;
    b = brk; brk = 0;
    c = cont; cont = 0;
    stmt();
    patch(cont, (es || et) ? ip : a);
    cont = c;
    if (et) { trim(); rv(e); e = et; }
    if (es) {
      patch(d,ip);
      patch(test(e,0), a);
      e = es;
    } else
      emj(JMP, a);
    patch(brk,ip); brk = b;
    return;

  case Do:
    next();
    b = brk; brk = 0;
    c = cont; cont = 0;
    a = ip;
    stmt();
    patch(cont,ip); cont = c;
    skip(While); skip(Paren);
    es = e;
    expr(Comma); if (ty == DOUBLE || ty == FLOAT) *(e-=2) = Nzf;
    patch(test(e,0), a);
    e = es;
    skip(')'); skip(';');
    patch(brk,ip); brk = b;
    return;

  case Switch:
    next(); skip(Paren);
    es = e;
    expr(Comma);
    rv(e);
    e = es;
    a = emf(JMP, 0);
    skip(')');
    b = brk; brk = 0;
    d = def; def = 0;
    es = e;
    stmt();
    brk = emf(JMP, brk);
    patch(a,ip);
    if (es == e) { //err("no case in switch statement");   XXX
      if (def) emj(JMP, def);
    } else {  // XXX lots of possible signed/unsigned under/overflow issues in this block
      cmin = cmax = *(et = es - 2);
      while (et > e) { et -= 2; if ((c = *et) > cmax) cmax = c; else if (c < cmin) cmin = c; }
      et = es;
      if ((uint)(cmax - cmin) <= ((int)es - (int)e)*2) { // jump table
        if (cmin > 0 && cmax <= (int)es - (int)e) cmin = 0;
        else if (cmin) { opi(SUB, cmin); cmax -= cmin; }
        lbi(++cmax);
        data = (data + 3) & -4;
        if (def) { emj(BGEU, def); emg(JMPI, data); def -= ip; }
        else { brk = emf(BGEU, brk); emg(JMPI, data); }
        for (c = 0; c < cmax; ) ((int *)(gs + data))[c++] = def;
        while (et > e) { et -= 2; ((int *)(gs + data))[*et - cmin] = et[1] - ip; }
        data += cmax * 4;
      } else { // jump list
//        printf("jump list at line %d\n",line);
        while (et > e) { et -= 2; lbi(*et); emj(BE, et[1]); }  // XXX overflow issues?
        if (def) emj(JMP, def);
      }
    }
    def = d;
    e = es;
    patch(brk,ip); brk = b;
    return;

  case Case:
    next();
    a = *(e-=2) = imm(); e[1] = ip;
    if (tk == Dots) {
      next();
      b = imm();
      while (a < b) { *(e-=2) = ++a; e[1] = ip; }
    }
    skip(':');
    stmt();
    return;

  case Asm:
    next();
    skip(Paren);
    a = imm();
    if (tk == Comma) { next(); emi(a, imm()); } else em(a);
    skip(')');
    skip(';');
    return;

  case Va_start:  // va_start(ap,v) ap = &v;
    next();
    skip(Paren);
    es = e;
    expr(Assign);
    b = (int)e;
    skip(Comma);
    expr(Assign);
    addr();
    *(e-=2) = Assign; e[1] = b;
    rv(e);
    e = es;
    skip(')');
    skip(';');
    return;

  case Default:
    next(); skip(':');
    def = ip;
    stmt();
    return;

  case Goto:
    next();
    if (tk == Id) {
      if (!id->class) {
        ploc->class = 0;
        ploc->id = id;
        ploc++;
        id->class = FLabel;
        id->val = emf(JMP,0);
      }
      else if (id->class == FLabel) id->val = emf(JMP,id->val);
      else if (id->class == Label) emj(JMP,id->val);
      else err("bad label name");
    }
    else
      err("bad goto");
    next(); skip(';');
    return;

  case '{':
    next();
    while (tk != '}') stmt();
  case ';':
    next();
    return;

  case Id:
    if (*pos == ':') {
      pos++;
      //printf("+ + + processing label\n");  // XXX put on local list if 0 or global
      if (!id->class) {
        ploc->class = 0;
        ploc->id = id;
        ploc++;
        id->class = Label;
        id->val = ip;
      } else if (id->class == FLabel) {
        patch(id->val,ip);
        id->class = Label;
        id->val = ip;
      } else
        err("bad label name"); // XXX should let labels override globals just like other locals
      next();
      stmt();
      return;
    }
  default:
    es = e;
    expr(Comma);
    trim();
    rv(e);
    e = es;
    skip(';');
  }
}

int main(int argc, char *argv[])
{
  int i, amain, text, *patchdata, *patchbss, sbrk_start;
  ident_t *tmain;
  char *outfile;
  struct { uint magic, bss, entry, flags; } hdr;
  struct stat st;

  cmd = *argv;
  if (argc < 2) goto usage;
  outfile = 0;
  file = *++argv;
  while (--argc && *file == '-') {
    switch (file[1]) {
    case 'v': verbose = 1; break;
    case 's': debug = 1; break;
    case 'I': incl = file + 2; break;
    case 'o': if (argc > 1) { outfile = *++argv; argc--; break; }
    default: usage: dprintf(2,"usage: %s [-v] [-s] [-Ipath] [-o exefile] file ...\n", cmd); return -1;
    }
    file = *++argv;
  }

  sbrk_start = (int) sbrk(0);
  ts = ip = (int) new(SEG_SZ);
  gs      = (int) new(SEG_SZ);
  va = vp = (int) new(VAR_SZ);

  bigend = 1; bigend = ((char *)&bigend)[3];

  pos = "asm auto break case char continue default do double else enum float for goto if int long return short "
        "sizeof static struct switch typedef union unsigned void while va_list va_start va_arg main";
  for (i = Asm; i <= Va_arg; i++) { next(); id->tk = i; }
  next();
  tmain = id;

  line = 1;
  if (stat(file, &st)) { dprintf(2,"%s : [%s:%d] error: can't stat file %s\n", cmd, file, line, file); return -1; } // XXX fstat inside mapfile?
  pos = mapfile(file, st.st_size);

  e = new(EXPR_SZ) + EXPR_SZ;
  pdata = patchdata = new(PSTACK_SZ);
  pbss  = patchbss  = new(PSTACK_SZ);
  ploc  =             new(LSTACK_SZ);

  if (verbose) dprintf(2,"%s : compiling %s\n", cmd, file);
  if (debug) dline();
  next();
  decl(Static);
  if (!errs && ffun) err("unresolved forward function (retry with -v)");

  ip = (ip + 7) & -8;
  text = ip - ts;
  data = (data + 7) & -8;
  bss = (bss + 7) & -8;

  if (text + data + bss > SEG_SZ) err("text + data + bss segment exceeds maximum size");
  if (!(amain = tmain->val)) err("main() not defined");

  if (verbose || errs) dprintf(2,"%s : %s compiled with %d errors\n", cmd, file, errs);
  if (verbose) dprintf(2,"entry = 0x%x text = 0x%x data = 0x%x bss = 0x%x\n", amain - ts, text, data, bss);

  if (!errs && !debug) {
    while (pdata != patchdata) { pdata--; *(int *)*pdata += (ip        - *pdata - 4) << 8; }
    while (pbss  != patchbss ) { pbss--;  *(int *)*pbss  += (ip + data - *pbss  - 4) << 8; }
    if (outfile) {
      if ((i = open(outfile, O_WRONLY | O_CREAT | O_TRUNC)) < 0)
        { dprintf(2,"%s : error: can't open output file %s\n", cmd, outfile); return -1; }
      hdr.magic = 0xC0DEF00D;
      hdr.bss   = bss;
      hdr.entry = amain - ts;
      hdr.flags = sizeof(hdr)+text;
      write(i, &hdr, sizeof(hdr));
      write(i, (void *) ts, text);
      write(i, (void *) gs, data);
      close(i);
    } else {
      memcpy((void *)ip, (void *)gs, data);
      sbrk(sbrk_start + text + data + 8 - (int)sbrk(0)); // free compiler memory
      sbrk(bss);
      if (verbose) dprintf(2,"%s : running %s\n", cmd, file);
      errs = ((int (*)())amain)(argc, argv);
      if (verbose) dprintf(2,"%s : %s main returned %d\n", cmd, file, errs);
    }
  }
  if (verbose) dprintf(2,"%s : exiting\n", cmd);
  return errs;
}
