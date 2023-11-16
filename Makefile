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
DEBUG := -g3
LIBS := -lgccjit
ARGS := ${LINT} ${OPTIMIZE} ${DEBUG} ${LIBS}
SRC := oxn.c

oxn: ${SRC}
	${CC} ${ARGS} -o $@ $^

.PHONY: sanitize
sanitize: oxn_sanitize_asan oxn_sanitize_tsan oxn_sanitize_ubsan

oxn_sanitize_msan: ${SRC}
	${CC} ${ARGS} -fsanitize=memory -o $@ $^

oxn_sanitize_asan: ${SRC}
	${CC} ${ARGS} -fsanitize=address -o $@ $^

oxn_sanitize_tsan: ${SRC}
	${CC} ${ARGS} -fsanitize=thread -o $@ $^

oxn_sanitize_lsan: ${SRC}
	${CC} ${ARGS} -fsanitize=leak -o $@ $^

oxn_sanitize_ubsan: ${SRC}
	${CC} ${ARGS} -fsanitize=undefined -o $@ $^

.PHONY: clean
clean:
	rm -rf oxn oxn_sanitize_* *.dSYM
