#ifndef TINYCOMPILER_UTILS_H
#define TINYCOMPILER_UTILS_H

#include <string>

// 统一的错误输出：写到 stderr，方便和正常输出区分开
void LogError(const std::string& msg);

// JSON 字符串转义（AST 可视化用）
std::string JsonEscape(const std::string& s);

// 把词法分析器抓到的字符串字面量（两端带引号）转成真正的内容：
// 去掉引号，并把 \n \t \r \\ \" 这些转义序列还原成本来的字符。
std::string UnescapeStringLiteral(const std::string& raw);

#endif // TINYCOMPILER_UTILS_H
