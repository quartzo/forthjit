# tiny-forth

A small, self-contained Forth interpreter written in C. It uses classic
**indirect threaded code**: a colon definition compiles to a flat array of
*execution tokens* (XTs), and one virtual-machine loop walks that array,
threading calls through the return stack. No C recursion is used for nested
colon calls.

The whole system is a single file, `forth.c`, built with gcc on Linux.

> **Full manual:** see [`docs/manual.md`](docs/manual.md) for a complete guide
> to the Forth and to the low-level IR used by `CODE` definitions.

## Build and run

```sh
make            # -> ./forth
./forth         # interactive REPL (prompt "> ")
./forth test.fs # run a script, then drop into the REPL
make test       # run test.fs and test-native.fs
make asan       # build with AddressSanitizer + UBSan
```

SLJIT is vendored under `third_party/sljit_src` and pinned in
`third_party/sljit_VERSION`; the build compiles only `sljitLir.c`.

A native prelude (`prelude.fs`) is zlib-compressed at build time into
`prelude_blob.h` and loaded at startup. Set `FORTH_NO_PRELUDE=1` to skip it.

Inside the REPL, tokens are read one line at a time and evaluated. Every line
is evaluated under an `ABORT` guard: an error aborts the current line, resets
the data and return stacks, and returns to the prompt instead of killing the
process.

```
> 1 2 + . CR
3
```

## Syntax basics

- A *word* is a whitespace-delimited token.
- Names are **case-sensitive**: `CR` is defined, `cr` is not.
- Numbers push themselves onto the data stack. Accepted literal forms:
  - decimal: `42`, `-7`
  - hex: `$FF`, `0xFF`
  - binary: `%1010`
  - forced decimal: `#42`
- `\` starts a line comment; `( ... )` is an inline comment (must be closed).
  Both are also ignored inside a `CODE ... ;CODE` body.
- `PARSE-NAME ( -- c-addr u )` reads the next token from the input.
- Conditional compilation: `<flag> [IF] ... [ELSE] ... [THEN]`, spans multiple
  input lines and nests.
- Stack-effect comments below use `( before -- after )`.

## The data stack

Forth is a stack language. Words consume and produce values on a data stack of
signed machine cells (`long`).

| Word    | Effect            | Description                                    |
|---------|-------------------|------------------------------------------------|
| `DUP`   | `( x -- x x )`    | duplicate top                                   |
| `DROP`  | `( x -- )`        | remove top                                      |
| `SWAP`  | `( a b -- b a )`  | exchange top two                                |
| `OVER`  | `( a b -- a b a )`| copy second to top                              |
| `ROT`   | `( a b c -- b c a )`| rotate third to top                           |
| `NIP`   | `( a b -- b )`    | drop second                                     |
| `TUCK`  | `( a b -- b a b )`| copy top below second                           |
| `?DUP`  | `( x -- x x \| x )`| duplicate only if non-zero                    |
| `DEPTH` | `( -- n )`        | number of items on the stack                    |

## Arithmetic and logic

| Word     | Effect          | Description                                  |
|----------|-----------------|----------------------------------------------|
| `+` `-` `*` | `( a b -- n )` | addition, subtraction, multiplication      |
| `/` `MOD` | `( a b -- n )`  | division / remainder (C semantics, truncated toward zero) |
| `NEGATE` | `( n -- -n )`   | sign change                                    |
| `ABS`    | `( n -- \|n\| )`| absolute value                                 |
| `1+` `1-`| `( n -- n±1 )`  | increment / decrement                          |
| `MIN` `MAX` | `( a b -- n )` | smaller / larger                            |
| `AND` `OR` `XOR` | `( a b -- n )` | bitwise operations                      |
| `INVERT` | `( n -- ~n )`   | bitwise complement                             |
| `LSHIFT` `RSHIFT` | `( a u -- n )` | logical shifts by `u` bits             |

Division by zero raises `div by zero` and aborts the line.

## Comparison

Comparisons return `-1` (true) or `0` (false).

| Word   | Effect          | Description              |
|--------|-----------------|--------------------------|
| `=` `<>` | `( a b -- f )`| equal / not equal        |
| `<` `>` `<=` `>=` | `( a b -- f )` | ordering      |
| `0=`   | `( n -- f )`    | n is zero                |
| `0<` `0>` | `( n -- f )`  | n is negative / positive |

## Output

| Word   | Effect    | Description                                            |
|--------|-----------|--------------------------------------------------------|
| `.`    | `( n -- )`| print top as signed decimal followed by a space        |
| `.S`   | `( -- )`  | print the whole stack, non-destructively: `<depth> a b ...` |
| `EMIT` | `( c -- )`| print one character                                    |
| `CR`   | `( -- )`  | print a newline                                        |

## Strings and byte memory

`S" ..."` leaves `( c-addr u )`; in interpret state it returns one of eight
rotating buffers, while in compile state the bytes are copied to the data space
and pushed at run time. `." ..."` is compile-time output. `TYPE ( c-addr u -- )`
prints a counted range.

| Word | Effect | Description |
|------|--------|-------------|
| `MOVE ( src dst u -- )` | memmove |
| `CMOVE ( src dst u -- )` / `CMOVE>` | forward / backward byte copy |
| `FILL ( c-addr u ch -- )` / `BLANK ( c-addr u -- )` | fill with a byte / spaces |
| `COMPARE ( a1 u1 a2 u2 -- n )` | `-1`/`0`/`1` lexicographic |
| `SEARCH ( a1 u1 a2 u2 -- a3 u3 flag )` | substring search |
| `-TRAILING ( a u -- a u' )` | strip trailing spaces |
| `/STRING ( a u n -- a+n u-n )` | advance a string |
| `>NUMBER ( ud a u -- ud' a' u' )` | parse decimal digits into a double |
| `COUNT ( c-addr1 -- c-addr2 u )` | counted string to address/length |
| `PLACE ( c-addr1 u c-addr2 -- )` | store a counted string |
| `C@` / `C!` | byte fetch / store |
| `SPACES ( n -- )` | print `n` spaces |
| `CHAR c` / `[CHAR] c` | character literal (interpret / compile) |

```
S" hello world" S" wor" SEARCH . TYPE CR   \ -1 world
S" abcdef" 2 /STRING TYPE CR               \ cdef
: greet S" hi!" TYPE ; greet CR            \ hi!
CREATE buf 64 ALLOT
S" counted" buf PLACE
buf COUNT TYPE CR                          \ counted
```

## Defining words: `:` and `;`

```
: name ... ;
```

`:` begins a definition. It reads the next token as the name and enters
compile mode; tokens are compiled into a threaded body instead of being
executed. `;` compiles an `EXIT` and ends the definition. The name being
defined is hidden from the dictionary until `;`, so `RECURSE` is used for
self-reference.

```
: square DUP * ;
5 square . CR        \ prints 25

: fact DUP 1 <= IF DROP 1 ELSE DUP 1- RECURSE * THEN ;
6 fact . CR          \ prints 720
```

`IMMEDIATE` marks the most recently defined word as immediate, so it executes
during compilation rather than being compiled. All control-flow words below
are immediate.

## Control flow

**Conditional** (compile-time words):

```
flag IF ... ELSE ... THEN
```

- `IF` compiles a conditional branch (skip when the flag is `0`).
- `ELSE` compiles an unconditional branch and resolves the `IF`.
- `THEN` resolves the pending branch.

```
: sign DUP 0< IF DROP -1 ELSE 0> IF 1 ELSE 0 THEN THEN ;
```

**Indefinite loops**:

```
BEGIN ... UNTIL      \ loop until flag is true (non-zero)
BEGIN ... AGAIN      \ endless loop
BEGIN ... WHILE ... REPEAT
```

```
: countdown BEGIN DUP . 1- DUP 0= UNTIL DROP ;
: halves BEGIN DUP 1 > WHILE DUP . 2 / REPEAT DROP ;
```

**Counted loops**:

```
limit start DO ... LOOP        \ index runs start .. limit-1
limit start DO ... n +LOOP     \ step by n
```

Inside a loop, `I` pushes the current index and `J` the index of the enclosing
loop. Loop bounds and indices live on the return stack and are removed
automatically when the loop ends.

```
: sumto 0 SWAP 1+ 1 DO I + LOOP ;
: evens 10 0 DO I . 2 +LOOP ;    \ 0 2 4 6 8
```

`EXIT` returns early from the current colon definition.

### Compiler kit

The low-level parts of the compiler are exposed so control flow can be (and
now is) written in Forth:

| Word | Effect |
|------|--------|
| `HERE` | `( -- n )` current code-space index |
| `COMPILE,` | `( xt -- )` append an execution token to the current definition |
| `CODE,` `CODE!` `CODE@` | emit/patch/read a raw code cell |
| `LITERAL` | `( x -- )` immediate: compile `x` as a literal |
| `STATE` | `( -- addr )` address of the compilation-state cell |
| `[` `]` | immediate: suspend/resume compilation |
| `PARSE-NAME` | `( -- c-addr u )` read the next input token |
| `POSTPONE` | immediate: compile the compilation semantics of the next word |
| `[COMPILE]` | immediate: compile an immediate word instead of running it |
| `:NONAME` | `( -- xt )` start an anonymous definition |

With these, `IF/ELSE/THEN`, `BEGIN/UNTIL/AGAIN` and `BEGIN/WHILE/REPEAT` are
plain immediate colon definitions in `prelude.fs` (`PATCH` compiles the branch
offsets), not C words. `DO/LOOP`, `?DO`, `LEAVE` and `CASE` are still C.

### Defining words: `CREATE` and `DOES>`

`CREATE name` makes a word that pushes the address of its body (which grows
with `,`/`ALLOT`). `DOES>` (immediate) changes that word so it runs the code
that follows in the defining word:

```
: mk CREATE , DOES> @ ;
123 mk n123
n123 . CR            \ 123

: mk2 CREATE , DOES> @ 2 * ;
5 mk2 d5
d5 . CR              \ 10
```

`VARIABLE` and `CONSTANT` are themselves Forth definitions in the prelude now:
`: VARIABLE CREATE 1 CELLS ALLOT ;` and `: CONSTANT CREATE , DOES> @ ;`.
`:`/`;` remain in C (defining them in Forth would be circular).

### Deferred words

`DEFER name` creates a word whose action can be set later with `IS`, and read
with `ACTION-OF`:

```
DEFER greet
: hello ." hi" ;
' hello IS greet
greet CR            \ hi
ACTION-OF greet .   \ the xt of hello
```

`:NONAME ... ;` compiles a definition and leaves its execution token on the
stack (for `EXECUTE` or to store):

```
:NONAME 7 ; EXECUTE . CR    \ 7
```

### Synonyms, markers and forget

`SYNONYM new old` (also `ALIAS`) makes `new` behave like `old`. `MARKER name`
creates a word that, when executed, removes `name` and every word defined
after it (restoring the dictionary, data and code pointers). `FORGET name`
does the same immediately (dictionary only; it does not reclaim data/code).

```
: plus2 + ;
SYNONYM add2 plus2
1 2 add2 . CR        \ 3

MARKER mm
: temp 99 ;
mm
temp                 \ ? temp
```

### Local variables

Inside a definition, `{ a b -- c }` declares locals. Names before `--` are
inputs popped from the data stack (the last name receives the top); names after
`--` are extra slots initialized to `0`. `TO name` stores into a local. Every
call gets its own frame (saved on the return stack), so locals are
recursion-safe. Local names shadow dictionary words for the rest of the
definition.

```
: ladd { a b -- c } a b + ;
3 4 ladd . CR            \ 7
: lsum { n -- r } 0 n 0 ?DO I + LOOP ;
10 lsum . CR             \ 45
: lfact { n -- r } n 1 <= IF 1 ELSE n 1- RECURSE n * THEN ;
6 lfact . CR             \ 720
: lstore { a b -- c } a b + TO c c ;
10 20 lstore . CR        \ 30
```

Up to 16 locals per definition. Early `EXIT` still tears the frame down.

## Memory, variables and constants

There is a flat, cell-addressed data space (`mem[]`). Addresses are raw cell
pointers.

| Word         | Effect            | Description                                  |
|--------------|-------------------|----------------------------------------------|
| `!`          | `( x addr -- )`   | store `x` at `addr`                          |
| `@`          | `( addr -- x )`   | fetch from `addr`                            |
| `,`          | `( x -- )`        | append `x` to the data space                 |
| `ALLOT`      | `( n -- )`        | reserve `n` cells in the data space          |
| `VARIABLE`   | `( -- )`          | `VARIABLE name`; defines `name` to push its address |
| `CONSTANT`   | `( x -- )`        | `x CONSTANT name`; defines constant `name`   |

```
VARIABLE counter
0 counter !
counter @ 1+ counter !
counter @ . CR        \ 1

42 CONSTANT answer
answer . CR           \ 42
```

## Error handling and exceptions

Errors do not terminate the process. Each input line runs inside a `setjmp`
frame; an uncaught error aborts the line, resets the interpreter and prints the
message. Errors are also catchable with the ANS-style exception words:

| Word | Effect |
|------|--------|
| `CATCH` | `( i*x xt -- j*x 0 \| i*x n )` run the execution token |
| `THROW` | `( n -- )` raise code `n` (0 is a no-op) |
| `ABORT` | `-1 THROW` |
| `ABORT"` | `( flag -- )` immediate, aborts with an inline message when flag is true |
| `ERROR-CODE` | `( -- n )` |
| `ERROR-MSG` | `( -- c-addr u )` |
| `EXECUTE` | `( xt -- )` run an execution token |
| `'` | `( -- xt )` parse a name, return its execution token |
| `[']` | immediate: compile an execution token |
| `CODE-ADDR` | `( xt -- c-addr )` C code pointer (for native `icall`) |

```
: boom 42 THROW ;
' boom CATCH . CR          \ 42

1 0 ' / CATCH . CR         \ -10  (division by zero)
1 0 ' / CATCH DROP ERROR-CODE . CR
```

Error codes: `-1` ABORT, `-2` ABORT", `-3` stack overflow, `-4` stack
underflow, `-5` return-stack error, `-10` division by zero, `-13` unknown word,
`-256` otherwise. Every internal error is catchable.

Native code can raise too: `icall &forth_raise` after `sig V W`.

## Implementation notes

- `forth.c` keeps the data stack, return stack, a dedicated compile-time
  control-flow stack, the threaded `code[]` array, and the dictionary.
- Internal runtime words are named `(exit)`, `(lit)`, `(branch)`,
  `(0branch)`, `(do)`, `(loop)`, `(+loop)`. They are the primitive targets
  emitted by the compiler and can technically be called directly.
- `EXIT` and the internal `(exit)` share the same code pointer; `EXIT` is the
  user-facing spelling.
- Instruction pointer and branch offsets are indices into `code[]`, so branch
  patching is plain integer arithmetic.
- Native words are assembled by a vendored copy of [SLJIT](https://github.com/zherczeg/sljit)
  (`third_party/sljit_src`, pinned in `third_party/sljit_VERSION`). Only
  `sljitLir.c` is compiled.

## Native code: the SLJIT LIR assembler

A second kind of definition, `CODE ... ;CODE`, assembles machine code at
compile time through SLJIT. The body is written in a small low-level IR, not
in Forth. The generated function follows the Forth calling convention
described below, so a native word is indistinguishable from any other word
when used at the prompt or inside `:` definitions. Comments (`\` to end of
line and `( ... )`) are ignored inside a `CODE` body.

```
CODE sq
  mov R0, [S1-8]
  mul R0, R0, R0
  mov [S1-8], R0
;CODE
5 sq . CR            \ 25
```

### Calling convention

`CODE` injects a prologue and epilogue:

- argument `S0` is a pointer just past the top of the data stack;
- the prologue copies it to `S1`, the working stack pointer;
- `TOS = [S1-8]`; pushing writes `[S1]` and does `S1 += 8`;
- the epilogue returns the new `S1` in `R0` (`ret S1`).

`R0..R3` and `S0..S1` are available. `locals N` as the first line (before any
instruction) reserves `N` bytes of local stack, addressed as `[SP+off]`.

### Operands

| Form | Meaning |
|------|---------|
| `R0`..`R9`, `S0`..`S9`, `SP` | integer registers |
| `F0`..`F9`, `FS0`..`FS9` | float registers |
| `V0`..`V9`, `VS0`..`VS9` | vector registers |
| `#123`, `#0x1f` | immediate |
| `[R1]`, `[R1+8]`, `[S0-16]` | base + offset |
| `[R1+R2<<3]` | indexed (shift 0..3) |
| `[#123]` | absolute address |
| `&name`, `&lib::name` | resolved symbol / native word |

### Instructions

```
mov dst, src                        (also mov.p, mov.u8/s8/u16/s16/u32/s32, mov32)
add|sub|mul|and|or|xor|shl|lshr|ashr|rotl|rotr dst, a, b   (suffix 32: add32, ...)
neg dst, src        not dst, src
clz|ctz|rev dst, src
cmp <cond> a, b, @label             <cond> = eq ne lt le gt ge ult ule ugt uge zero nz
setflags <cond> a, b                set flags for a following `flags`
flags <cond> dst                    dst = 0/1 from the last flags
select <cond> dst, a, b             conditional move
jmp @label
ret [src]                           ret S1 returns the stack pointer
nop       int3
```

### Expanded SLJIT surface

Generic bindings for the rest of the API (opcodes are numbers or `SLJIT_*`):

```
op0 <op>                 op1 <op> dst, src        op2 <op> dst, a, b
op2u <op> a, b           op2r <op> dst, a, b      op2shift <op> dst, a, b, sh
op2cmpz <op> dst, a, b, @label
ijump <op> addr          opsrc <op> addr          opdst <op> addr
ret_to addr              opaddr <op> dst, @label  aligned_label <align> NAME
custom <n> <byte>...     jump <op> @label|#addr   setlabel NAME

fop1 <op> fd, fs         fop2 <op> fd, fa, fb     fop2r <op> fd, fa, fb
fcmp <cond> fa, fb, @label                        (<cond> = feq fne flt fle fgt fge)
fselect <cond> fd, fa, fb
fcopy <op> fd, reg       fset32 fd, <float>       fset64 fd, <double>
fmem <type> fd, [mem]    fmem_update <type> fd, [mem]
```

Runtime patching and introspection:

```
rwjump NAME <op> @label     a rewritable jump
rwconst NAME <op> dst, #v   a rewritable constant
const <op> dst, #v          a plain rewritable constant
SET-JUMP-ADDR ( c-addr u target -- )   patch a rwjump by name
SET-CONST     ( c-addr u value -- )    patch a rwconst by name
CPU-FEATURE?  ( n -- flag )            sljit_has_cpu_feature
JIT-SIZE      ( -- n )                 size of the last generated code
```

All `SLJIT_*` constants are also Forth words that push their value.

### Raw emission (`asm.*`, per architecture)

For instructions SLJIT does not expose, raw bytes can be emitted directly.
On x86-64 the following mnemonics encode instructions using
`sljit_get_register_index`, so they accept either SLJIT registers
(`R0`..`R9`, `S0`..`S9`, `SP`) or physical names (`RAX`..`R15`, `RDI`, ...):

```
asm.mov <dst> <src>        mov r64, r64
asm.mov.imm <dst> <v>      mov r64, imm64      (v = #imm | &sym | number)
asm.call <reg>             call r64
asm.push <reg> / asm.pop <reg>
asm.mov.al <imm8>          mov al, imm8
asm.ret / asm.nop / asm.int3
db / dw / dd / dq <v>      emit 1/2/4/8 data bytes (little-endian)
```

`ARCH` pushes the target code (`1` = x86-64). The `asm.*` mnemonics are only
defined for the matching architecture.

This also makes variadic C calls correct. `sljit_emit_icall` cannot set `AL`
(it moves arguments internally, and `SLJIT_R0` is `RAX` on x86-64), so place
the arguments in ABI registers yourself and set `AL` last:

```
CODE printf1 ( fmt value -- )
  mov R3, &printf        \ function address in RCX
  mov R2, [S1-16]        \ format  -> RDI
  mov R1, [S1-8]         \ value   -> RSI
  add S1, S1, #-16
  asm.mov.al 0           \ AL = 0 vector registers
  asm.call R3
  mov R0, S1
;CODE
S" value=%ld" DROP 42 printf1 CR   \ value=42
```

Raw emission inserts bytes into SLJIT's stream; it is an escape hatch, not a
replacement for an assembler (SLJIT still owns layout and jump resolution).

`custom <n> <byte>...` emits arbitrary bytes if no mnemonic fits.

### Inline (macro expansion)

`inline <name>` splices the stored IR body of another `CODE` word into the
current word, with **no call**. Labels in the inlined body are renamed per
expansion, so a word can be inlined several times.

```
CODE 2DUP
  inline OVER
  inline OVER
;CODE
```

Use `inline` for small words (removes call indirection, duplicates code) and
`callw <name>` for larger ones (a real indirect call).

### IR as a string

`S" ..."` parses a string literal and returns `( addr len )`. `ASSEMBLE`
compiles it (raw, no injected prologue or epilogue) and returns the code
pointer. `NATIVE` turns that pointer into a named word:

```
S" enter P 4 2 0 P mov S1 S0 mov R0 [S1-8] add R0 R0 #100 mov [S1-8] R0 ret S1"
   ASSEMBLE NATIVE plus100
7 plus100 . CR        \ 107
```

Inside `CODE`, `IR" ..."` embeds an IR fragment. `'` (tick) pushes a word's
execution token; `CODE-ADDR` converts it to the C code pointer for `icall`
(e.g. `C-PUTCHAR` pushes the address of libc `putchar`).

### Native prelude

A growing part of the language is written in `prelude.fs` as `CODE` words and
assembled to machine code at boot. The C kernel keeps only what cannot easily
live in IR: the VM and compiler words, memory access, I/O, the dictionary, the
`CODE` assembler and the dynamic linker.

Native words call each other with `callw <name>`, which requires the operand
ABI used by `CODE`: the stack pointer is passed in `R0` (scratch argument),
which matches the C call convention. For example:

```
CODE OVER
  mov R0, [S1-16]
  mov [S1], R0
  add S1, S1, #8
  mov R0, S1
;CODE

CODE 2DUP
  callw OVER
  callw OVER
;CODE
```

`&name` resolves a native word first, then a shared-library symbol, so native
code can reference either.

The current prelude provides:

- stack: `DUP DROP SWAP OVER NIP TUCK ROT 2DUP 2DROP`
- arithmetic/logic: `+ - * / MOD NEGATE ABS 1+ 1- MIN MAX AND OR XOR INVERT LSHIFT RSHIFT`
- comparisons: `= <> < > <= >= 0= 0< 0>`
- return stack: `>R R> R@ 2>R 2R>`
- loop indices: `I J`
- misc: `?DUP @ ! PICK DEPTH , ALLOT CELLS +! SP@ SP! RP@ EXIT`
- output: `EMIT CR SPACE . .S TYPE`
- strings: `." ..."` (compile-time string output)

`PICK` is native; `.` and `.S` are plain threaded `:` definitions that use
`/ MOD EMIT` and a `VARIABLE` buffer (number formatting in Forth, no C).
`DEPTH`, `,`, `ALLOT` and `I`/`J` read the interpreter state through the
internal symbols `&dstack`, `&mem`, `&memtop`, `&rp` and `&rstack`, which the
IR resolves directly. `/`/`MOD` raise on division by zero through
`icall &forth_divzero`.

Their C versions were removed; the C kernel keeps `EXIT`, the dictionary
words, the compiler, the `CODE` assembler and the dynamic linker. A prelude
error aborts startup with a fatal message.

Native `CODE` words guard their stack arity: each starts with `require <n>`
(raises the catchable `-4`, stack underflow, via `&forth_need`) and pushing
words add `room <n>` (`&forth_room`, `-3`, stack overflow). `PICK` also bounds
its index dynamically.

The internal-symbol operands available to native code are `&dstack`,
`&rstack`, `&mem`, `&sp`, `&rp`, `&memtop`, `&forth_need`, `&forth_room`,
`&forth_type`, `&forth_raise` and `&forth_divzero`.

### Dictionary and reflection

The dictionary is reachable from both Forth and native `CODE`:

| Word | Effect |
|------|--------|
| `FIND` | `( c-addr u -- xt\|0 )` |
| `LATEST` | `( -- xt )` |
| `>LINK` | `( xt -- xt\|0 )` |
| `>NAME` | `( xt -- c-addr u )` |
| `WORD-DATA`, `>BODY` | `( xt -- n )`, `( xt -- addr )` |
| `IMMEDIATE? HIDDEN? COLON? NATIVE? VARIABLE?` | `( xt -- f )` |
| `WORDS` | list visible names |
| `WORDS-MATCH` | `( c-addr u -- )` list names with a prefix |
| `SEE` | `( xt -- )` print a definition from its logged source |
| `NATIVE>`, `JIT?` | `( xt -- addr )`, `( xt -- f )` |
| `F_IMMEDIATE`, `F_HIDDEN` | flag bits (1, 2) |

`FIND`, `>LINK`, `>NAME`, `WORD-DATA`, `>BODY` and the predicates are `CODE`
words in the prelude that call small C helpers; `WORDS` and the compile-time
source log used by `SEE` stay in C (they are dominated by the interpreter's
internal state), while `WORDS-MATCH`, `DUMP` and `JIT?` are prelude words. The
`struct Word` layout stays in C.

Native code can call the same helpers directly through `&` symbols
(`&forth_find`, `&forth_latest`, `&forth_name`, `&forth_link`,
`&forth_flags`, `&forth_code`, `&forth_native`, `&forth_data`,
`&forth_body`, `&forth_body_start`, `&forth_body_end`, plus the predicates).
For example:

```
CODE nfind
  mov R0, [S1-16]
  mov R1, [S1-8]
  sig P P 32
  icall &forth_find
  mov [S1-16], R0
  add S1, S1, #-8
  mov R0, S1
;CODE
```

The interpreter/compiler state is also exposed to native code:
`&code`, `&here`, `&ip`, `&state`, `&compiling`, `&latest`, `&nwords`, `&dict`.

### Dynamic linking

Symbols from shared libraries are resolved with `dlopen`/`dlsym` and called
directly by native code. Resolution is **eager**: it happens while the `CODE`
is assembled, and the address is embedded as an immediate.

An `&name` operand (or `&library::name`) resolves through the interpreter's
library registry and is usable anywhere an operand is expected:

```
CODE slen
  mov R0, [S1-16]
  sig W P
  icall &strlen            \ libc symbol, global scope
  add S1, S1, #-8
  mov [S1-8], R0
;CODE
S" hello world" slen . CR  \ 11

CODE sqrt-addr
  op1 SLJIT_MOV R0, &libm.so.6::sqrt   \ opens libm on demand
  ...
;CODE
```

IR directives `dlopen <path>` and `dlclose <path>` preload/unload libraries.
Because `dlopen`/`dlsym` are themselves ordinary symbols, native code can also
call `&dlopen` / `&dlsym` to link at runtime.

The same registry is available at the prompt:

| Word | Effect |
|------|--------|
| `DLOPEN`  | `( c-addr u -- handle )` |
| `DLSYM`   | `( handle c-addr u -- addr )`, handle `0` = global scope |
| `DLCLOSE` | `( handle -- )` |
| `DLLIBS`  | `( -- )` list open libraries |
| `DLERROR` | `( -- c-addr u )` last loader message |

Resolved symbols are cached; libraries stay open until `DLCLOSE` or process
exit. Closing a library whose symbols were already embedded in generated code
leaves those call sites dangling, so `DLCLOSE` should be used with care.

Variadic C functions (e.g. `printf`) can be called through `asm.*`: place the
arguments in the ABI registers yourself, set `AL` last, then `asm.call` (see
the raw-emission section).

## JIT (call-threaded)

`JIT name` translates a colon definition's threaded body into native code, and
`JIT-ALL` does it for every colon word that can be compiled (it returns how
many were compiled).

```
: sq DUP * ;
JIT sq
5 sq . CR        \ 25
```

The generated function keeps the **stack pointer in a register** (the same
register ABI as prelude words), calls prelude/native words directly through
that ABI, inlines literals and `(0branch)`, and uses native jumps for
`IF`/`BEGIN`. C primitives (which use the global stack) are surrounded by a
sync. It is installed as the word's `native` pointer with `code = p_native`,
so it is indistinguishable from a native word.

This removes the VM dispatch loop and the per-call global-stack sync; a
`BEGIN/UNTIL` loop ran ~2x faster than the threaded version. Bodies
containing `DO`/`LOOP`/`+LOOP` are left to the VM, and threaded colon words
called from JIT code still go through the interpreter. The generated code
preserves the caller's instruction pointer, so JIT and threaded words call
each other freely.

Next step (not done): inline the hot primitive bodies (skipping their
`require`/`room` guards), which would remove the remaining call per operation.

## Tooling

`SEE ( xt -- )` prints a definition by replaying the source text logged while it
was compiled, so control flow and locals appear exactly as written (colon words
only; `VARIABLE`, `CONSTANT`, `CODE ... ;CODE` and primitives are summarised):

```
: fact DUP 1 <= IF DROP 1 ELSE DUP 1- RECURSE * THEN ;
' fact SEE      \ : fact DUP 1 <= IF DROP 1 ELSE DUP 1- RECURSE * THEN ;
```

Other introspection words:

| Word | Effect | Description |
|------|--------|-------------|
| `WORDS-MATCH ( c-addr u -- )` | list words whose name starts with the prefix |
| `DUMP ( c-addr u -- )` | hex dump, 16 bytes per line |
| `JIT? ( xt -- flag )` | true when a colon word has been JIT-compiled |
| `NATIVE> ( xt -- a-addr )` | native code pointer (0 if none) |
| `JIT-SIZE`, `CPU-FEATURE?` | size of the last generated block / SLJIT feature query |

## Coverage: what a Forth is expected to have

Legend: **Yes** = implemented, **Partial** = limited/subset, **No** = absent.

### Core language

| Capability                              | Status  | Notes |
|-----------------------------------------|---------|-------|
| Data stack manipulation (DUP DROP SWAP OVER ROT NIP TUCK ?DUP DEPTH) | Yes | |
| `PICK`, `ROLL`                          | No      | not implemented |
| Return-stack words `>R R> R@ 2>R 2R>`   | Yes     | shared with the VM return stack |
| Arithmetic (+ - * / MOD NEGATE ABS 1+ 1-) | Yes   | |
| `*/`, `*/MOD`, `FM/MOD`, `SM/REM`       | No      | |
| Comparison (`= <> < > <= >= 0= 0< 0>`)  | Yes     | |
| Bitwise (`AND OR XOR INVERT LSHIFT RSHIFT`) | Yes | |
| Number bases (`BASE`, `HEX`, `DECIMAL`) | Partial | only literal prefixes `$`, `0x`, `%`, `#`; no `BASE` variable |
| Double-cell and floating-point words    | No      | single-cell integers only |

### Defining and compiling

| Capability                              | Status  | Notes |
|-----------------------------------------|---------|-------|
| `:` … `;` colon definitions             | Yes     | |
| `RECURSE`                               | Yes     | |
| `IMMEDIATE`                             | Yes     | |
| Compile-mode `[` / `]`, `LITERAL`       | Yes     | `IF/THEN/BEGIN...` are Forth over the compiler kit |
| `HERE`, `COMPILE,`, `CODE,`/`CODE!`/`CODE@`, `STATE` | Yes | compiler kit |
| `[IF]` `[ELSE]` `[THEN]`                | Yes     | multi-line and nested |
| `PARSE-NAME`                            | Yes     | reads the next input token |
| `CREATE` / `DOES>`                      | Yes     | `VARIABLE`/`CONSTANT` are Forth over them |
| `POSTPONE`, `[COMPILE]`                 | Yes     | |
| `:NONAME`                               | Yes     | leaves an xt on the stack |
| `DEFER`, `IS`, `ACTION-OF`              | Yes     | deferred words |
| `SYNONYM` / `ALIAS`, `MARKER`, `FORGET` | Yes     | |
| `{ a b -- c }` locals, `TO`            | Yes     | per-invocation frame; recursion-safe |
| `EXECUTE`, `'` (tick), `[']`            | Yes     | execution tokens (`Word*`); `CODE-ADDR` gives the C pointer |
| `FIND`, `>BODY`, `>NAME`, `>LINK`, `LATEST` | Yes | dictionary reflection (prelude over C helpers) |
| `WORDS`, `SEE`, `WORDS-MATCH`, `DUMP`, `JIT?` | Yes | `SEE` replays logged source |
| User-defined defining words             | No      | |

### Control flow

| Capability                              | Status  | Notes |
|-----------------------------------------|---------|-------|
| `IF ELSE THEN`                          | Yes     | |
| `BEGIN UNTIL`, `BEGIN AGAIN`            | Yes     | |
| `BEGIN WHILE REPEAT`                    | Yes     | one `WHILE` per `BEGIN` |
| `DO LOOP`, `?DO`, `+LOOP`, `I`, `J`, `LEAVE`, `UNLOOP` | Yes | |
| `CASE OF ENDOF ENDCASE`                 | Yes     | |

### Memory and data

| Capability                              | Status  | Notes |
|-----------------------------------------|---------|-------|
| `!` `@` `,` `ALLOT`                     | Yes     | |
| `VARIABLE` `CONSTANT`                   | Yes     | |
| `C@ C! +! COUNT PLACE`                  | Yes     | |
| `2@ 2!`                                 | No      | |
| `HERE`, `SP@`, `SP!`, `RP@`             | Yes     | `HERE` returns the code index |
| `PAD`, `RP!`                            | No      | |

### I/O

| Capability                              | Status  | Notes |
|-----------------------------------------|---------|-------|
| `.` `.S` `EMIT` `CR`                    | Yes     | |
| `TYPE`, `SPACE`, `SPACES`, `."`        | Yes     | |
| `KEY`, `ACCEPT`, `WORD`, `FIND`         | No      | input is line-based only |
| String words `S"`, `MOVE`, `FILL`, `COMPARE`, `SEARCH`, `-TRAILING`, `/STRING`, `>NUMBER`, `CMOVE` | Yes | `S"` works in interpret and compile state |
| `CHAR`, `[CHAR]`                        | Yes     | |
| File and block I/O                      | No      | |

### Error handling

| Capability                              | Status  | Notes |
|-----------------------------------------|---------|-------|
| Automatic abort + stack reset on error  | Yes     | via `setjmp`/`longjmp` |
| `ABORT`, `ABORT"` as callable words     | No      | behavior exists, no user word |
| `CATCH` / `THROW` / `ABORT` / `ABORT"`  | Yes     | ANS exceptions; every internal error is catchable |
| `ERROR-CODE` / `ERROR-MSG`              | Yes     | code + message side channel |

### Standards

| Capability                              | Status  | Notes |
|-----------------------------------------|---------|-------|
| ANS/ISO Forth core compliance           | No      | ad-hoc subset, not conformance-tested |
| Case-insensitive dictionary lookup      | No      | names are case-sensitive |
| Outer interpreter with `WORD`/number parsing | Partial | line/token based; number parsing for decimal/hex/binary |

### Native code / JIT (SLJIT)

| Capability                              | Status  | Notes |
|-----------------------------------------|---------|-------|
| `CODE ... ;CODE` native words        | Yes     | Forth calling convention, `locals`, labels |
| Native prelude (`prelude.fs`, zlib)    | Yes     | `DUP DROP SWAP OVER NIP TUCK ROT 2DUP 2DROP + - *`; C versions removed |
| Native-to-native calls (`callw`)        | Yes     | scratch-arg ABI; defined words only, no forward refs |
| LIR: mov/arith/shifts/clz/ctz/rev       | Yes     | integer subset; `32`-suffix variants |
| Memory operands `[reg±off]`, `[r1+r2<<s]` | Yes   | |
| Labels, `jmp`, `cmp`+`@label`           | Yes     | forward references resolved at assembly |
| Generic escape (`op0/op1/op2/cmp/jump`) | Yes     | opcodes by number or `SLJIT_*` name |
| `enter`, `sig`, local stack             | Yes     | custom signatures / `[SP+off]` |
| Indirect calls `call`/`icall`/`icall.reg` | Yes   | address operand; direct label calls not exposed |
| IR as a string (`S"` + `ASSEMBLE` + `NATIVE`) | Yes | raw ABI for arbitrary functions |
| Dynamic linking `&name` / `&lib::name`  | Yes     | eager `dlopen`/`dlsym`, cached registry |
| `DLOPEN`/`DLSYM`/`DLCLOSE`/`DLLIBS`/`DLERROR` | Yes | shared with the IR resolver |
| Flags/select (`op2u`, `flags`, `select`, `setflags`) | Yes | materialize comparisons / cmov |
| Fast calls, prefetch, `op_src`/`op_dst` | Yes     | `ijump`, `opsrc`, `opdst`, `ret_to` |
| Abs addresses, aligned labels, raw bytes | Yes     | `opaddr`, `aligned_label`, `custom` |
| Floating point (`fop1/fop2/fcmp/fselect/fcopy/fmem`) | Yes | float operands `F*`, `fscratches`/`fsaveds` |
| Runtime patching (`rwjump`/`rwconst`, `SET-*`) | Yes | self-modifying code / inline caches |
| `inline` macro expansion                | Yes     | label renaming per expansion |
| JIT of colon definitions (`JIT`/`JIT-ALL`) | Yes  | call-threaded; no loops/inlining yet |
| SIMD / atomics                          | No      | SLJIT supports them; not bound here |
| Variadic C calls (e.g. `printf`)        | Yes     | via `asm.*`: place args in ABI regs, `asm.mov.al`, `asm.call` |
| Raw per-arch emission (`asm.*`, `db`..`dq`) | Yes  | x86-64; `ARCH` selects the target |
| Direct `sljit_emit_call` to a label     | No      | requires a separately compiled function context |
| Meta-JIT (`&sljit_*` from native code)  | No      | would need `-rdynamic` / explicit export |
