#ifndef TINYCOMPILER_TESTS_EXTERN_MAIN_CPP
#define TINYCOMPILER_TESTS_EXTERN_MAIN_CPP

// tests/testExtern.input 本身没有 main，这里提供一个入口函数，
// 用来把它和编译出来的目标文件链接成可执行程序。
extern "C" int testExtern();

int main() {
    return testExtern();
}

#endif
