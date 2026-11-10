# TinyCompiler

一个用 **flex + bison + LLVM** 实现的类 C 语言编译器。

源码经过「词法分析 → 语法分析 → 抽象语法树 → LLVM IR → 目标文件」这条流水线，
最终产出可以直接链接运行的 `.o` / `.obj` 文件。

- 编译器本体：`C++17`，依赖 LLVM 22（当前 LTS）
- 前端：`flex` 做词法分析，`bison` 做语法分析
- 后端：基于 LLVM 的 IR 生成接口，目标代码由 LLVM 后端完成
- 平台：Windows（MSVC）和 Linux 都可以

---

## 1. 快速开始

### 1.1 准备依赖

| 依赖 | 版本 | 说明 |
| --- | --- | --- |
| LLVM | 22.x | 需要 **C++ 头文件和库**，只有 C API 的发行版不够用 |
| flex | 2.6+ | 词法分析器生成器 |
| bison | 3.x | 语法分析器生成器 |
| CMake | 3.20+ | 构建 |
| C++ 编译器 | 支持 C++17 | MSVC 2019+ / GCC 7+ / Clang 6+ |

#### LLVM 去哪拿（Windows 上有个坑）

LLVM 官方在 Windows 上发的是**两个不同的包**，内容差别很大：

| 资产 | 大小 | 含 C++ 头文件？ | 说明 |
| --- | --- | --- | --- |
| `LLVM-22.1.8-win64.exe` | 434 MB | ❌ **没有** | 官方安装包，只有 `llvm-c/` 和 clang 工具链 |
| `clang+llvm-22.1.8-x86_64-pc-windows-msvc.tar.xz` | 822 MB | ✅ **有** | CI 用的完整包，含 `include/llvm/**`、145 个静态库、`LLVMConfig.cmake` |

官方安装包不含 C++ 开发文件是**有意为之**，LLVM 官方论坛上的原话：

> The pre-built LLVM installer for Windows only contains the toolchain parts:
> clang, lld, compiler-rt libraries, etc. If you need the LLVM libraries,
> the best way is to build them yourself.
> —— [How to compile the C file that uses LLVM on Windows?](https://discourse.llvm.org/t/how-to-compile-the-c-file-that-uses-llvm-on-windows/72642)

所以在 Windows 上有三条路可选，**推荐第 1 条**：

**① 官方 tar.xz（推荐，最"正统"，本项目默认用这个）**

```powershell
# 下载（走代理的话加上 --proxy）
curl -L -o llvm.tar.xz https://github.com/llvm/llvm-project/releases/download/llvmorg-22.1.8/clang+llvm-22.1.8-x86_64-pc-windows-msvc.tar.xz
# 解压
tar -xf llvm.tar.xz -C E:\LLVM-full
```

解压后目录是 `clang+llvm-22.1.8-x86_64-pc-windows-msvc/{bin,include,lib}`，
自带 `llvm-config.exe`、`LLVMConfig.cmake`、完整的头文件和静态库，
而且**不依赖 zlib/zstd 动态库**（编出来的 exe 只依赖系统 DLL）。

`build.ps1` 会自动在 `E:\LLVM-full`、`C:\LLVM-full` 等位置找它。

**② conda-forge**

```powershell
conda create -n tinycompiler -c conda-forge llvmdev=22 zlib zstd
conda activate tinycompiler
```

省事，但会多引入 conda 依赖，而且 `llvmdev` 是用 zlib 动态库链的，
编出来的 exe 需要 `zlib.dll` / `zstd.dll`（本项目的 CMakeLists 已经自动
把它们复制到 exe 旁边了）。

**③ MSYS2**

```bash
pacman -S mingw-w64-x86_64-llvm mingw-w64-x86_64-gcc flex bison
```

`mingw-w64-x86_64-llvm` 同样带完整的 `include/llvm/**` 和静态库，
而且 flex / bison 一并就位。注意它编出来的是 **MinGW ABI**
（目标三元组 `x86_64-w64-windows-gnu`），和 MSVC 那套不能混用。

#### flex / bison

Windows 上用 [winflexbison](https://github.com/lexxmark/winflexbison)
（提供 `win_flex.exe` 和 `win_bison.exe`），或者直接用 MSYS2 / conda 里的。

### 1.2 构建

Windows：

```powershell
pwsh -File build.ps1
```

`build.ps1` 会自动加载 Visual Studio 的编译环境、找到 LLVM / flex / bison，
然后调用 CMake。LLVM 目录默认会自动探测（`E:\LLVM-full`、`C:\LLVM-full`、
`C:\Program Files\LLVM` 等），也可以显式指定：

```powershell
# 用官方 tar.xz 解压出来的 LLVM（自动探测不到时）
pwsh -File build.ps1 -LlvmDir 'E:\LLVM-full\clang+llvm-22.1.8-x86_64-pc-windows-msvc'

# 或者用 conda-forge 的 llvmdev
pwsh -File build.ps1 -LlvmDir 'E:\Anaconda\envs\tinycompiler\Library' `
                       -FlexBisonDir 'E:\Tools\winflexbison'
```

Linux：

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

### 1.3 使用

```bash
# 编译（从标准输入读源码）
cat test.c | ./build/TinyCompiler -o output.o

# 编译（从文件读源码）
./build/TinyCompiler -o output.o test.c

# 只生成 LLVM IR，不生成目标文件
./build/TinyCompiler -S -o output.ll test.c

# 顺便把语法树打印到屏幕、并导出成 JSON
./build/TinyCompiler --ast --ast-json A_tree.json test.c
```

生成的目标文件可以直接链接：

```bash
clang output.o testmain.cpp -o test
./test
```

### 1.4 跑测试

```powershell
pwsh -File tests/run_tests.ps1 -BuildDir build
```

每个用例包含三个文件：

| 文件 | 作用 |
| --- | --- |
| `tests/xxx.input` | 测试用的源代码 |
| `tests/xxx.expected` | 程序的预期输出（用来自动比对） |
| `tests/xxx.stdin` | 喂给程序的输入（可选） |

脚本会依次完成「生成 IR → 生成目标文件 → 链接成 exe → 运行 → 比对输出」。

---

## 2. 源码结构

```
TinyCompiler/
├── ASTNodes.h / .cpp    抽象语法树的节点定义，以及统一的打印 / JSON 导出
├── CodeGen.h / .cpp     语义分析和中间代码生成（AST → LLVM IR）
├── TypeSystem.h / .cpp  类型系统：类型名 ↔ LLVM 类型，隐式转换规则
├── ObjGen.h / .cpp      目标代码生成（LLVM IR → .o）
├── grammar.y            bison 语法文件
├── token.l              flex 词法文件
├── utils.h / .cpp       错误输出、字符串转义等小工具
├── main.cpp             程序入口
├── CMakeLists.txt       构建脚本
├── build.ps1            Windows 一键构建
└── tests/               测试用例和测试脚本
```

各部分的职责：

```
源代码
  │
  ├─ token.l   ──词法分析──▶  token 流（关键字 / 标识符 / 字面量 / 运算符）
  │
  ├─ grammar.y ──语法分析──▶ 抽象语法树（ASTNodes.h）
  │                          每个节点都有 codeGen() 方法
  │
  ├─ CodeGen.cpp ──语义分析 + 中间代码──▶  LLVM IR
  │                                     （CodeGenContext 维护作用域和符号表）
  │
  └─ ObjGen.cpp ──目标代码──▶  output.o
```

---

## 3. 语言定义

语法是 C 的一个子集，但有几处刻意做了简化：

- 语句末尾不写分号
- 不支持指针类型，但结构体和数组够用
- `if` / `for` / `while` 的 body 必须用大括号
- 数组初始化用中括号而不是大括号：`int[3] a = [1, 2, 3]`
- 强制要求写 `return`

支持的数据类型：`void` `int` `float` `double` `char` `bool` `string`，以及自定义结构体。
数组支持多维。

### 基本语法

```c
int func(int a, int b){
    if( a <= 1 ) {
        return 1
    }else if( a == 2 ){
        return 2
    }else{
        return 0
    }
}

int main(int argc, string[1] argv){
    int i
    for( i = 1 ; i<argc; i=i+1){
        printf("i=%d", i)
    }
    return 0
}
```

### 结构体

```c
struct Point{
    int x
    int y
}

int func(struct Point p){
    return p.x
}
```

### 数组

```c
int testArray(){
    int[13] arr = [1,2,3,4]
    int[3][4] arr2
    int i
    int j
    for(i=0; i<3; i=i+1){
        for(j=0; j<4; j=j+1){
            arr2[i][j] = arr[i*4+j]
        }
    }
    return 0
}
```

### 外部函数

```c
extern int printf(string format)

int main(){
    printf("hello, %d", 42)
    return 0
}
```

---

## 4. 实现要点

这一节是读代码时最值得看的部分。

### 4.1 AST 节点：样板代码只写一次

所有节点都继承自 `Node`。基类统一提供：

| 方法 | 作用 |
| --- | --- |
| `getTypeName()` | 节点类型名，必须实现 |
| `label()` | 节点在树上显示的文本，默认等于类型名 |
| `children()` | 子节点列表，默认空 |
| `codeGen()` | 生成 LLVM IR，默认什么都不生成 |
| `print()` | 打印整棵子树（基类实现） |
| `toJson()` | 导出 JSON（基类实现） |

所以一个新节点通常只要写十几行：

```cpp
class NAssignment : public NExpression {
public:
    std::shared_ptr<NIdentifier> lhs;
    std::shared_ptr<NExpression> rhs;

    const char* getTypeName() const override { return "NAssignment"; }
    std::vector<Node*> children() const override {
        return {lhs.get(), rhs.get()};
    }
    llvm::Value* codeGen(CodeGenContext& context) override;
};
```

`print()` 和 `toJson()` 都基于 `children()` 在基类里实现一次，所以
**20 个节点类省掉了 200 多行重复代码**。

### 4.2 类型引用单独成节点

变量名和类型名都是「名字」，但语义完全不同。早期版本用一个 `NIdentifier`
加 `isType` / `isArray` 两个布尔标志位来区分，代码里到处都要判断这两个标志，
很容易漏。

现在拆成两个节点：

- `NIdentifier` —— 只表示「一个名字」，用于变量引用、函数名、结构体成员名
- `NTypeRef` —— 表示「一个类型」，带名字和数组维度

```cpp
class NTypeRef : public Node {
public:
    std::string name;              // "int" / "Point"
    std::vector<uint64_t> dims;    // 数组各维长度，空表示不是数组

    bool isArray() const { return !dims.empty(); }
    uint64_t elementCount() const;  // 各维相乘
};
```

好处是「类型」变成了编译器的第一类概念，类型系统不再需要去猜。

### 4.3 作用域与符号表

`CodeGenContext` 里维护一个作用域栈，每层作用域一张表：

```cpp
struct Symbol {
    llvm::Value* addr = nullptr;       // 标量：存放变量值的栈空间地址
    llvm::Value* arrayBase = nullptr;  // 数组：指向首元素的指针
    llvm::Type*  type = nullptr;       // 标量类型 / 数组元素类型
    std::string  typeName;             // 源语言里的类型名
    std::vector<uint64_t> dims;        // 数组维度，空表示不是数组
};
```

查找变量时从最内层作用域往外找，符合 C 的遮蔽规则：

```cpp
Symbol* CodeGenContext::findSymbol(const std::string& name) {
    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
        auto it = scope->symbols.find(name);
        if (it != scope->symbols.end()) {
            return &it->second;
        }
    }
    return nullptr;
}
```

### 4.4 数组：统一按「首元素指针」处理

数组在 C 里有一个容易忽略的性质：**数组名会退化成指向首元素的指针**。

本项目把这件事显式地存进了符号表：

- 局部数组 `int[13] arr` → 分配 `[13 x i32]`，`arrayBase` 指向首元素
- 数组形参 `int arr[10]` → 本来就是指针，`arrayBase` 直接用传进来的值

于是取元素的代码只有一种写法：

```cpp
llvm::Value* index = flatIndex(ctx, *symbol, this->exprs);   // 算出下标
llvm::Value* elementPtr = createGEP(ctx, symbol->type, symbol->arrayBase, {index});
return ctx.builder.CreateLoad(symbol->type, elementPtr, "elem");
```

多维数组在内存里展平成一维（和 C 的行主序一致）。下标换算：

```
int[2][3][4] cube        →  展平成 [24 x i32]
cube[i][j][k]            →  下标 = ((i * 3) + j) * 4 + k
```

代码里用统一的公式算，因此任意维数都正确：

```cpp
// 下标 = ((e0 * d1) + e1) * d2 + e2 ...
Value* result = nullptr;
for (size_t i = 0; i < exprs.size(); ++i) {
    Value* element = exprs[i]->codeGen(ctx);
    if (!result) { result = element; continue; }
    Value* stride = ConstantInt::get(intType, symbol.dims[i]);
    result = builder.CreateAdd(builder.CreateMul(result, stride), element);
}
```

### 4.5 类型转换

`TypeSystem::cast()` 负责所有隐式类型转换，规则很简单：

| 源类型 | 目标类型 | 转换 |
| --- | --- | --- |
| 整数 | 整数 | 位宽变窄用 `trunc`，变宽用 `sext` |
| 整数 | 布尔 | `icmp ne 0`（**不能直接截断**，否则 `2` 会变成假） |
| 整数 | 浮点 | `sitofp`（有符号！用 `uitofp` 会把负数变成很大的正数） |
| 浮点 | 整数 | `fptosi` |
| 浮点 | 浮点 | `fpext` / `fptrunc` |

二元运算前会先把两个操作数统一到同一种类型：只要有一边是浮点就都提升到
`double`，两边都是整数就统一到较宽的那个（至少 32 位）。

### 4.6 基本块的生成

`if` 语句生成三个基本块：`then` / `else` / `ifcont`（合并块）。

```cpp
thenBlock  = BasicBlock::Create(ctx.llvmContext, "then", function);
elseBlock  = BasicBlock::Create(ctx.llvmContext, "else", function);
mergeBlock = BasicBlock::Create(ctx.llvmContext, "ifcont", function);

builder.CreateCondBr(condition, thenBlock, elseBlock ? elseBlock : mergeBlock);
// ... 分别生成 then / else 分支 ...
builder.CreateBr(mergeBlock);
```

两个容易踩的坑：

1. **每个分支生成完都要判断当前块有没有终结指令**。如果分支里有 `return`，
   块已经有终结指令了，再 `CreateBr` 会直接触发 LLVM 的断言。
2. **合并块要挪到函数块列表的末尾**。分支里如果还有嵌套的 `if`，它们会继续
   创建新块，不挪一下的话块在列表里的顺序就和执行顺序对不上了。

`for` 循环用的是「旋转后」的经典结构，条件每轮只求值一次：

```
entry:      初始化
            br for.cond
for.cond:   条件 → br for.body / for.end
for.body:   循环体 → 递增 → br for.cond
for.end:
```

### 4.7 目标文件生成

`ObjGen.cpp` 用的是 LLVM 的 **legacy PassManager**（和 `llc` 工具默认的做法一致）：

```cpp
llvm::legacy::PassManager passManager;
passManager.add(new llvm::TargetLibraryInfoWrapperPass(targetLibraryInfo));
passManager.add(new llvm::RuntimeLibraryInfoWrapper(...));

machine->addPassesToEmitFile(passManager, out, nullptr,
                             llvm::CodeGenFileType::ObjectFile, true,
                             machineModuleInfo);

passManager.run(*context.theModule);
```

> LLVM 22 里虽然新增了基于新 PassManager 的 `TargetMachine::buildCodeGenPipeline`，
> 但 **X86 后端的 AsmPrinter 还没有移植过去**（`X86CodeGenPassBuilder::addAsmPrinter`
> 里还写着 `// TODO: Add AsmPrinter.`），走新 PassManager 会生成不出目标文件。
> 所以这里仍然用 legacy 方式，`llc` 自己也是默认这么做的。

另外注意 LLVM 15 之后默认开启了 **opaque pointer**，指针不再带元素类型：

- `CreateInBoundsGEP` 必须显式给元素类型：`CreateInBoundsGEP(元素类型, 指针, 下标)`
- `CreateLoad` 同理：`CreateLoad(元素类型, 指针)`
- `Type::getInt8PtrTy()` 已被删除，改用 `PointerType::getUnqual(Type::getInt8Ty(ctx))`

### 4.8 字符串字面量

字符串字面量在 LLVM 里是一个 `[N x i8]` 的全局常量，要当成 `char*` 用必须
先取首元素的指针：

```cpp
llvm::GlobalVariable* global = builder.CreateGlobalString(this->value, "str");
std::vector<llvm::Value*> indices{zero(ctx), zero(ctx)};
return createGEP(ctx, global->getValueType(), global, indices);
```

引号和转义序列在**词法分析阶段**就处理掉（`UnescapeStringLiteral`），
语法树里存的是真正的字符串内容。

### 4.9 错误处理

编译器里所有的失败路径都走同一个出口 `LogError()`，它会做两件事：

1. 把错误信息打到 **stderr**（正常输出走 stdout，两者不会混在一起）
2. 给错误计数加一

编译结束时检查这个计数：

```cpp
if (ErrorCount() > 0) {
    fprintf(stderr, "error: 编译过程中有 %d 处错误\n", ErrorCount());
    return 1;
}
```

所以**只要有错误，就不会生成目标文件，退出码也一定是非 0**。
脚本可以直接靠退出码判断编译是否成功，不用去解析输出。

```
$ TinyCompiler -o out.obj bad.input
error: 未定义的变量：undefinedVar
$ echo $?
1
```

这样做的好处是：代码生成里任何一步失败都只是「返回 nullptr + 记一个错」，
不会像以前那样直接访问空指针把程序带崩。

---

## 5. 测试

```powershell
pwsh -File tests/run_tests.ps1 -BuildDir build
```

输出：

```
[PASS] testArray
[PASS] testBasic
[PASS] testBugfix
[PASS] testExtern
[PASS] testStruct

通过 5 个，失败 0 个，跳过 0 个
```

`tests/testBugfix.input` 专门用来锁住那些曾经出过问题的行为，改动代码生成
逻辑时记得跑一遍。

> **Windows 上链接 `scanf` 的注意事项**：MSVC 的运行库把 `scanf` / `fopen` 这些
> 「老名字」单独放在 `legacy_stdio_definitions.lib` 里（clang 会自动改写成
> `__stdio_common_vfscanf`）。本编译器生成的目标文件是直接引用 `scanf` 的，
> 所以链接时需要带上这个库：
>
> ```
> clang output.o legacy_stdio_definitions.lib -o test
> ```
>
> `tests/run_tests.ps1` 已经自动处理了这一点。

---

## 6. 历史上修过的问题

这一节列的是从 LLVM 3.9 时代遗留到现在的问题，也是这份代码里最有意思的部分。
每一条都有对应的回归测试（见 `tests/testBugfix.input`）。

### 语义错误

| 问题 | 原因 |
| --- | --- |
| `return` 写在 `if` / `for` 里面不生效 | `NReturnStatement` 只记录了返回值，没有生成 `ret` 指令 |
| `a < b` 对负数判断错误 | `<` 和 `>` 用了无符号比较（`icmp ult`），而 `<=` `>=` 用的是有符号 |
| `if (2)` 判断为假 | 转成布尔时把整数截断成了 1 位，`2` 变成 `0` |
| `a + b` 在负数上出错 | 整数转浮点用了 `uitofp`（无符号），负数会变成巨大的正数 |
| `a % b` 报「未知运算符」 | 语法和词法都支持取模，但代码生成里漏了 `srem` |
| 三维及以上的数组下标算错 | 下标换算公式只对两维正确 |
| 调用后面才定义的函数会崩溃 | 语法分析阶段没有做函数原型收集 |
| 浮点变量参与运算生成非法 IR | 代码生成只认 `double`，不认 `float` |

### 崩溃

| 问题 | 原因 |
| --- | --- |
| 引用未定义的变量就崩 | 先解引用后判空 |
| 调用不存在的函数就崩 | 拿到 `nullptr` 后直接调 `arg_size()` |
| `else` 分支里的变量作用域错乱 | 压栈时压的是 `then` 块而不是 `else` 块 |
| `if/else` 里都有 `return` 就崩 | 给已经有终结指令的块又加了一条 `br` |
| 一元负号让程序崩溃 | 语法规则返回了 `nullptr` |

### 生成非法 IR

| 问题 | 原因 |
| --- | --- |
| 字符串字面量不能当 `char*` 传 | 传的是 `[N x i8]*`，而不是首元素的指针 |
| 字符串字面量带着引号 | 词法分析没有剥掉两端的引号 |
| `\n` 原样输出 | 没有处理转义序列 |
| 字符串字面量每次都重新生成 | 缺少缓存，同一个字符串会产生多个全局变量 |

### 代码冗余

| 问题 | 处理 |
| --- | --- |
| 20 个节点类各写一遍 `print()` 和 `jsonGen()` | 抽到基类，子类只提供 `children()` |
| 依赖 jsoncpp | 换成 40 行手写的 JSON 输出，少一个外部依赖 |
| 作用域里 4 张并行的 map | 合并成一张 `Symbol` 表 |
| `shared_ptr<vector>` | 直接用 `vector` |
| 类型信息混在 `NIdentifier` 里 | 拆出独立的 `NTypeRef` |
| 词法分析每读一个 token 就 `puts` 一次 | 删掉，改成只在出错时提示 |
| `utils.cpp` 里 80 行全是注释掉的死代码 | 删除 |

---

## 7. 参考资料

- [LLVM Language Reference Manual](https://llvm.org/docs/LangRef.html)
- [LLVM 官方文档](https://llvm.org/docs/)
- [bison 手册](https://www.gnu.org/software/bison/manual/)
- [flex 手册](https://westes.appspot.com/flex/manual/)
- [Writing a Compiler in C](https://github.com/neovim/neovim/wiki/Implementing-a-Compiler-in-C)
