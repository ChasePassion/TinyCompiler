# 这是一个包装脚本，真正的构建逻辑在 CMakeLists.txt 里。
# 直接用 cmake 也可以：
#
#   cmake -B build -S . && cmake --build build
#
# Windows 上更推荐用 build.ps1（会自动加载 MSVC 环境并找到 LLVM / flex / bison）：
#
#   pwsh -File build.ps1
#
# Linux 上需要自己保证 LLVM（含 C++ 头文件）、flex、bison 都在。

BUILD_DIR ?= build
BUILD_TYPE ?= Release

.PHONY: all configure build test run clean

all: build

configure:
	cmake -B $(BUILD_DIR) -S . -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)

build: configure
	cmake --build $(BUILD_DIR) --config $(BUILD_TYPE)

# 跑测试用例：生成 IR -> 生成目标文件 -> 链接 -> 运行 -> 比对输出
test: build
	pwsh -File tests/run_tests.ps1 -BuildDir $(BUILD_DIR)

# 用 tests/testBasic.input 编译一遍，看看生成的 IR
run: build
	cmake --build $(BUILD_DIR) --config $(BUILD_TYPE) --target run

clean:
	cmake --build $(BUILD_DIR) --config $(BUILD_TYPE) --target clean
	cmake -E rm -rf $(BUILD_DIR)
