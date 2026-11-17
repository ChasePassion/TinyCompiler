#include "utils.h"

#include <cstdio>
#include <string>

namespace {
// 已经报出来的错误个数
int g_errorCount = 0;
} // namespace

void LogError(const std::string& msg) {
    fprintf(stderr, "error: %s\n", msg.c_str());
    ++g_errorCount;
}

int ErrorCount() {
    return g_errorCount;
}

std::string UnescapeStringLiteral(const std::string& raw) {
    // raw 的两端是引号，真正的内容在中间
    std::string out;
    for (size_t i = 1; i + 1 < raw.size(); ++i) {
        char c = raw[i];
        if (c != '\\' || i + 2 >= raw.size()) {
            out += c;
            continue;
        }
        char escaped = raw[++i];
        switch (escaped) {
        case 'n':  out += '\n'; break;
        case 't':  out += '\t'; break;
        case 'r':  out += '\r'; break;
        case '0':  out += '\0'; break;
        case '\\': out += '\\'; break;
        case '"':  out += '"';  break;
        default:
            // 不认识的转义序列原样保留
            out += '\\';
            out += escaped;
            break;
        }
    }
    return out;
}
