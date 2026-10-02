\ GC stress: mixed fixed/variable objects, repeated collection, integrity checks
REQUIRE objects.fs

SYSTEM NEWTYPE P
  1 FIELD a
  1 FIELD b
;TYPE

VARIABLE R
P SYSTEM NEW R !
R SYSTEM ROOT

\ rooted graph: P.a -> array(10); array[3] = 999
10 SYSTEM ARRAY-NEW R @ 0 FIELD!
999 R @ 0 FIELD@ 3 ARRAY!

: MIX { n -- } n 0 ?DO
    I 3 MOD 0= IF P SYSTEM NEW DROP
    ELSE I 9 MOD 6 + SYSTEM ARRAY-NEW DROP
    THEN
  LOOP ;

: CYCLE { n -- } n 0 ?DO
    200 MIX
    SYSTEM GC
    SYSTEM OBJECTS-VERIFY DROP
    SYSTEM S-DATA CELLS + @ ARENA-VERIFY DROP
    R @ 0 FIELD@ 3 ARRAY@ 999 <> IF -1 THROW THEN
  LOOP ;

200 CYCLE
S" gc-stress ok" TYPE CR

SYSTEM STORE-FREE
