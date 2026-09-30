CC       ?= gcc
CFLAGS   ?= -std=c11 -O2 -Wall -Wextra
TARGET    = forth

SLJIT_DIR = third_party/sljit_src
SLJIT_OBJ = $(SLJIT_DIR)/sljitLir.o
SLJIT_DEFS = -DSLJIT_DEBUG=0 -DSLJIT_ARGUMENT_CHECKS=1

PRELUDE_SRC = prelude.fs
PRELUDE_HDR = prelude_blob.h
MKPRELUDE   = tools/mkprelude

CPPFLAGS += -I$(SLJIT_DIR)

all: $(TARGET)

$(MKPRELUDE): tools/mkprelude.c
	$(CC) -O2 -o $@ tools/mkprelude.c -lz

$(PRELUDE_HDR): $(PRELUDE_SRC) $(MKPRELUDE)
	./$(MKPRELUDE) $(PRELUDE_SRC) $(PRELUDE_HDR)

$(SLJIT_OBJ): $(SLJIT_DIR)/sljitLir.c
	$(CC) -O2 -w $(SLJIT_DEFS) -I$(SLJIT_DIR) -c $< -o $@

$(TARGET): forth.c $(SLJIT_OBJ) $(PRELUDE_HDR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ forth.c $(SLJIT_OBJ) -pthread -ldl -lz

test: $(TARGET)
	./$(TARGET) test.fs </dev/null
	./$(TARGET) test-native.fs </dev/null

asan: forth.c $(SLJIT_OBJ) $(PRELUDE_HDR)
	$(CC) -std=c11 -g -O1 -Wall -Wextra $(CPPFLAGS) -fsanitize=address,undefined -o $(TARGET) forth.c $(SLJIT_OBJ) -pthread -ldl -lz

clean:
	rm -f $(TARGET) $(SLJIT_OBJ) $(MKPRELUDE) $(PRELUDE_HDR)

.PHONY: all test asan clean
