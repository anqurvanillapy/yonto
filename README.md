# JianScript

> WIP.

JianScript 是一门静态类型脚本语言, 它具有以下语言特性:

* 极简语法
* 基于 [libgccjit], 支持四种模式的编译或运行:
    * 启用 JIT 的脚本运行
    * 纯字节码的脚本运行 (即关闭 JIT 引擎)
    * 字节码编译
    * AOT 全机器码编译
* 依赖类型 (dependent types)
* 行多态 (row polymorphism)
* 内置 monadic 风格副作用系统 (effect system)
* 编译器使用 C++17 编写

[libgccjit]: https://gcc.gnu.org/onlinedocs/jit/

## 许可

MIT
