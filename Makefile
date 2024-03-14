CXX := gcc
LINT := \
	-std=c++17 \
	-Wno-poison-system-directories \
	-Wno-c++98-compat-pedantic \
	-Weverything -Werror
OPTIMIZE := -O0
DEBUG := -g
INCLUDE := -isystem /usr/local/include
LINKS := -lc++ -lgccjit
ARGS := ${LINT} ${OPTIMIZE} ${DEBUG} ${INCLUDE}

HEADER := jian.h
GCH := ${HEADER}.gch
SRC := '\#include"jian.h"\nint main(int c,const char*v[]){return jian::main(c,v);}'

BIN := echo ${SRC} | ${CXX} ${ARGS} ${LINKS} -x c++ -o
LIB := ${CXX} ${ARGS} -x c++-header -c

.PHONY: all
all: jian ${GCH} sanitize

${HEADER}:

${GCH}: ${HEADER}
	${LIB} $^

jian: ${GCH}
	${BIN} $@ -

.PHONY: sanitize
sanitize: jian_sanitize_asan jian_sanitize_tsan jian_sanitize_ubsan

jian_sanitize_msan: ${GCH}
	${BIN} $@ -fsanitize=memory -

jian_sanitize_asan: ${GCH}
	${BIN} $@ -fsanitize=address -

jian_sanitize_tsan: ${GCH}
	${BIN} $@ -fsanitize=thread -

jian_sanitize_lsan: ${GCH}
	${BIN} $@ -fsanitize=leak -

jian_sanitize_ubsan: ${GCH}
	${BIN} $@ -fsanitize=undefined -

.PHONY: clean
clean:
	rm -rf jian *_sanitize_*san *.dSYM *.gch
