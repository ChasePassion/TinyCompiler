#include <cstdio>

// TinyCompiler 编译出来的函数用的是 C 调用约定，
// 所以在 C++ 里要用 extern "C" 声明，否则名字会被改写而链接不上。
extern "C" int add(int a, int b);
extern "C" int square(int x);
extern "C" int fib(int n);

int main() {
    printf("add(3, 4) = %d\n", add(3, 4));
    printf("square(5) = %d\n", square(5));
    printf("fib(10) = %d\n", fib(10));
    return 0;
}
