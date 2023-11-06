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
SRC := joben.c

joben: ${SRC}
	${CC} ${ARGS} -o $@ $^

.PHONY: sanitize
sanitize: joben_sanitize_asan joben_sanitize_tsan joben_sanitize_ubsan

joben_sanitize_msan: ${SRC}
	${CC} ${ARGS} -fsanitize=memory -o $@ $^

joben_sanitize_asan: ${SRC}
	${CC} ${ARGS} -fsanitize=address -o $@ $^

joben_sanitize_tsan: ${SRC}
	${CC} ${ARGS} -fsanitize=thread -o $@ $^

joben_sanitize_lsan: ${SRC}
	${CC} ${ARGS} -fsanitize=leak -o $@ $^

joben_sanitize_ubsan: ${SRC}
	${CC} ${ARGS} -fsanitize=undefined -o $@ $^

.PHONY: clean
clean:
	rm -rf joben joben_sanitize_* *.dSYM
