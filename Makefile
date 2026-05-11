# ============================================================
#  SmartClip  -  Windows Native Clipboard Manager
#  Toolchain : MinGW (mingw32-make)
#  Layout    :
#     src/        C++ sources
#     include/    headers
#     resources/  resource.rc + icons + images/
#     build/      intermediate artifacts (gitignored)
#  Usage     :
#     mingw32-make              # release build (default)
#     mingw32-make debug        # debug build
#     mingw32-make run          # build and run
#     mingw32-make clean        # remove build artifacts
#     mingw32-make rebuild      # clean + build
#     mingw32-make -j8          # parallel build
# ============================================================

# ---- 基本信息 ----
TARGET   := SmartClip_Native.exe
SRCDIR   := src
INCDIR   := include
RESDIR   := resources
BUILDDIR := build

# ---- Shell (强制 cmd,避免从 bash 调用时命中 sh 导致语法错) ----
SHELL := cmd.exe
.SHELLFLAGS := /C

# ---- 工具链 ----
CXX     := g++
WINDRES := windres

# ---- 源文件 ----
SRCS := \
    $(SRCDIR)/smartclip.cpp         \
    $(SRCDIR)/image_handler.cpp     \
    $(SRCDIR)/history.cpp           \
    $(SRCDIR)/hotkey.cpp            \
    $(SRCDIR)/card_renderer.cpp     \
    $(SRCDIR)/tray.cpp              \
    $(SRCDIR)/settings.cpp          \
    $(SRCDIR)/search.cpp            \
    $(SRCDIR)/text_utils.cpp        \
    $(SRCDIR)/graphics_utils.cpp    \
    $(SRCDIR)/password_vault.cpp    \
    $(SRCDIR)/password_panel.cpp    \
    $(SRCDIR)/tag_popup.cpp         \
    $(SRCDIR)/themed_dialog.cpp     \
    $(SRCDIR)/theme.cpp             \
    $(SRCDIR)/i18n.cpp

RC   := $(RESDIR)/resource.rc
RES  := $(BUILDDIR)/resource.res
OBJS := $(patsubst $(SRCDIR)/%.cpp,$(BUILDDIR)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

# ---- 编译 / 链接选项 ----
CPPFLAGS := -DUNICODE -D_UNICODE -D_WIN32_IE=0x0500 -I$(INCDIR)
CXXFLAGS := -std=c++11 -Wall -Wextra -MMD -MP -municode
LDFLAGS  := -mwindows -municode -static-libgcc -static-libstdc++
LDLIBS   := -luser32 -lgdi32 -lcomctl32 -lpsapi -lshell32 -lwinmm \
            -lole32 -loleaut32 -lgdiplus -ldwmapi -lshlwapi -luxtheme -luuid \
            -lmsimg32 -lcrypt32 -lruntimeobject

# ---- 构建模式 (release / debug) ----
BUILD ?= release
ifeq ($(BUILD),debug)
    CXXFLAGS += -O0 -g -DDEBUG
else
    CXXFLAGS += -O2 -DNDEBUG
endif

# ============================================================
#  构建规则
# ============================================================
.PHONY: all debug release run clean rebuild
.DEFAULT_GOAL := all

all: $(TARGET)

release:
	@$(MAKE) --no-print-directory BUILD=release all

debug:
	@$(MAKE) --no-print-directory BUILD=debug all

# 链接
$(TARGET): $(OBJS) $(RES)
	@echo [LINK] $@
	@$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# 编译 .cpp -> build/xxx.o (自动生成头文件依赖 .d)
$(BUILDDIR)/%.o: $(SRCDIR)/%.cpp | $(BUILDDIR)
	@echo [CXX]  $<
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

# 编译资源文件
$(RES): $(RC) $(INCDIR)/resource.h | $(BUILDDIR)
	@echo [RC]   $<
	@$(WINDRES) --include-dir $(INCDIR) --include-dir $(RESDIR) -i $< -O coff -o $@

# 创建 build 目录
$(BUILDDIR):
	@if not exist "$(BUILDDIR)" mkdir "$(BUILDDIR)"

# 运行
run: $(TARGET)
	@echo [RUN]  $(TARGET)
	@./$(TARGET)

# 清理
clean:
	@if exist "$(BUILDDIR)" rmdir /S /Q "$(BUILDDIR)"
	@if exist "$(TARGET)"   del /Q /F "$(TARGET)"
	@echo [CLEAN] done

rebuild: clean all

# 自动引入头文件依赖
-include $(DEPS)
