# 这是一个包装脚本，真正的构建逻辑在 CMakeLists.txt 里。
# 直接用 cmake 也可以：
#
#   cmake -B build -S . && cmake --build build
#
# 需要先准备好 LLVM（含 C++ 头文件）、flex 和 bison。

BUILD_DIR ?= build
BUILD_TYPE ?= Release

.PHONY: all configure build test clean run

all: build

configure:
	cmake -B $(BUILD_DIR) -S . -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)

build: configure
	cmake --build $(BUILD_DIR) --config $(BUILD_TYPE)

# 用 tests/ 下的用例跑一遍
test: build
	cmake --build $(BUILD_DIR) --config $(BUILD_TYPE) --target test

run: build
	cmake --build $(BUILD_DIR) --config $(BUILD_TYPE) --target run

clean:
	cmake --build $(BUILD_DIR) --config $(BUILD_TYPE) --target clean
	rm -rf $(BUILD_DIR)
