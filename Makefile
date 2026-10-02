CC       ?= gcc
CFLAGS   ?= -std=c11 -Os -Wall -Wextra
TARGET    = forth

SLJIT_DIR = third_party/sljit_src
SLJIT_OBJ = $(SLJIT_DIR)/sljitLir.o
SLJIT_DEFS = -DSLJIT_DEBUG=0 -DSLJIT_ARGUMENT_CHECKS=0 -DSLJIT_VERBOSE=0

TINF_DIR = third_party/tinf
TINF_OBJ = $(TINF_DIR)/tinflate.o

PRELUDE_SRC = prelude.fs
PRELUDE_HDR = prelude_blob.h
MKPRELUDE   = tools/mkprelude

# Trim dead code and unwind metadata; -s strips the release binary.
SHRINK   = -ffunction-sections -fdata-sections \
           -fno-asynchronous-unwind-tables -fno-unwind-tables
LDFLAGS += -Wl,--gc-sections -Wl,--as-needed

CPPFLAGS += -I$(SLJIT_DIR) -I$(TINF_DIR)

all: $(TARGET)

$(MKPRELUDE): tools/mkprelude.c
	$(CC) -O2 -o $@ tools/mkprelude.c -lz

$(PRELUDE_HDR): $(PRELUDE_SRC) $(MKPRELUDE)
	./$(MKPRELUDE) $(PRELUDE_SRC) $(PRELUDE_HDR)

$(SLJIT_OBJ): $(SLJIT_DIR)/sljitLir.c
	$(CC) -Os -w $(SLJIT_DEFS) -I$(SLJIT_DIR) -c $< -o $@

$(TINF_OBJ): $(TINF_DIR)/tinflate.c $(TINF_DIR)/tinf.h
	$(CC) -Os -DNDEBUG -w -I$(TINF_DIR) -c $< -o $@

$(TARGET): forth.c $(SLJIT_OBJ) $(TINF_OBJ) $(PRELUDE_HDR)
	$(CC) $(CFLAGS) $(SHRINK) $(CPPFLAGS) $(LDFLAGS) -o $@ forth.c \
	      $(SLJIT_OBJ) $(TINF_OBJ) -ldl -lm -lpthread -Wl,-s

test: $(TARGET)
	./$(TARGET) test.fs </dev/null
	./$(TARGET) test-native.fs </dev/null
	./$(TARGET) test-lib.fs </dev/null
	./$(TARGET) test-scheme.fs </dev/null
	FORTH_WORKERS=4 ./$(TARGET) test-threads.fs </dev/null
	@printf '(+ 1 2)\n' | ./$(TARGET) --require scheme.fs --repl SCHEME >/dev/null

asan: forth.c $(SLJIT_OBJ) $(TINF_OBJ) $(PRELUDE_HDR)
	$(CC) -std=c11 -g -O1 -Wall -Wextra $(CPPFLAGS) -fsanitize=address,undefined -o $(TARGET) forth.c $(SLJIT_OBJ) $(TINF_OBJ) -ldl -lm -lpthread

tsan: forth.c $(SLJIT_OBJ) $(TINF_OBJ) $(PRELUDE_HDR)
	$(CC) -std=c11 -g -O1 -Wall -Wextra $(CPPFLAGS) -fsanitize=thread -o $(TARGET) forth.c $(SLJIT_OBJ) $(TINF_OBJ) -ldl -lm -lpthread

clean:
	rm -f $(TARGET) $(SLJIT_OBJ) $(TINF_OBJ) $(MKPRELUDE) $(PRELUDE_HDR)

.PHONY: all test asan tsan clean
