NAME := jian

CXX := gcc
LINT := \
	-std=c++17 \
	-Wno-poison-system-directories \
	-Wno-c++98-compat-pedantic \
	-Wno-padded \
	-Weverything -Werror
OPTIMIZE := -O0
DEBUG := -g
INCLUDE := -isystem /usr/local/include
LINKS := -lc++ -lgccjit
ARGS := ${LINT} ${OPTIMIZE} ${DEBUG} ${INCLUDE}

BIN := ${NAME}
HEADER := ${NAME}.h
GCH := ${HEADER}.gch
SRC := '\#include"${NAME}.h"\nint main(int c,const char*v[]){return ${NAME}::main(c,v);}'

CXX_BIN := echo ${SRC} | ${CXX} ${ARGS} ${LINKS} -x c++ -o
CXX_LIB := ${CXX} ${ARGS} -x c++-header -c

.PHONY: all
all: \
	${BIN} \
	${GCH} \
	${BIN}_sanitize_asan \
	${BIN}_sanitize_tsan \
	${BIN}_sanitize_ubsan

${HEADER}:

${GCH}: ${HEADER}
	${CXX_LIB} $^

${BIN}: ${GCH}
	${CXX_BIN} $@ -

${BIN}_sanitize_msan: ${GCH}
	${CXX_BIN} $@ -fsanitize=memory -

${BIN}_sanitize_asan: ${GCH}
	${CXX_BIN} $@ -fsanitize=address -

${BIN}_sanitize_tsan: ${GCH}
	${CXX_BIN} $@ -fsanitize=thread -

${BIN}_sanitize_lsan: ${GCH}
	${CXX_BIN} $@ -fsanitize=leak -

${BIN}_sanitize_ubsan: ${GCH}
	${CXX_BIN} $@ -fsanitize=undefined -

.PHONY: clean
clean:
	rm -rf ${BIN} *_sanitize_*san *.dSYM *.gch
