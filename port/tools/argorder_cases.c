/*
 * Self-test cases for port/tools/argorder_sweep.py (--selftest). Each function holds one
 * expression; its comment gives the evaluation order both compilers really used, read from
 * agbcc -O2 -mthumb-interwork (tools/agbcc/bin/agbcc, the GBA build's flags) and from the
 * host GCC's -fdump-tree-gimple with HOST_CFLAGS (-m32 -std=gnu89 -O1), round 9
 * (port-plan/pending/r9-argorder.md). "expect KIND GBA / PC" is the order the sweep must
 * predict for its site (part or argument indices, first evaluated first); "expect none":
 * both compilers take the same order, so the sweep must not record a site.
 * Not part of any build.
 */
struct U { int hp; int x; };
int g, gi;
struct U *gp;
struct U gs;
int arr[8];
int **gpp;
void (*fp)(int);
void (*fpt[4])(int);
int *pa(void) { return &arr[g]; }
int a(void) { g++; gi++; gp = 0; arr[0] = 1; gs.x = 2; fp = 0; return g; }
int b(void) { return g; }
int c(void) { return 0; }
int d(void) { return 0; }
int e(void) { return 0; }
int k(void) { return 0; }
int h(int x) { return x; }
void f2(int x, int y) { }
void f3(int x, int y, int z) { }
void f4(int x, int y, int z, int w) { }
void f5(int x, int y, int z, int w, int v) { }
void f6(int x, int y, int z, int w, int v, int u) { }

/* call arguments: agbcc left to right, GCC right to left */
void t01(void) { f2(a(), b()); }                        /* expect call 0,1 / 1,0 */
void t02(int x) { f3(x, a(), b()); }                    /* expect call 0,1,2 / 2,1,0 */
void t03(void) { f4(a(), b(), c(), d()); }              /* expect call 0,1,2,3 / 3,2,1,0 */
void t06(void) { f2(g, a()); }                          /* expect call 0,1 / 1,0 */
void t07(void) { f2(a(), g); }                          /* expect call 0,1 / 1,0 */
void t08(struct U *p) { f2(p->x, a()); }                /* expect call 0,1 / 1,0 */
void t10(void) { f2(g++, a()); }                        /* expect call 0,1 / 1,0 */
void t20(void) { f4(1, a(), 2, b()); }                  /* expect call 0,1,2,3 / 3,2,1,0 */
void t27(void) { f2(a() % 9 - 4, a() % 9 - 4); }        /* expect call 0,1 / 1,0 */
/* stack arguments: agbcc evaluates the arguments with calls first (precompute_arguments) */
void t04(void) { f5(a(), b(), c(), d(), e()); }         /* expect call 0,1,2,3,4 / 4,3,2,1,0 */
void t23(void) { f5(g, 2, 3, 4, a()); }                 /* expect call 4,0,1,2,3 / 4,3,2,1,0 */
void t24(void) { f5(a(), 2, 3, 4, g); }                 /* expect call 0,1,2,3,4 / 4,3,2,1,0 */
void t25(void) { f6(1, 2, 3, 4, a(), b()); }            /* expect call 4,5,0,1,2,3 / 5,4,3,2,1,0 */
/* operators: both take calls left to right; agbcc takes the calls before the plain reads */
int t12(void) { return a() - b(); }                     /* expect none */
int t26(void) { return (a() % 9 - 4) + (b() % 9 - 4); } /* expect none */
int u11(void) { return a() + g; }                       /* expect none */
int u12(void) { return g + a(); }                       /* expect none */
int v01(void) { return g < a(); }                       /* expect none */
int v02(struct U *p) { return p->x < a(); }             /* expect bin 1,0 / 0,1 */
int v03(void) { return g - a(); }                       /* expect bin 1,0 / 0,1 */
int v05(void) { return (a() ? g : 0) + b(); }           /* expect bin 1,0 / 0,1 */
int v08(void) { return gp->x + a(); }                   /* expect bin 1,0 / 0,1 */
int v11(void) { return (g + 1) + a(); }                 /* expect bin 1,0 / 0,1 */
int v12(void) { return a() + (g + 1); }                 /* expect none */
int v13(void) { return (g & 3) == a(); }                /* expect bin 1,0 / 0,1 */
/* assignments */
void u01(void) { gp->x = a(); }                         /* expect asg 1,0 / 0,1 */
void u02(void) { g += a(); }                            /* expect none */
void u04(void) { arr[g] = a(); }                        /* expect asg 1,0 / 0,1 */
void u06(void) { gs.x = a(); }                          /* expect none */
void u07(void) { arr[gi] += a(); }                      /* expect none */
void t16(void) { arr[a()] = b(); }                      /* expect none */
void v04(void) { arr[a()] += b(); }                     /* expect asg 0,1 / 1,0 */
void v10(void) { arr[gi] = a() + b(); }                 /* expect none */
/* calls through a function pointer: agbcc loads it after the arguments */
void u09(void) { fp(a()); }                             /* expect fnptr 1,0 / 0,1 */
