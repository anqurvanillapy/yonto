CXX := gcc
LINT := \
	-x c++ -std=c++17 \
	-Wno-poison-system-directories \
	-Wno-c++98-compat-pedantic \
	-Weverything -Werror
OPTIMIZE := -O0
DEBUG := -g
INCLUDE := -isystem /usr/local/include
LIBS := -lc++ -lgccjit
ARGS := ${LINT} ${OPTIMIZE} ${DEBUG} ${INCLUDE} ${LIBS}
HEADER := jian.h
SRC := '\#include"jian.h"\nint main(int c,const char*v[]){return jian::main(c,v);}'
BIN := echo ${SRC} | ${CXX} ${ARGS}

.PHONY: all
all: jian sanitize

${HEADER}:

jian: ${HEADER}
	${BIN} -o $@ -

.PHONY: sanitize
sanitize: jian_sanitize_asan jian_sanitize_tsan jian_sanitize_ubsan

jian_sanitize_msan: ${HEADER}
	${BIN} -fsanitize=memory -o $@ -

jian_sanitize_asan: ${HEADER}
	${BIN} -fsanitize=address -o $@ -

jian_sanitize_tsan: ${HEADER}
	${BIN} -fsanitize=thread -o $@ -

jian_sanitize_lsan: ${HEADER}
	${BIN} -fsanitize=leak -o $@ -

jian_sanitize_ubsan: ${HEADER}
	${BIN} -fsanitize=undefined -o $@ -

.PHONY: clean
clean:
	rm -rf jian jian_sanitize_* *.dSYM
