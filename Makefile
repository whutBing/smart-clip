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

# ---- 第三方库 ----
SQLITE_DIR := third_party/sqlite

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
    $(SRCDIR)/tag_popup.cpp         \
    $(SRCDIR)/themed_dialog.cpp     \
    $(SRCDIR)/theme.cpp             \
    $(SRCDIR)/i18n.cpp              \
    $(SRCDIR)/scrollbar.cpp         \
    $(SRCDIR)/drag_drop.cpp         \
    $(SRCDIR)/listbox_handler.cpp   \
    $(SRCDIR)/ui_state.cpp

# SQLite amalgamation（C 源码，单独编译）
SQLITE_SRC := $(SQLITE_DIR)/sqlite3.c
SQLITE_OBJ := $(BUILDDIR)/sqlite3.o

RC   := $(RESDIR)/resource.rc
RES  := $(BUILDDIR)/resource.res
OBJS := $(patsubst $(SRCDIR)/%.cpp,$(BUILDDIR)/%.o,$(SRCS)) $(SQLITE_OBJ)
DEPS := $(patsubst $(SRCDIR)/%.cpp,$(BUILDDIR)/%.d,$(SRCS))

# ---- 编译 / 链接选项 ----
CPPFLAGS := -DUNICODE -D_UNICODE -D_WIN32_IE=0x0500 -I$(INCDIR) -I$(SRCDIR) -I$(SQLITE_DIR)
CXXFLAGS := -std=c++11 -Wall -Wextra -MMD -MP -municode
# SQLite 编译选项：线程安全、禁用扩展加载、禁用内存统计以减小体积
SQLITE_CFLAGS := -DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION \
                 -DSQLITE_DEFAULT_MEMSTATUS=0 -DSQLITE_ENABLE_FTS5 \
                 -DSQLITE_OMIT_DEPRECATED -Os -DNDEBUG
LDFLAGS  := -mwindows -municode -static-libgcc -static-libstdc++ -static -s
LDLIBS   := -luser32 -lgdi32 -lcomctl32 -lpsapi -lshell32 -lwinmm \
            -lole32 -loleaut32 -lgdiplus -ldwmapi -lshlwapi -luxtheme -luuid \
            -lmsimg32 -lcrypt32 -lruntimeobject -ld2d1 -ldwrite

# ---- 构建模式 (release / debug) ----
BUILD ?= release
ifeq ($(BUILD),debug)
    CXXFLAGS += -O0 -g -DDEBUG
else
    CXXFLAGS += -Os -DNDEBUG
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

# 编译 SQLite amalgamation（C 源码，用 gcc 编译以获得最佳兼容性）
$(SQLITE_OBJ): $(SQLITE_SRC) | $(BUILDDIR)
	@echo [CC]   $<
	@gcc $(SQLITE_CFLAGS) -c $< -o $@

# 编译 .cpp -> build/xxx.o (自动生成头文件依赖 .d)
$(BUILDDIR)/%.o: $(SRCDIR)/%.cpp | $(BUILDDIR)
	@echo [CXX]  $<
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

# 编译资源文件
# 注意：windres 不会自动检测 .rc 引用的图片/图标文件变化，
# 必须显式将所有资源素材列入依赖，否则更换图片后增量编译不会
# 重新生成 .res，导致 EXE 里嵌入的还是旧图片。
RESOURCE_ASSETS := $(RESDIR)/clip.ico $(RESDIR)/app.manifest \
                   $(wildcard $(RESDIR)/images/*.png) \
                   $(wildcard $(RESDIR)/images/*.ico) \
                   $(wildcard $(RESDIR)/images/*.bmp)
$(RES): $(RC) $(INCDIR)/resource.h $(INCDIR)/version.h $(RESOURCE_ASSETS) | $(BUILDDIR)
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
