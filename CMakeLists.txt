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

add_executable(TinyCompiler
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ASTNodes.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/CodeGen.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TypeSystem.cpp
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

# 链接 LLVM。优先用 LLVMConfig.cmake 提供的导出目标
if(TARGET LLVM)
    target_link_libraries(TinyCompiler PRIVATE LLVM)
else()
    # core/support/irreader/passes 是编译器核心，native 是本机目标的后端（x86）
    llvm_map_components_to_libnames(LLVM_LIBS core support irreader passes native)
    target_link_libraries(TinyCompiler PRIVATE ${LLVM_LIBS})
endif()

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
