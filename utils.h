#ifndef TINYCOMPILER_UTILS_H
#define TINYCOMPILER_UTILS_H

#include <string>

// 统一的错误输出：写到 stderr，方便和正常输出区分开
void LogError(const std::string& msg);

// 已经报了多少个错。编译器在最后会检查这个计数：
// 只要有错就不生成目标文件，并以非 0 的退出码结束。
int ErrorCount();

// JSON 字符串转义（AST 可视化用）
std::string JsonEscape(const std::string& s);

// 把词法分析器抓到的字符串字面量（两端带引号）转成真正的内容：
// 去掉引号，并把 \n \t \r \\ \" 这些转义序列还原成本来的字符。
std::string UnescapeStringLiteral(const std::string& raw);

#endif // TINYCOMPILER_UTILS_H
