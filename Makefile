LINT_FLAGS := \
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
OPTIMIZE_FLAGS := -O0
DEBUG_FLAGS := -g3
LIBS := -lgccjit

joben: joben.c
	clang ${LINT_FLAGS} ${OPTIMIZE_FLAGS} ${DEBUG_FLAGS} ${LIBS} -o $@ $^
