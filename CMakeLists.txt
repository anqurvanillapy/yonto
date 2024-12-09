cmake_minimum_required(VERSION 3.31 FATAL_ERROR)

project(yonto VERSION 0.1.0 LANGUAGES CXX)

add_executable(yonto yonto.cc)
target_precompile_headers(yonto PRIVATE yonto.h)
target_compile_features(yonto PRIVATE cxx_std_23)
target_compile_options(yonto PRIVATE
        -Werror
        -Weverything
        $<$<CONFIG:Debug>:-fsanitize=address>
)
target_link_options(yonto PRIVATE $<$<CONFIG:Debug>:-fsanitize=address>)
target_link_libraries(yonto PRIVATE gccjit)
