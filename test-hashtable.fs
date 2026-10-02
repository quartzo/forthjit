\ lib/hashtable.fs smoke tests: a generic Forth hash table over store arrays
REQUIRE hashtable.fs

: CHECK { f a u -- }
  f 0= IF S" FAIL: " TYPE a u TYPE CR -1 THROW THEN ;

VARIABLE H
8 SYSTEM HT-NEW H !

\ basic put / get / update / missing
111 1 H @ HT-PUT
222 2 H @ HT-PUT
1 H @ HT-GET 111 = S" get 1" CHECK
2 H @ HT-GET 222 = S" get 2" CHECK
3 H @ HT-GET 0 = S" missing" CHECK
333 1 H @ HT-PUT
1 H @ HT-GET 333 = S" update" CHECK
H @ HT-N 2 = S" count" CHECK

\ growth under many entries
: FILLHT 1000 1 DO I 10 * I H @ HT-PUT LOOP ;
FILLHT
H @ HT-N 999 = S" grown count" CHECK
: VERIFY { -- f } -1 TO f
  1000 1 DO I H @ HT-GET I 10 * <> IF 0 TO f LEAVE THEN LOOP f ;
VERIFY S" all entries survive growth" CHECK

\ an object value is kept alive by the rooted table, through the array
SYSTEM NEWTYPE NODE
  0 FIELD val
;TYPE
NODE SYSTEM NEW DUP 77 SWAP 0 FIELD!
4242 H @ HT-PUT
H SYSTEM ROOT
SYSTEM GC
4242 H @ HT-GET 0 <> S" object survives" CHECK
4242 H @ HT-GET 0 FIELD@ 77 = S" object intact" CHECK

SYSTEM STORE-FREE
S" hashtable ok" TYPE CR
