CC := clang
LINT := \
	-std=c99 \
	-Werror \
	-Wall \
	-Wextra \
	-Wpedantic \
	-Wshadow \
	-Wcast-align \
	-Wunused \
	-Wconversion \
	-Wsign-conversion \
	-Wnull-dereference \
	-Wdouble-promotion \
	-Wformat=2
OPTIMIZE := -O0
DEBUG := -g
LIBS := -lgccjit
ARGS := ${LINT} ${OPTIMIZE} ${DEBUG} ${LIBS}
SRC := jian.c

.PHONY: all
all: jian sanitize

${SRC}:

jian: ${SRC}
	${CC} ${ARGS} -o $@ $^

.PHONY: sanitize
sanitize: jian_sanitize_asan jian_sanitize_tsan jian_sanitize_ubsan

jian_sanitize_msan: ${SRC}
	${CC} ${ARGS} -fsanitize=memory -o $@ $^

jian_sanitize_asan: ${SRC}
	${CC} ${ARGS} -fsanitize=address -o $@ $^

jian_sanitize_tsan: ${SRC}
	${CC} ${ARGS} -fsanitize=thread -o $@ $^

jian_sanitize_lsan: ${SRC}
	${CC} ${ARGS} -fsanitize=leak -o $@ $^

jian_sanitize_ubsan: ${SRC}
	${CC} ${ARGS} -fsanitize=undefined -o $@ $^

.PHONY: clean
clean:
	rm -rf jian jian_sanitize_* *.dSYM
