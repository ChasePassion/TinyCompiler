%{
#include <cstdio>
#include <string>

#ifdef _WIN32
// Windows 没有 unistd.h，但 flex 需要 isatty/fileno 来判断输入是不是终端。
// <io.h> 提供了 Windows 版本的这两个函数，并定义 YY_NO_UNISTD_H 让 flex
// 不要再 include <unistd.h>。
#include <io.h>
#define YY_NO_UNISTD_H
#undef isatty
#undef fileno
#define isatty _isatty
#define fileno _fileno
#endif

#include "ASTNodes.h"
#include "grammar.hpp"
#include "utils.h"

// 词法分析器当前所在的行号，报语法错误时用
int currentLine = 1;

// 关键字和标识符的文本都存到 yylval.text 里，用完记得 delete
#define SAVE_TOKEN yylval.text = new std::string(yytext)
#define TOKEN(t)   (yylval.token = (t))
%}

%option noyywrap

%%

"#"[^\n]*                ;
[ \t\r\n]+              {
                           for (char* p = yytext; *p; ++p) {
                               if (*p == '\n') { ++currentLine; }
                           }
                         }
"if"                    return TOKEN(TIF);
"else"                  return TOKEN(TELSE);
"return"                return TOKEN(TRETURN);
"for"                   return TOKEN(TFOR);
"while"                 return TOKEN(TWHILE);
"struct"                return TOKEN(TSTRUCT);
"int"|"double"|"float"|"char"|"bool"|"string"|"void" {
                           // 内置类型名统一用 TTYPENAME，具体哪种类型由名字决定
                           SAVE_TOKEN; return TTYPENAME;
                         }
"extern"                SAVE_TOKEN; return TEXTERN;
[a-zA-Z_][a-zA-Z0-9_]*   SAVE_TOKEN; return TIDENTIFIER;
[0-9]+\.[0-9]*          SAVE_TOKEN; return TDOUBLE;
[0-9]+                  SAVE_TOKEN; return TINTEGER;
\"(\\.|[^"])*\"         {
                           // 去掉两端的引号并处理转义，yylval 里放的是真正的内容
                           yylval.text = new std::string(UnescapeStringLiteral(yytext));
                           return TLITERAL;
                         }
"=="                    return TOKEN(TCEQ);
"!="                    return TOKEN(TCNE);
"<="                    return TOKEN(TCLE);
">="                    return TOKEN(TCGE);
"<<"                    return TOKEN(TSHIFTL);
">>"                    return TOKEN(TSHIFTR);
"<"                     return TOKEN(TCLT);
">"                     return TOKEN(TCGT);
"="                     return TOKEN(TEQUAL);
"("                     return TOKEN(TLPAREN);
")"                     return TOKEN(TRPAREN);
"{"                     return TOKEN(TLBRACE);
"}"                     return TOKEN(TRBRACE);
"["                     return TOKEN(TLBRACKET);
"]"                     return TOKEN(TRBRACKET);
"."                     return TOKEN(TDOT);
","                     return TOKEN(TCOMMA);
"+"                     return TOKEN(TPLUS);
"-"                     return TOKEN(TMINUS);
"*"                     return TOKEN(TMUL);
"/"                     return TOKEN(TDIV);
"&"                     return TOKEN(TAND);
"|"                     return TOKEN(TOR);
"^"                     return TOKEN(TXOR);
"%"                     return TOKEN(TMOD);
";"                     return TOKEN(TSEMICOLON);
.                       {
                           fprintf(stderr, "error: 第 %d 行: 无法识别的字符 '%s'\n",
                                   currentLine, yytext);
                           yyterminate();
                       }

%%
