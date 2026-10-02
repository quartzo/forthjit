\ hierarchical store chain: child references parent, GC deferral, lifetime
REQUIRE objects.fs

: CHECK { f a u -- }
  f 0= IF S" FAIL: " TYPE a u TYPE CR -1 THROW THEN ;

VARIABLE R
VARIABLE C
VARIABLE A
VARIABLE B

1 20 LSHIFT STORE-NEW R !
R @ NEWTYPE NODE
  1 FIELD next
  0 FIELD val
;TYPE

\ an object in the root, kept alive as a root
NODE R @ NEW A !
A R @ ROOT
100 A @ 0 FIELD!

\ a child store of the root
R @ 1 20 LSHIFT STORE-CHILD C !
C @ STORE-PARENT R @ = S" child parent" CHECK
R @ STORE-NCHILDREN 1 = S" root has one child" CHECK

NODE C @ NEW B !
B C @ ROOT
A @ B @ 0 FIELD!          \ B.next = A : child -> parent reference
7 B @ 1 FIELD!

\ child GC keeps its rooted object and never touches the parent's object
C @ OBJECTS-LIVE 1 = S" child live" CHECK
C @ GC
C @ OBJECTS-LIVE 1 = S" child live after gc" CHECK
A @ 0 FIELD@ 100 = S" parent object intact" CHECK

\ root GC is deferred while it has a live child
R @ OBJECTS-LIVE 1 = S" root live" CHECK
R @ GC
R @ OBJECTS-LIVE 1 = S" root live after deferred gc" CHECK

\ dropping the last child un-defers the root
C @ STORE-FREE
R @ STORE-NCHILDREN 0 = S" root has no child" CHECK

\ now an unrooted object in the root is collectable
NODE R @ NEW DROP
R @ OBJECTS-LIVE 2 = S" root live+1" CHECK
R @ GC
R @ OBJECTS-LIVE 1 = S" root live after gc" CHECK
A @ 0 FIELD@ 100 = S" parent object intact after gc" CHECK

\ idempotent type definition: a redefinition reuses the descriptor, and a
\ definition in a child reuses the one found up the chain
' NODE >BODY @ CONSTANT NODE-D
R @ NEWTYPE NODE 1 FIELD next 0 FIELD val ;TYPE
' NODE >BODY @ NODE-D = S" idempotent redefine" CHECK
R @ 1 20 LSHIFT STORE-CHILD C !
C @ NEWTYPE NODE 1 FIELD next 0 FIELD val ;TYPE
' NODE >BODY @ NODE-D = S" idempotent via chain" CHECK
C @ STORE-FREE

R @ STORE-FREE
SYSTEM STORE-FREE
S" store-chain ok" TYPE CR
