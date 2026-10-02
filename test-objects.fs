\ object store + mark-and-sweep GC smoke tests
REQUIRE objects.fs

NEWTYPE NODE
  1 FIELD next
  0 FIELD val
;TYPE

VARIABLE HEAD
HEAD ROOT
0 HEAD !

: ADD { n -- obj } NODE NEW TO obj  n obj 1 FIELD!  HEAD @ obj 0 FIELD!  obj HEAD ! ;
: BUILD 100 0 DO I ADD LOOP ;
: SUM 0 HEAD @ BEGIN DUP WHILE DUP 1 FIELD@ ROT + SWAP 0 FIELD@ REPEAT DROP ;

BUILD
S" chain sum = " TYPE SUM . CR            \ 4950
GC
S" after GC  = " TYPE SUM . CR            \ 4950 (rooted chain kept)
S" live = " TYPE TAB-N @ . CR             \ 100

\ unrooted objects are collected
: GARBAGE NODE NEW DROP ;
GARBAGE GARBAGE GARBAGE
S" live+3 = " TYPE TAB-N @ . CR           \ 103
GC
S" live after GC = " TYPE TAB-N @ . CR    \ 100

\ a cycle reachable from a root survives
VARIABLE A
A ROOT
0 A !
NODE NEW A !                              \ n1
NODE NEW DUP A @ 0 FIELD!                 \ n2.next = n1
A @ SWAP 0 FIELD!                         \ n1.next = n2  (cycle)
GC
S" cycle live = " TYPE TAB-N @ . CR       \ 102

\ named field access
: NAMED ( -- x )
  NODE NEW >R  42 R@ S" val" !FIELD  R@ S" val" @FIELD  R> DROP ;
S" named field = " TYPE NAMED . CR        \ 42

OBJECTS-FREE

