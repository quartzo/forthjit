\ lib/objects.fs arrays: variable-length GC-traced base elements
REQUIRE objects.fs

: CHECK { f a u -- }
  f 0= IF S" FAIL: " TYPE a u TYPE CR -1 THROW THEN ;

SYSTEM NEWTYPE NODE
  0 FIELD val
;TYPE

\ length, zero-fill and read/write
VARIABLE A
10 SYSTEM ARRAY-NEW A !
A @ ARRAY-LEN 10 = S" length" CHECK
A @ 0 ARRAY@ 0 = S" zero-filled" CHECK
12345 A @ 3 ARRAY!
A @ 3 ARRAY@ 12345 = S" read/write" CHECK

\ a slot holding an object keeps it alive across GC
A SYSTEM ROOT
NODE SYSTEM NEW DUP 77 SWAP 0 FIELD!
A @ 0 ARRAY!
SYSTEM GC
A @ 0 ARRAY@ 0 <> S" object slot survives" CHECK
A @ 0 ARRAY@ 0 FIELD@ 77 = S" object intact" CHECK

\ a slot holding a plain number does not confuse GC
VARIABLE C
5 SYSTEM ARRAY-NEW C !
C SYSTEM ROOT
999 C @ 1 ARRAY!
SYSTEM GC
C @ 1 ARRAY@ 999 = S" scalar slot" CHECK

\ unrooted arrays are collected
SYSTEM OBJECTS-LIVE CONSTANT LIVE0
: GARBAGE 20 SYSTEM ARRAY-NEW DROP ;
GARBAGE GARBAGE
SYSTEM OBJECTS-LIVE LIVE0 > S" arrays counted" CHECK
SYSTEM GC
SYSTEM OBJECTS-LIVE LIVE0 = S" unrooted arrays collected" CHECK

\ zero-length array is fine
0 SYSTEM ARRAY-NEW DROP

SYSTEM STORE-FREE
S" array ok" TYPE CR
