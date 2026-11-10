#ifndef TINYCOMPILER_OBJGEN_H
#define TINYCOMPILER_OBJGEN_H

#include <string>

class CodeGenContext;

// 把已经生成的 LLVM IR 翻译成目标文件（.o / .obj）
void ObjGen(CodeGenContext& context, const std::string& filename = "output.o");

#endif // TINYCOMPILER_OBJGEN_H