# Makefile for CRC Library
# 不使用查表法的 CRC 算法库

# 编译器和选项
CC      = gcc
CFLAGS  = -Wall -Wextra -std=c23 -pedantic -O2
LDFLAGS =

# 存档工具
AR      = ar
RANLIB  = ranlib

# 目录
SRC_DIR   = src
INC_DIR   = include
TEST_DIR  = tests
BUILD_DIR = build
BIN_DIR   = $(BUILD_DIR)/bin
PREFIX    = /usr/local

# 源文件
LIB_SOURCES = $(SRC_DIR)/crc.c
LIB_SOURCES += $(SRC_DIR)/crc_api.c

# 头文件
LIB_HEADERS = $(INC_DIR)/crc.h
LIB_HEADERS += $(INC_DIR)/crc_api.h

# 测试文件
TEST_SOURCE = $(TEST_DIR)/test_crc.c

# 目标文件
LIB_OBJECTS = $(BUILD_DIR)/crc.o $(BUILD_DIR)/crc_api.o
TEST_OBJECT = $(BUILD_DIR)/test_crc.o
LIB_TARGET  = $(BUILD_DIR)/libcrc.a
TEST_TARGET = $(BIN_DIR)/test_crc

# 默认目标
.PHONY: all lib test runtest clean help install

all: lib $(TEST_TARGET)

# ============================================================================
# 编译规则
# ============================================================================

# 创建构建目录
$(BUILD_DIR) $(BIN_DIR):
	@mkdir -p $@

# 编译目标文件
$(BUILD_DIR)/crc.o: $(SRC_DIR)/crc.c $(INC_DIR)/crc.h | $(BUILD_DIR)
	@echo "  CC $< -c -o $@"
	@$(CC) $(CFLAGS) -c $< -o $@ -I$(INC_DIR)

$(BUILD_DIR)/crc_api.o: $(SRC_DIR)/crc_api.c $(INC_DIR)/crc_api.h $(INC_DIR)/crc.h | $(BUILD_DIR)
	@echo "  CC $< -c -o $@"
	@$(CC) $(CFLAGS) -c $< -o $@ -I$(INC_DIR)

# 编译测试文件
$(TEST_OBJECT): $(TEST_SOURCE) $(LIB_HEADERS) | $(BUILD_DIR)
	@echo "  CC $< -c -o $@"
	@$(CC) $(CFLAGS) -c $< -o $@ -I$(INC_DIR)

# 创建静态库
$(LIB_TARGET): $(LIB_OBJECTS) | $(BUILD_DIR)
	@echo "  AR rcs $@"
	@$(AR) rcs $@ $^
	@$(RANLIB) $@
	@echo "  Library: $@"

# 链接测试程序
$(TEST_TARGET): $(TEST_OBJECT) $(LIB_TARGET) | $(BIN_DIR)
	@echo "  LD -o $@"
	@$(CC) $(LDFLAGS) -o $@ $^
	@echo "  Test: $@"

# ============================================================================
# 特殊目标
# ============================================================================

# 仅编译库
lib: $(LIB_TARGET)

# 运行测试
runtest: $(TEST_TARGET)
	@echo "Running tests..."
	@$(TEST_TARGET)

# 清理构建文件
clean:
	@echo "  RM $(BUILD_DIR)"
	@rm -rf $(BUILD_DIR)
	@echo "  Clean"

# 安装
install: all
	@echo "  INSTALL to $(PREFIX)"
	@mkdir -p $(PREFIX)/lib
	@mkdir -p $(PREFIX)/include
	@if [ -f $(LIB_TARGET) ]; then \
		install -m 0644 $(LIB_TARGET) $(PREFIX)/lib/; \
	fi
	@install -m 0644 $(LIB_HEADERS) $(PREFIX)/include/
	@echo "  Done"

# 帮助信息
help:
	@echo "CRC Library - Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  all       - Build library and test (default)"
	@echo "  lib       - Build library only"
	@echo "  runtest   - Build and run test"
	@echo "  clean     - Remove build artifacts"
	@echo "  install   - Install library and headers"
	@echo ""
	@echo "Directories:"
	@echo "  SRC_DIR   = $(SRC_DIR)"
	@echo "  INC_DIR   = $(INC_DIR)"
	@echo "  TEST_DIR  = $(TEST_DIR)"
	@echo "  BUILD_DIR = $(BUILD_DIR)"
	@echo ""
	@echo "Install prefix:"
	@echo "  PREFIX    = $(PREFIX)"
