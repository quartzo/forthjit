\ per-task object stores: each task attaches a private child of SYSTEM
REQUIRE objects.fs

: CHECK { f a u -- }
  f 0= IF S" FAIL: " TYPE a u TYPE CR -1 THROW THEN ;

SYSTEM NEWTYPE CELLT
  0 FIELD v
;TYPE

\ a task allocates n objects in its own store and reports its live count
: WT { n -- live st }
  TASK-STORE TO st
  n 0 DO CELLT st NEW DROP LOOP
  st OBJECTS-LIVE ;

5 ' WT 1 SPAWN JOIN 5 = S" task1 isolated" CHECK
9 ' WT 1 SPAWN JOIN 9 = S" task2 isolated" CHECK

\ parallel tasks each see only their own objects (4 and 6, not 4 and 10)
4 ' WT 6 ' WT PAR + 10 = S" parallel isolated" CHECK

\ GC inside a task collects that task's unrooted objects
: WT-GC { n -- live st }
  TASK-STORE TO st
  n 0 DO CELLT st NEW DROP LOOP
  st GC st OBJECTS-LIVE ;
6 ' WT-GC 1 SPAWN JOIN 0 = S" task gc" CHECK

\ a rooted local survives the task's own GC
: WT-ROOT { n -- live st keep }
  TASK-STORE TO st
  CELLT st NEW TO keep
  42 keep S" v" !FIELD
  n 0 DO CELLT st NEW DROP LOOP
  st GC
  keep S" v" @FIELD DROP
  st OBJECTS-LIVE ;
3 ' WT-ROOT 1 SPAWN JOIN 1 = S" task root" CHECK

\ the shared system store is untouched by task allocations
SYSTEM OBJECTS-LIVE 0 = S" root store unaffected" CHECK

SYSTEM STORE-FREE
S" task-store ok" TYPE CR
