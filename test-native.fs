\ ---- native (SLJIT) tests ----

\ square via LIR
CODE sq
  mov R0, [S1-8]
  mul R0, R0, R0
  mov [S1-8], R0
;CODE
5 sq . CR              \ 25

\ native words are usable inside colon definitions
: t2 sq sq ;
3 t2 . CR              \ 81

\ labels, signed comparison and conditional jump
CODE nmax
  mov R0, [S1-8]
  mov R1, [S1-16]
  cmp gt R1, R0, @big
  mov [S1-16], R0
  add S1, S1, #-8
  ret S1
label: big
  mov [S1-16], R1
  add S1, S1, #-8
  ret S1
;CODE
3 7 nmax . CR          \ 7
9 2 nmax . CR          \ 9

\ local stack
CODE loc locals 16
  mov R0, #10
  mov [SP+0], R0
  mov R1, [SP+0]
  mov [S1], R1
  add S1, S1, #8
  mov R0, S1
;CODE
loc . CR               \ 10

\ IR fragment embedded in CODE
CODE add5
  IR" add S1, S1, #-8"
  mov R0, [S1]
  add R0, R0, #5
  mov [S1], R0
  add S1, S1, #8
  mov R0, S1
;CODE
10 add5 . CR           \ 15

\ raw IR string -> code pointer -> named native word
S" enter P 4 2 0 P mov S1 S0 mov R0 [S1-8] add R0 R0 #100 mov [S1-8] R0 ret S1" ASSEMBLE NATIVE plus100
7 plus100 . CR         \ 107

\ icall to libc putchar: ( arg fn -- ret )
CODE pc
  mov R2, [S1-8]
  mov R1, [S1-16]
  add S1, S1, #-16
  mov R0, R1
  sig W 32
  icall R2
  mov [S1], R0
  add S1, S1, #8
  mov R0, S1
;CODE
67 C-PUTCHAR pc . CR   \ C67

\ generic escape hatch: op2 with a named SLJIT opcode (a - b)
CODE gsub
  mov R0, [S1-8]
  mov R1, [S1-16]
  op2 SLJIT_SUB R0 R1 R0
  mov [S1-16], R0
  add S1, S1, #-8
  mov R0, S1
;CODE
10 3 gsub . CR         \ 7

\ dynamic linking: call libc strlen via &name
CODE slen
  mov R0, [S1-16]
  sig W P
  icall &strlen
  add S1, S1, #-8
  mov [S1-8], R0
;CODE
S" hello world" slen . CR   \ 11

\ resolve a symbol in a named library (auto dlopen at assembly)
CODE sqrt-addr
  op1 SLJIT_MOV R0, &libm.so.6::sqrt
  mov [S1] R0
  add S1, S1, #8
  mov R0 S1
;CODE
sqrt-addr . CR              \ non-zero address

\ Forth-level dlopen / dlsym against the same registry
S" libc.so.6" DLOPEN . CR   \ non-zero handle
S" libc.so.6" DLOPEN S" getpid" DLSYM . CR

\ ---- expanded SLJIT surface ----

\ flags: materialize a comparison as 0/1
CODE iszero
  mov R0, [S1-8]
  setflags zero R0, #0
  flags SLJIT_ZERO R0
  mov [S1-8], R0
  mov R0, S1
;CODE
0 iszero . CR                \ 1
7 iszero . CR                \ 0

\ raw instruction bytes
CODE raw
  custom 1 0x90
  mov R0, S1
;CODE
raw .S CR

\ floating point: 1.5 + 2.25 = 3.75 (raw double bits)
CODE fadd
  mov R0, [S1-8]
  mov R1, [S1-16]
  fcopy SLJIT_COPY_TO_F64 F0, R0
  fcopy SLJIT_COPY_TO_F64 F1, R1
  fop2 SLJIT_ADD_F64 F0, F1, F0
  fcopy SLJIT_COPY_FROM_F64 F0, R0
  mov [S1-16], R0
  add S1, S1, #-8
  mov R0, S1
;CODE
$3ff8000000000000 $4002000000000000 fadd . CR   \ 0x400e000000000000 = 3.75

\ rewritable constant patched at runtime
CODE cval
  rwconst K SLJIT_MOV R0, #0
  mov [S1] R0
  add S1, S1, #8
  mov R0, S1
;CODE
cval . CR                    \ 0
S" K" 123 SET-CONST
cval . CR                    \ 123

\ inline (macro expansion, labels renamed)
CODE min2
  mov R0, [S1-8]
  mov R1, [S1-16]
  cmp lt R1, R0, @keep
  mov R1, R0
label: keep
  mov [S1-16], R1
  add S1, S1, #-8
  mov R0, S1
;CODE
CODE min4
  inline min2
  inline min2
;CODE
1 2 3 4 min4 . . CR          \ 2 1

\ introspection
SLJIT_HAS_FPU CPU-FEATURE? . CR   \ 1

CR

\ ---- exceptions (CATCH / THROW) ----

: boom 42 THROW ;
' boom CATCH . CR            \ 42

: okword 7 ;
' okword CATCH . . CR        \ 0 7

\ EXECUTE and ['] (XT)
1 2 ' + EXECUTE . CR         \ 3
: call+ ['] + EXECUTE ;
1 2 call+ . CR               \ 3

\ ABORT"
: chk DUP 0< ABORT" negative!" ;
5 chk . CR                   \ 5
-3 ' chk CATCH . DROP CR     \ -2 (then drop the restored -3)

\ division by zero is catchable
1 0 ' / CATCH . DROP DROP CR \ -10 (then drop the restored 1 0)
1 0 ' / CATCH DROP ERROR-CODE . DROP DROP CR   \ -10
' boom CATCH DROP ERROR-CODE . CR    \ 42

\ native THROW via icall &forth_raise
CODE nthrow
  mov R0, #99
  sig V W
  icall &forth_raise
  mov R0, S1
;CODE
' nthrow CATCH . CR          \ 99

CR

\ ---- dictionary access ----
S" DUP" FIND ' DUP = . CR    \ -1
S" NOPE" FIND . CR           \ 0
: foo DUP * ;
' foo COLON? . CR            \ -1
' foo >NAME .S 2DROP CR      \ <2> addr 3
VARIABLE v
' v VARIABLE? . CR           \ -1
' foo SEE
' v SEE

\ native CODE calling a dictionary helper directly
CODE nfind
  mov R0, [S1-16]
  mov R1, [S1-8]
  sig P P 32
  icall &forth_find
  mov [S1-16], R0
  add S1, S1, #-8
  mov R0, S1
;CODE
S" DUP" nfind ' DUP = . CR    \ -1

CR

\ ---- PICK ----
5 6 7 0 PICK . CR            \ 7
5 6 7 2 PICK . CR            \ 5

CR

\ ---- per-arch raw emission (x86-64) ----
ARCH . CR                    \ 1 on x86-64
CODE printf1
  mov R3, &printf
  mov R2, [S1-16]
  mov R1, [S1-8]
  add S1, S1, #-16
  asm.mov.al 0
  asm.call R3
  mov R0, S1
;CODE
S" value=%ld" DROP 42 printf1 CR   \ value=42
CODE rawdemo
  asm.nop
  asm.mov R0 S1
;CODE
rawdemo .S CR

CR

\ comments inside CODE bodies
CODE cadd ( n -- n+100 )
  mov R0, [S1-8]   \ TOS
  add R0, R0, #100 ( plus 100 )
  mov [S1-8], R0
  mov R0, S1
;CODE
5 cadd . CR          \ 105

CR

\ ---- native underflow protection (catchable) ----
' DUP CATCH . CR              \ -4
' PICK CATCH . CR             \ -4
1 2 + . CR                    \ 3

CR

\ ---- return stack, addresses, strings ----
1 >R R@ . R> . CR            \ 1 1
1 2 2>R 2R> . . CR           \ 2 1
5 6 SP@ @ . CR               \ 6
S" hi" TYPE CR               \ hi
: rtest 1 2 3 >R >R >R R> R> R> . . . ;
rtest CR                     \ 3 2 1
: qtest ." n=" 3 . ;
qtest CR                     \ n=3

CR

\ ---- conditional compilation and PARSE-NAME ----
ARCH 1 = [IF] 111 [ELSE] 222 [THEN] . CR   \ 111 on x86-64
PARSE-NAME hello TYPE CR                   \ hello

CR

\ ---- control flow defined in Forth ----
: ft1 IF 111 ELSE 222 THEN ;
0 ft1 . -1 ft1 . CR          \ 222 111
: ft2 BEGIN DUP . 1- DUP 0= UNTIL DROP ;
3 ft2 CR                     \ 3 2 1
: ft3 BEGIN DUP 0> WHILE DUP . 1- REPEAT DROP ;
3 ft3 CR                     \ 3 2 1
: ft4 [ 10 20 + ] LITERAL . ;
ft4 CR                       \ 30

CR

\ ---- CREATE / DOES> and defining words in Forth ----
: mk CREATE , DOES> @ ;
123 mk n123
n123 . CR                    \ 123
: mk2 CREATE , DOES> @ 2 * ;
5 mk2 d5
d5 . CR                      \ 10
42 CONSTANT answer
answer . CR                  \ 42
VARIABLE vv
9 vv ! vv @ . CR             \ 9

CR

\ ---- call-threaded JIT ----
: jsq DUP * ;
5 jsq . CR                    \ 25
JIT jsq
5 jsq . CR                    \ 25

: jabs DUP 0< IF NEGATE THEN ;
JIT jabs
-5 jabs . 3 jabs . CR         \ 5 3

: jinc 1+ ;
: jtwice jinc jinc ;
JIT jinc
JIT jtwice
3 jtwice . CR                 \ 5

JIT-ALL . CR                  \ number of words compiled

CR

\ ---- POSTPONE / [COMPILE] / :NONAME / DEFER ----
: TIF POSTPONE IF ; IMMEDIATE
: TTHEN POSTPONE THEN ; IMMEDIATE
: tif-test TIF 42 TTHEN ;
-1 tif-test . CR              \ 42
: COMPILE-DUP POSTPONE DUP ; IMMEDIATE
: cdup-test COMPILE-DUP ;
5 cdup-test . . CR            \ 5 5
:NONAME 7 ; EXECUTE . CR      \ 7
DEFER dgreet
: dhello ." ok" ;
' dhello IS dgreet
dgreet CR                     \ ok

CR

\ ---- SYNONYM / MARKER / FORGET ----
: plus2 + ;
SYNONYM add2 plus2
1 2 add2 . CR            \ 3
MARKER mm
: tempdef 123 ;
tempdef . CR             \ 123
mm
: aa 1 ;
: bb 2 ;
FORGET bb
aa . CR                 \ 1

CR

\ ---- CASE / UNLOOP ----
: ctest CASE 1 OF 111 ENDOF 2 OF 222 ENDOF 333 SWAP ENDCASE ;
1 ctest . 2 ctest . 3 ctest . CR    \ 111 222 333
: utest 10 0 DO I 5 = IF UNLOOP 99 EXIT THEN LOOP ;
utest . CR                          \ 99

CR

\ ---- ?DO / LEAVE ----
: qtest 0 0 ?DO 999 LOOP 42 ;
qtest . CR                  \ 42
: ltest 10 0 DO I 5 = IF LEAVE THEN I . LOOP ;
ltest CR                    \ 0 1 2 3 4

CR

CR

\ ---- [IF] multi-line ----
0 [IF]
: neverdef 1 ;
[ELSE]
: chosen 2 ;
[THEN]
chosen . CR                  \ 2
1 [IF]
: yesdef 3 ;
[ELSE]
: nodef 4 ;
[THEN]
yesdef . CR                  \ 3
1 [IF]
 0 [IF]
  99
 [ELSE]
  : nested 11 ;
 [THEN]
[THEN]
nested . CR                  \ 11
: ccond [ 1 ] [IF] 10 [ELSE] 20 [THEN] ;
ccond . CR                   \ 10

CR

CR

\ ---- locals { a b -- c } ----
: ladd { a b -- c } a b + ;
3 4 ladd . CR                \ 7
: lsq { n -- } n n * ;
5 lsq . CR                   \ 25
: lsum { n -- r } 0 n 0 ?DO I + LOOP ;
10 lsum . CR                 \ 45
: lfact { n -- r } n 1 <= IF 1 ELSE n 1- RECURSE n * THEN ;
6 lfact . CR                 \ 720
: learly { n -- r } n 0< IF 0 EXIT THEN n 10 * ;
5 learly . -3 learly . CR    \ 50 0
: lstore { a b -- c } a b + TO c c ;
10 20 lstore . CR            \ 30
: jladd { a b -- c } a b + ;
JIT jladd
2 5 jladd . CR               \ 7

CR

CR

\ ---- strings / byte memory ----
S" abc" S" abc" COMPARE . CR          \ 0
S" abc" S" abd" COMPARE . CR          \ -1
S" abd" S" abc" COMPARE . CR          \ 1
S" hello world" S" wor" SEARCH . TYPE CR   \ -1 world
S" xyz" S" QQ" SEARCH . TYPE CR       \ 0 xyz
S" hi   " -TRAILING NIP . CR          \ 2
S" abcdef" 2 /STRING TYPE CR          \ cdef
0 0 S" 1234" >NUMBER 2DROP . . CR     \ 0 1234
0 0 S" 12ab" >NUMBER TYPE CR          \ ab
: greet S" hi!" TYPE ; greet CR       \ hi!
CHAR A . CR                           \ 65
: cc [CHAR] B EMIT ; cc CR            \ B
CREATE cbuf 64 ALLOT
S" counted" cbuf PLACE
cbuf COUNT TYPE CR                    \ counted
cbuf C@ . CR                          \ 7
CREATE mb 16 ALLOT
S" abcdef" mb SWAP MOVE
mb 6 TYPE CR                          \ abcdef
mb 3 CHAR X FILL
mb 6 TYPE CR                          \ XXXdef
mb 6 BLANK 60 EMIT mb 6 TYPE 62 EMIT CR
: sp 3 SPACES 42 . ; sp CR            \ 42

CR

CR

\ ---- tooling ----
: seeif DUP 0< IF NEGATE THEN ;
' seeif SEE                  \ : seeif DUP 0< IF NEGATE THEN ;
: seeloop 0 10 0 DO I + LOOP ;
' seeloop SEE                \ : seeloop 0 10 0 DO I + LOOP ;
: jt DUP * ;
JIT jt
' jt JIT? . CR               \ -1
' DUP JIT? . CR              \ 0
S" ABC" DROP 3 DUMP          \ 41 42 43
S" DU" WORDS-MATCH           \ DUMP DUP ...

CR
