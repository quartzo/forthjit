\ code objects: copy generated code into a store's RWX code arena
REQUIRE objects.fs

: CHECK { f a u -- }
  f 0= IF S" FAIL: " TYPE a u TYPE CR -1 THROW THEN ;

SYSTEM S-CODE S@ 0 <> S" store has code arena" CHECK

\ assemble a Forth-ABI function that adds 100, copy it, free the source
VARIABLE SZ
S" locals 8 mov R0, [S1-1c] add R0, R0, #100 mov [S1-1c], R0"
ASSEMBLE-FORTH-DYN CONSTANT RAW
RAW JIT-SIZE SZ !
SZ @ 0 > S" size nonzero" CHECK
RAW SZ @ SYSTEM NEW-CODE CONSTANT C1
RAW (SLJIT-FREE)                     \ source block released; C1 owns a copy
5 C1 CODE-PTR CALLF1 105 = S" copied code runs" CHECK
C1 CODE-SIZE SZ @ = S" recorded size" CHECK

\ a rooted code object survives GC and still runs
VARIABLE C2
S" locals 8 mov R0, [S1-1c] add R0, R0, #7 mov [S1-1c], R0"
ASSEMBLE-FORTH-DYN CONSTANT RAW2
RAW2 JIT-SIZE SYSTEM NEW-CODE C2 !
RAW2 (SLJIT-FREE)
C2 SYSTEM ROOT
SYSTEM GC
C2 @ 0 <> S" code object survives gc" CHECK
3 C2 @ CODE-PTR CALLF1 10 = S" still runs after gc" CHECK

SYSTEM OBJECTS-FREE
SYSTEM STORE-FREE
S" code-obj ok" TYPE CR
