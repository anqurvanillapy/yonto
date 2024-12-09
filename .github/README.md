# Yonto

> WIP.

Yonto 脚本语言.

* 酷似 Python 的极简语法
* ∂CBPV 类型系统
* 支持 Shell DSL, 可当作 Shell 脚本编写
* 基于 [libgccjit], 支持四种模式的编译或运行:
    * 启用 JIT 的脚本运行
    * 纯字节码的脚本运行 (即关闭 JIT 引擎)
    * 字节码编译
    * AOT 全机器码编译
* 编译器使用 C++23 编写

[libgccjit]: https://gcc.gnu.org/onlinedocs/jit/

## 许可

MIT
