\ test-lib.fs - exercise the on-demand library mechanism (INCLUDE/REQUIRE).
REQUIRE vm.fs

\ sum 1..10 on the bytecode VM
CREATE PROG
  1 , 0 , 1 , 10 , 5 , 8 , 16 , 10 , 11 , 2 , 10 ,
  1 , 1 , 3 , 7 , 4 , 6 , 9 , 0 ,
PROG VM-BIND VM-RUN CR

\ call/cc: invoking the continuation skips the PUSH 111 / PRINT
CREATE CC1
  12 , 8 , 1 , 222 , 13 , 1 , 111 , 9 , 9 , 0 ,
CC1 VM-BIND VM-RUN CR

\ REQUIRE is idempotent
REQUIRE vm.fs
1 2 + . CR
