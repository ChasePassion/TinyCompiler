cmake_minimum_required(VERSION 3.20)
project(TinyCompiler CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ---------------------------------------------------------------------------
# LLVM
#
# 优先使用 LLVMConfig.cmake（conda / vcpkg / 官方安装包都提供），
# 找不到时退回 llvm-config。
# ---------------------------------------------------------------------------
find_package(LLVM REQUIRED CONFIG)
message(STATUS "使用 LLVM ${LLVM_PACKAGE_VERSION} (${LLVM_DIR})")

# ---------------------------------------------------------------------------
# flex / bison
# ---------------------------------------------------------------------------
find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)
message(STATUS "使用 bison ${BISON_VERSION} (${BISON_EXECUTABLE})")
message(STATUS "使用 flex ${FLEX_VERSION} (${FLEX_EXECUTABLE})")

set(GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GENERATED_DIR})

# bison: grammar.y -> grammar.cpp + grammar.hpp
bison_target(Grammar ${CMAKE_CURRENT_SOURCE_DIR}/grammar.y ${GENERATED_DIR}/grammar.cpp)
set(BISON_OUTPUT ${GENERATED_DIR}/grammar.cpp)
# flex: token.l -> token.cpp
flex_target(Token ${CMAKE_CURRENT_SOURCE_DIR}/token.l ${GENERATED_DIR}/token.cpp)
set(FLEX_OUTPUT ${GENERATED_DIR}/token.cpp)

# 源码按教材第 1 章图 1.1 的模块划分组织：
#   ASTNodes      抽象语法树（第 6 章）
#   IntermGen     语义分析和中间代码产生（第 7 章）
#   Quadruple     中间代码：四元式（第 7 章 7.1.3）
#   TypeSystem    类型系统（第 7 章 7.7）
#   SymbolTable   符号表（第 8 章）
#   Optimizer     中间代码优化（第 10 章）
#   Backend       目标代码生成（四元式 -> LLVM IR）
#   ObjGen        目标文件生成（LLVM IR -> .o）
add_executable(TinyCompiler
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ASTNodes.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/IntermGen.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Quadruple.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TypeSystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SymbolTable.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Optimizer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Backend.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ObjGen.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/utils.cpp
    ${BISON_OUTPUT}
    ${FLEX_OUTPUT}
)

target_include_directories(TinyCompiler PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${GENERATED_DIR}
    ${LLVM_INCLUDE_DIRS}
)

# LLVM_DEFINITIONS 里是 -DXXX 形式，去掉前缀才能传给 target_compile_definitions
foreach(definition ${LLVM_DEFINITIONS})
    string(REGEX REPLACE "^-D" "" definition ${definition})
    list(APPEND LLVM_DEFINITIONS_CLEAN ${definition})
endforeach()
target_compile_definitions(TinyCompiler PRIVATE ${LLVM_DEFINITIONS_CLEAN})

# 链接 LLVM。
#
# 注意这里不要用 LLVMConfig.cmake 导出的 LLVM 伞形目标：它把 clang / lld
# 也算进来，而那些库引用了 DIA SDK（只有完整版 Visual Studio 才装）。
# 只链真正用到的组件就没这个问题。
llvm_map_components_to_libnames(LLVM_LIBS core support irreader passes native)

# LLVM 官方发布的 Windows 包里，LLVMDebugInfoCodeView 的接口带了一个
# 「构建 LLVM 的那台机器上」的 DIA SDK 绝对路径，换台机器就找不到这个文件了
# （实际上 LLVM 的库并不真的用到 DIA 的符号）。这里把指向不存在文件的
# 绝对路径去掉。
foreach(_llvm_lib LLVMDebugInfoCodeView LLVMDebugInfoPDB)
    if(TARGET ${_llvm_lib})
        get_target_property(_llvm_iface ${_llvm_lib} INTERFACE_LINK_LIBRARIES)
        if(_llvm_iface)
            set(_llvm_fixed "")
            foreach(_dep ${_llvm_iface})
                if(_dep MATCHES "^[A-Za-z]:/" AND NOT EXISTS "${_dep}")
                    message(STATUS "去掉指向不存在文件的依赖: ${_dep}")
                else()
                    list(APPEND _llvm_fixed ${_dep})
                endif()
            endforeach()
            set_target_properties(${_llvm_lib} PROPERTIES
                                  INTERFACE_LINK_LIBRARIES "${_llvm_fixed}")
        endif()
    endif()
endforeach()

target_link_libraries(TinyCompiler PRIVATE ${LLVM_LIBS})

# Windows 下 LLVM 的静态库依赖几个系统库
if(WIN32)
    target_link_libraries(TinyCompiler PRIVATE ws2_32 advapi32 userenv)
endif()

# MSVC 默认按系统 ANSI 码页读源文件，中文注释会被误解导致解析错误，
# 必须显式指定源文件是 UTF-8。
if(MSVC)
    target_compile_options(TinyCompiler PRIVATE /utf-8 /W3)
endif()

# LLVM 依赖 zlib / zstd 的动态库。把它们复制到可执行文件旁边，
# 这样不额外设置 PATH 也能直接运行 TinyCompiler。
if(WIN32)
    # LLVM_DIR 指向 <prefix>/lib/cmake/llvm，往上三级就是 <prefix>
    get_filename_component(LLVM_PREFIX "${LLVM_DIR}/../../.." ABSOLUTE)
    foreach(dll zlib zstd)
        set(candidate "${LLVM_PREFIX}/bin/${dll}.dll")
        if(EXISTS "${candidate}")
            add_custom_command(TARGET TinyCompiler POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${candidate}" "$<TARGET_FILE_DIR:TinyCompiler>")
        endif()
    endforeach()
endif()

# ---------------------------------------------------------------------------
# 便捷目标：
#   cmake --build build --target run      用 test.input 跑一遍
# ---------------------------------------------------------------------------
add_custom_target(run
    COMMAND ${CMAKE_COMMAND} -E echo "编译 tests/testBasic.input"
    COMMAND TinyCompiler -o ${CMAKE_CURRENT_BINARY_DIR}/output.o
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/testBasic.input
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS TinyCompiler
    COMMENT "用测试用例编译一遍"
)
