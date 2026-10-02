\ lib/hashtable.fs - a generic open-addressing hash table over lib/objects.fs
\ arrays.  Load with `REQUIRE hashtable.fs`.
\
\ A table is a store object holding an ARRAY of 2*cap cells (slot i = key at
\ 2i, value at 2i+1).  A slot is empty when the key cell is 0, so 0 is not a
\ valid key.  Keys and values are ordinary cells (object or number); GC marks
\ the array through the table's `slots` pointer field, and the array scans all
\ its cells (see objects.fs ARRAY).  `cap` must be a power of two.

REQUIRE objects.fs

SYSTEM NEWTYPE HASHTABLE
  1 FIELD ht-slots     \ ARRAY of 2*cap cells
  0 FIELD ht-n         \ live entries
  0 FIELD ht-cap       \ number of slots
  0 FIELD ht-store     \ owning store (scalar)
;TYPE

: HT-HASH { key cap -- i }
  key DUP 33 RSHIFT XOR
  2654435761 *
  DUP 32 RSHIFT XOR
  DUP 16 RSHIFT XOR
  cap 1- AND ;

: HT-INSERT { val key ht -- slots cap i }
  ht 0 FIELD@ TO slots
  ht 2 FIELD@ TO cap
  key cap HT-HASH TO i
  BEGIN
    slots 2 i * ARRAY@
    DUP 0= IF DROP
      key slots 2 i * ARRAY!
      val slots 2 i * 1+ ARRAY!
      ht 1 FIELD@ 1+ ht 1 FIELD!
      EXIT
    THEN
    DUP key = IF DROP
      val slots 2 i * 1+ ARRAY!
      EXIT
    THEN
    DROP
    i 1+ cap 1- AND TO i
  AGAIN ;

: HT-GROW { ht -- old oldcap newcap newtab }
  ht 0 FIELD@ TO old
  ht 2 FIELD@ TO oldcap
  oldcap 2 * TO newcap
  newcap 2 * ht 3 FIELD@ ARRAY-NEW TO newtab
  newtab ht 0 FIELD!
  newcap ht 2 FIELD!
  0 ht 1 FIELD!
  oldcap 0 ?DO
    old 2 I * ARRAY@ DUP 0 <> IF
      old 2 I * 1+ ARRAY@  old 2 I * ARRAY@  ht HT-INSERT
    ELSE DROP THEN
  LOOP ;

: HT-PUT { val key ht -- }
  ht 1 FIELD@ 1+ 4 * ht 2 FIELD@ 3 * >= IF ht HT-GROW THEN
  val key ht HT-INSERT ;

: HT-GET { key ht -- val slots cap i }
  ht 0 FIELD@ TO slots
  ht 2 FIELD@ TO cap
  key cap HT-HASH TO i
  BEGIN
    slots 2 i * ARRAY@
    DUP 0= IF DROP 0 EXIT THEN
    DUP key = IF DROP slots 2 i * 1+ ARRAY@ EXIT THEN
    DROP
    i 1+ cap 1- AND TO i
  AGAIN ;

: HT-NEW { cap store -- ht }
  cap 2 < IF 2 TO cap THEN
  HASHTABLE store NEW TO ht
  cap 2 * store ARRAY-NEW ht 0 FIELD!
  cap ht 2 FIELD!
  0 ht 1 FIELD!
  store ht 3 FIELD!
  ht ;

: HT-N { ht -- n } ht 1 FIELD@ ;
: HT-CAP { ht -- cap } ht 2 FIELD@ ;
