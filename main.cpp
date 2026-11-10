#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>

#include "ASTNodes.h"
#include "CodeGen.h"
#include "ObjGen.h"
#include "utils.h"

// 语法分析器建立的语法树根节点
extern NBlock* programBlock;
extern int yyparse();
// flex 生成的词法分析器的输入文件指针
extern FILE* yyin;

namespace {

void PrintUsage(const char* program) {
    std::cout
        << "用法: " << program << " [选项] [源文件]\n"
        << "源文件可以省略，省略时从标准输入读取。\n\n"
        << "选项:\n"
        << "  -o <文件>     指定生成的目标文件，默认 output.o\n"
        << "  -S            只生成 LLVM IR（.ll）文件，不生成目标文件\n"
        << "  --ast        同时在屏幕上打印语法树\n"
        << "  --ast-json <文件>\n"
        << "                把语法树以 JSON 格式写到文件，默认 visualization/A_tree.json\n"
        << "  -h, --help   显示这个帮助信息\n";
}

} // namespace

int main(int argc, char** argv) {
    std::string outputFile = "output.o";
    std::string jsonFile;
    std::string inputFile;
    bool printAst = false;
    bool emitIROnly = false;

    for (int i = 1; i < argc; ++i) {
        std::string argument = argv[i];
        if (argument == "-o" && i + 1 < argc) {
            outputFile = argv[++i];
        } else if (argument == "-S") {
            emitIROnly = true;
        } else if (argument == "--ast") {
            printAst = true;
        } else if (argument == "--ast-json" && i + 1 < argc) {
            jsonFile = argv[++i];
        } else if (argument == "-h" || argument == "--help") {
            PrintUsage(argv[0]);
            return 0;
        } else if (!argument.empty() && argument[0] == '-') {
            fprintf(stderr, "error: 无法识别的参数 %s\n", argument.c_str());
            PrintUsage(argv[0]);
            return 1;
        } else {
            inputFile = argument;
        }
    }

    if (!inputFile.empty()) {
        yyin = fopen(inputFile.c_str(), "r");
        if (!yyin) {
            fprintf(stderr, "error: 无法打开源文件 %s\n", inputFile.c_str());
            return 1;
        }
    }

    if (yyparse() != 0 || programBlock == nullptr) {
        fprintf(stderr, "error: 语法分析失败\n");
        return 1;
    }

    // 语法树可视化：JSON 交给 visualization/ 下的脚本画树
    if (jsonFile.empty() && printAst) {
        jsonFile = "visualization/A_tree.json";
    }
    if (!jsonFile.empty()) {
        std::ofstream out(jsonFile);
        if (out.is_open()) {
            out << programBlock->toJson();
            out.close();
            std::cout << "语法树 JSON 已写入 " << jsonFile << "\n";
        } else {
            fprintf(stderr, "error: 无法写入 %s\n", jsonFile.c_str());
        }
    }
    if (printAst) {
        programBlock->print("--");
    }

    // 生成 LLVM IR，再翻译成目标文件
    CodeGenContext context;
    context.generateCode(*programBlock);

    if (ErrorCount() > 0) {
        fprintf(stderr, "error: 编译过程中有 %d 处错误\n", ErrorCount());
        return 1;
    }

    if (emitIROnly) {
        std::error_code ec;
        llvm::raw_fd_ostream out(outputFile, ec, llvm::sys::fs::OF_None);
        if (ec) {
            fprintf(stderr, "error: 无法写入 %s: %s\n", outputFile.c_str(), ec.message().c_str());
            return 1;
        }
        context.theModule->print(out, nullptr);
        out.flush();
        std::cout << "LLVM IR 已写入 " << outputFile << "\n";
        return 0;
    }

    ObjGen(context, outputFile);
    return ErrorCount() > 0 ? 1 : 0;
}
