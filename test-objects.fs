\ object store + mark-and-sweep GC smoke tests
REQUIRE objects.fs

VARIABLE ST
1 20 LSHIFT STORE-NEW ST !

ST @ NEWTYPE NODE
  1 FIELD next
  0 FIELD val
;TYPE

VARIABLE HEAD
HEAD ST @ ROOT
0 HEAD !

: ADD { n -- obj } NODE ST @ NEW TO obj  n obj 1 FIELD!  HEAD @ obj 0 FIELD!  obj HEAD ! ;
: BUILD 100 0 DO I ADD LOOP ;
: SUM 0 HEAD @ BEGIN DUP WHILE DUP 1 FIELD@ ROT + SWAP 0 FIELD@ REPEAT DROP ;

BUILD
S" chain sum = " TYPE SUM . CR            \ 4950
ST @ GC
S" after GC  = " TYPE SUM . CR            \ 4950 (rooted chain kept)
S" live = " TYPE ST @ OBJECTS-LIVE . CR   \ 100

\ unrooted objects are collected
: GARBAGE NODE ST @ NEW DROP ;
GARBAGE GARBAGE GARBAGE
S" live+3 = " TYPE ST @ OBJECTS-LIVE . CR \ 103
ST @ GC
S" live after GC = " TYPE ST @ OBJECTS-LIVE . CR    \ 100

\ a cycle reachable from a root survives
VARIABLE A
A ST @ ROOT
0 A !
NODE ST @ NEW A !                         \ n1
NODE ST @ NEW DUP A @ 0 FIELD!            \ n2.next = n1
A @ SWAP 0 FIELD!                         \ n1.next = n2  (cycle)
ST @ GC
S" cycle live = " TYPE ST @ OBJECTS-LIVE . CR       \ 102

\ named field access
: NAMED ( -- x )
  NODE ST @ NEW >R  42 R@ S" val" !FIELD  R@ S" val" @FIELD  R> DROP ;
S" named field = " TYPE NAMED . CR        \ 42

ST @ OBJECTS-STATS
ST @ STORE-FREE
SYSTEM STORE-FREE
