# Object store rework: hierarchical stores on arenas

Status: **Complete (Phases 1-6).**  There is no clone transport: sibling
stores do not exchange mutable graphs.

## Goal

Replace the single global, `MALLOC`-per-object object store (`lib/objects.fs`)
with per-instance **hierarchical stores** backed by **arenas**, so that a
microthread gets an isolated mutable heap while sharing immutable code and
types with its ancestors.

## Model

An object store is a node in a **single-parent chain** (a tree).  Lookups
(type, code, binding) walk child -> parent.  This mirrors what the kernel
already does: `forth_task_new` shares `fcode` and the dictionary through
`F->root` (`forth.c:1625`); tasks differ only in stacks.

- **Shared store** = the root context's store (code objects, types, shared
  data).  One per engine/root.
- **Leaf store** = per microthread (task), `parent = root store`, created in
  `forth_task_new` and freed in `forth_task_free`.

### Invariants (agreed)

1. **Ancestors are mutable** (open); no freezing.
2. **Single parent always** (tree, not a DAG).
3. **References point up only**: an object field may point child -> parent;
   **parent -> child is forbidden in object fields**.  The only downward edge
   is ownership, kept in store metadata that the GC never scans.
4. **No cross-store GC.**
   - A leaf store marks/sweeps only its own objects; a pointer that is not in
     its own index stops the mark (do not trace, do not free).
   - An ancestor store with >= 1 live child stays **all-live** (GC deferred);
     when its child count drops to 0 it GCs normally from its own roots.
   - Child count is a refcount maintained at store create/free.
   - Future optimization (not MVP): an upward remembered set to let ancestors
     GC while children are alive.
5. **No `FREEZE`.**
6. **Types** live in a store catalog; resolution walks the chain; `NEWTYPE` is
   **idempotent** (reuse an existing name found up the chain instead of
   creating a duplicate).  A type lives as long as its store.

Store metadata (the `T-STORE` handles and the child registry) lives in a
**system store `S`** at the top, separate from data arenas, so ownership edges
are never mistaken for data pointers.

## Mapped onto the current code

| Concept | Location |
| --- | --- |
| shared store / leaf store | `F->root` store / task store |
| create leaf store | `forth_task_new` (`forth.c:1625`) |
| free leaf store | `forth_task_free` (`forth.c:1660`; used by `join_task` `forth.c:1827` and spawn error `forth.c:1804`) |
| spawn: shared XT + scalars | `SPAWN` (`forth.c:1832`) |
| store record (was global state) | `lib/objects.fs` `S-*` fields |
| object allocation (was `MALLOC`) | `NEW` -> `ARENA-ALLOC` |
| code registry (now growable) | `g_ir.jit_codes` + `ir_track_code` |

## Phases

1. **`lib/arena.fs`** (DONE) - C prims `MAP-RW` / `MAP-RWX` / `UNMAP`
   (`forth.c`); Forth arena instance, 64 KiB blocks, first-fit free list,
   splitting, no coalescing; data and RWX code arenas.  Tests in
   `test-arena.fs`, wired into `make test`.
   Fix: the bump pointer now leaves room for the chunk header after the block
   header (previously the first chunk's header overlapped the block's
   `cap`/`used`, which overfilled large blocks and corrupted adjacent memory).
   Fix: a free chunk needs **two** cells (size + next-free), so a split is only
   done when the remainder is `>= 16` bytes; allowing an 8-byte remainder wrote
   the free-list link over the following chunk.  Latent with fixed-size objects
   (all requests equal), exposed by variable-size arrays.
2. **Store instance (no globals)** (DONE) - `lib/objects.fs` now carries all
   runtime GC/alloc state in a `Store` record (`STORE-NEW` / `STORE-FREE`)
   over two arenas: `S-DATA` for objects and `S-META` for the index/worklist.
   The handle is threaded explicitly (`( ... store -- ... )`): `NEW`, `GC`,
   `ROOT`, `UNROOT`, `OBJECTS-LIVE/STATS/FREE`.  `NEW` uses `ARENA-ALLOC` on
   the data arena; `S-FREEOBJ` returns the chunk to the arena free list.
   Types/catalog remain global process-lifetime metadata for now (moved into
   the system store in Phase 3).  Tests updated (`test-objects.fs`,
   `lib/scheme-jit.fs`, `test-scheme-jit.fs`).
3. **Chain + system store** (DONE) - `STORE-MAKE`/`STORE-NEW` (root)/
   `STORE-CHILD` (child); single parent (`S-PARENT`); child registry
   (`S-CHILDREN`/`S-SIBLING`) and refcount (`S-NCHILD`) kept in plain,
   non-GC records so parent->child ownership is never scanned as a data
   pointer.  A `SYSTEM` root store is created once at load.  The type catalog
   now lives per store (`S-CATALOG`); `FIND-TYPE-CHAIN` walks up, and
   `NEWTYPE` (which now takes the target store) is idempotent: a same-named
   type is reused instead of duplicated.  `WORD-NAME` (C) exposes a word's
   name so descriptors can be catalogued.  Tested in `test-store-chain.fs`.
4. **GC policy** - leaf normal; ancestor all-live while it has children, GC at
   zero; boundary rule in `MARK-CELL`.  (The all-live deferral landed with
   Phase 3: `S-GC` returns immediately when `S-NCHILD > 0`, so no cross-store
   tracing is ever needed.)
5. **Task integration** (DONE) - `struct forth` gained a `store` field;
   primitives `(GET-STORE)`/`(SET-STORE)`, `STORE-LOCK`/`STORE-UNLOCK`, and
   `(TASK-HOOKS)` register Forth enter/leave words.  `task_run` runs the enter
   hook before the body and the leave hook afterwards (with its own abort
   guard), so each task attaches `SYSTEM 0 STORE-CHILD` and frees it.  The
   child registry is guarded by a dedicated mutex so concurrent spawns are
   safe.  `TASK-STORE` is the store to use inside a task; the root context's
   store is `SYSTEM`.  Tested in `test-task-store.fs` (isolation, parallel
   isolation, per-task GC, rooted local survives).
6. **Code as object** (DONE) - each store gained an RWX `S-CODE` arena
   (`ARENA-NEW-CODE`).  `NEW-CODE` copies generated bytes into it and returns
   a `CODE-OBJ` (fields `addr`/`size`, both scalar); `CODE-PTR`/`CODE-SIZE`
   read them.  The process-lifetime registry `g_ir.jit_codes` is now growable
   (`ir_track_code`) instead of `MAX_JIT` slots.  `ir_compile` takes a `keep`
   flag; the new `ASSEMBLE-FORTH-DYN` returns untracked code so the caller can
   `NEW-CODE` then `(SLJIT-FREE)`.  `lib/scheme-jit.fs` now stores a code
   object in the `JITF` record.  Tested in `test-code-obj.fs`.
   Also fixed a latent bug: `forth.c`/`code.c` were compiled without
   `SLJIT_DEFS`, giving a mismatched `struct sljit_compiler` layout
   (`SLJIT_DEBUG`/`SLJIT_VERBOSE` default to 1), which made
   `sljit_get_generated_code_size` return 0 and `sljit_get_executable_offset`
   garbage.

## Scheme runtime on the store

`lib/scheme.fs` now allocates **pairs and closures as GC objects** in the
current context's store (`SCM-STORE` = `TASK-STORE`, i.e. `SYSTEM` in the
root).  Because Scheme values are tagged cells, the store is given a **tag
mask** (`7 SCM-STORE STORE-MASK!`): `S-MARK-CELL` clears the low bits before
looking a value up in the object table, so GC traces tagged pair/closure
fields and tagged roots correctly.  Symbols and strings are interned and
immutable, living in the store's meta arena (`META-ALLOC`); `GENV` and `SYMS`
are registered as roots.  `test-scheme-gc.fs` stresses collection.

The evaluator is an **iterative CEK-style machine**: `M-EXPR`/`M-ENV`/`M-VAL`
hold the current state, and the continuation is a plain **cell stack** (`KSTK`)
registered as a GC root range (via `ROOT-RANGE`).  Scheme recursion uses
neither the C stack nor heap allocation for control, and tail calls are
constant-space (proper tail calls).  Verified with `(build 2000)` (non-tail)
and a tail loop under a 512 KB C stack.  (Moving control frames from Scheme
lists to `KSTK` made the evaluator ~7x faster, and building argument lists in
order — no `REVERSE` — gave ~25% more.)

The object allocator has a **C fast path**: `(OBJ-FAST)` does bump/first-fit
data-arena allocation, object-header setup, list link and hash-table insert;
`(TAB-REHASH)` does table growth.  Both fall back to the Forth `S-NEW` /
`S-TAB-GROW` for the slow cases (GC, block growth).  Offsets in `forth.c`
mirror `lib/objects.fs` / `lib/arena.fs`.  Note the arena chunk header is
**one** cell (`C-HDR`): the free-list link shares the first payload cell.
This cut `CONS` ~3.6x and the evaluator further; the whole Scheme loop is now
~12x faster than the original recursive evaluator.
Also fixed a latent arena bug: when a free chunk was reused with a split, the
allocated chunk's size field was not updated, so freeing it later overlapped
the split remainder.

## Arrays (base GC-traced element)

`lib/objects.fs` gained a variable-length **`ARRAY`** base element: payload of
`H-SIZE` generic cells, allocated with `ARRAY-NEW` (`S-NEW-N` with an explicit
payload size, zero-filled).  Marking is **conservative** (`TRACE-ALL` scans
every cell): only values that are objects of this store are marked, so numbers
and null are ignored.  This is sound (never misses a live reference); at worst
a number aliasing an object address retains an object.  API: `ARRAY-NEW`,
`ARRAY-LEN`, `ARRAY@ ( arr i -- x )`, `ARRAY! ( x arr i -- )`.  The C fast path
became `(OBJ-FAST-N)` (explicit payload size), used by fixed structs and
arrays alike.  Tested in `test-array.fs`.

## Hash table (first use of arrays)

`lib/hashtable.fs` adds a generic **open-addressing hash table** on top of the
store arrays (no Scheme concepts).  A table is a `HASHTABLE` store object with
`slots` (an ARRAY of `2*cap` cells), `n`, `cap` and its owning store; slot `i`
is `key` at `2i` and `value` at `2i+1`, empty when the key is `0` (so `0` is
not a valid key).  `cap` must be a power of two.  API: `HT-NEW ( cap store -- ht )`,
`HT-PUT ( val key ht -- )`, `HT-GET ( key ht -- val )`, `HT-N`, `HT-CAP`;
growth rehashes into a doubled array (the old one is just abandoned to GC).
GC reaches keys/values via `ht-slots` -> ARRAY -> conservative cell scan.
Tested in `test-hashtable.fs`.

## Scheme global environment on the store

The Scheme global bindings are no longer a process-global `GENV` alist.
`lib/objects.fs` gained generic per-store handle fields `S-BINDINGS` and
`S-SYMBOLS` (`STORE-BINDINGS`/`!`, `STORE-SYMBOLS`/`!`); `lib/scheme.fs`
creates a `HASHTABLE` in `S-BINDINGS` in `SCHEME-INIT` and keeps the symbol
intern list in `S-SYMBOLS` (`SYMS` now returns that field address), registering
both fields as store roots.  The `env`
chain passed around the evaluator now holds only **local frames** (parameters,
`let`, closures); `ENV-LOOKUP` walks them and falls back to the global hash
table, while `ENV-DEFINE`/`ENV-SET` write to the innermost local frame or, at
top level, to the table.  So each store has its own global environment.
Tested via `test-scheme.fs` / `test-scheme-gc.fs`.

## Scheme vectors

Scheme vectors are a tagged store `ARRAY` (`T-VEC`): `make-vector`,
`vector-ref`, `vector-set!`, `vector-length`, `vector?`, printed as `#(...)`.
They reuse the conservative array marking; tested in `test-scheme.fs`.

## Array-backed environments

Each local frame is an `ARRAY` of `[key0 val0 key1 val1 ...]` (raw, untagged)
instead of an alist of pairs; `ENV-EXTEND` builds the frame in one allocation
and `ENV-DEFINE` reallocates + copies on local `define`.  `M-LET` allocates the
frame for `let` and the continuation carries `(rest frame k env body)` so the
CEK machine fills the frame in order without building an intermediate list.

This was earlier thought to crash under a deep tail loop, but with the
allocator bug below fixed and with full poisoning it is stable: `make test`,
50x native `(loop 12000 0)`, 30x `let` + GC, and Valgrind-clean on both.

### Split relink bug (fixed)

When first-fit found a free chunk that was **not** the list head and split it,
`ARENA-ALLOC` unlinked the chunk (`prev[1] = next` / `A_FREE = next`) and then
unconditionally set `A_FREE = rest`, **dropping every chunk before `prev`** from
the free list (a leak).  The split must relink the remainder where the chunk
was: `prev[1] = rest` when `prev != 0`, else `A_FREE = rest`.  Fixed in both
`p_obj_fast` (`forth.c`) and `ARENA-ALLOC` (`lib/arena.fs`).

### Free-list poisoning (Valgrind debugging)

`make vg` builds with `-DUSE_VALGRIND`; `ARENA-VG-ON` (set on each store's data
arena) makes `ARENA-FREE` mark the whole freed chunk `NOACCESS` and
`ARENA-ALLOC` reads the just-freed chunk safely (`(VG-OPEN-HDR)` /
`(VG-CLOSE-HDR)` / `(VG-OPEN-ALL)` / `(VG-CLOSE-ALL)`, which are no-ops in a
normal build).  Any write into a free chunk — from C or from native `CODE`
primitives (which ASAN cannot see) — is then reported by Valgrind with a stack
trace.  Only the data arena is poisoned; the meta arena holds the live address
index and mark worklist.

## Verification

- `OBJECTS-VERIFY ( store -- n )`: after GC, every live object must be indexed,
  its mark clear, and the object-list length must equal `TABN` (also rejects
  cycles).  `ARENA-VERIFY ( arena -- n )`: every free-list chunk must be
  8-aligned with a positive multiple-of-8 size.  Used by `test-gc-stress.fs`
  (mixed fixed/variable objects, repeated GC) and `test-scheme-gc.fs` (deep
  non-tail recursion), which all pass under ASAN.

- `make test` (includes `test-arena.fs`, `test-objects.fs`,
  `test-scheme-jit.fs`, `test-threads.fs`).
- `make asan` with `LD_PRELOAD=$(gcc -print-file-name=libasan.so)` on the
  store tests; `make tsan` for the task paths.
