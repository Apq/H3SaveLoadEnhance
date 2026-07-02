// SaveLoadEnhance.cpp
// 英雄无敌3 SoD HD Mod 插件「人性化读档」。
// 功能:玩家手动存档或真正执行读档后,记录该存档名(仅进程内内存);
//       下次打开存档/读档界面时,自动把默认选中项移到该存档所在行。
// 启停由 HD Mod 启动器插件列表控制;只要 DLL 被加载即视为启用,不使用 ini。

#define _H3API_PATCHER_X86_
#include <H3API.hpp>

using namespace h3;

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

Patcher*         _P  = nullptr;
PatcherInstance* _PI = nullptr;

// ========== 进程内状态 ==========

static struct LastManualSaveOrLoadState {
    char file_name[MAX_PATH];
    char source[64];
} last_state;

static char g_log_path[MAX_PATH];
static wchar_t g_wlog_path[MAX_PATH * 2];
static bool  g_inside_auto_select_refresh = false;
static bool  g_save_dialog_visible = false;  // DialogShow 时设置，析构时清除
static bool  g_save_dialog_recently_closed = false;  // 存档窗口关闭后，等待后续 SaveGame 确认
static DWORD g_save_dialog_recent_until = 0;

static const DWORD SAVE_DIALOG_RECENT_MS = 3000;

static const int MAX_LOG_FILES_TO_KEEP = 30;   // 插件加载时仅保留最近 30 个本插件日志。
static const int MAX_LOG_FILES_TO_SCAN = 1024; // 防御上限。

static const DWORD ADDR_H3MAIN_SAVE_GAME  = 0x4BEB60; // H3Main::SaveGame
static const DWORD ADDR_DIALOG_SHOW = 0x584EF4;         // 对话框显示/消息循环入口内(ESI=对话框对象,消息循环之前)
static const DWORD ADDR_DIALOG_DESTRUCTOR = 0x583E10;   // 对话框析构函数入口(ECX=对话框对象,数据尚完整)
static const DWORD ADDR_LOAD_UPDATE_SELECTED = 0x5857D0; // 原生"应用选择项"函数
static const DWORD ADDR_DIALOG_REDRAW = 0x584820;       // 原生整窗重绘函数

// 显示数组与控件字段偏移。
static const int   LOAD_OBJ_DISP_BEGIN    = 0x1054;
static const int   LOAD_OBJ_DISP_END      = 0x1058;
static const int   LOAD_OBJ_SELECTED      = 0x374;
static const int   LOAD_OBJ_TOPINDEX      = 0x370;
static const int   LOAD_VISIBLE_ROWS      = 18;
static const int   SAVE_VISIBLE_ROWS      = 16;
static const int   LOAD_ENTRY_SIZE        = 0xCA4;
static const int   LOAD_ENTRY_NAME_OFFSET = 0x33D;
static const int   LOAD_OBJ_SCROLLBAR_LIST = 0x183C;
static const int   SCROLLBAR_TICK          = 0x3C;
static const int   SCROLLBAR_TICKS_COUNT   = 0x48;
static const int   LOAD_OBJ_MODE_LOAD     = 0x64;
static const int   LOAD_OBJ_MODE_SAVE     = 0x65;

// 日志文件条目;用于按最后写入时间清理旧日志。
struct LogFileEntryW {
    wchar_t path[MAX_PATH * 2];
    FILETIME last_write;
};

static int __cdecl CompareLogFileEntryW(const void* a, const void* b)
{
    const LogFileEntryW* la = (const LogFileEntryW*)a;
    const LogFileEntryW* lb = (const LogFileEntryW*)b;
    int cmp = CompareFileTime(&la->last_write, &lb->last_write);
    if (cmp != 0) return cmp;
    return _wcsicmp(la->path, lb->path);
}

static void SplitModulePathForLogW(const wchar_t* module_path, wchar_t* out_dir, int out_dir_size, wchar_t* out_base, int out_base_size)
{
    if (out_dir && out_dir_size > 0) out_dir[0] = 0;
    if (out_base && out_base_size > 0) out_base[0] = 0;
    if (!module_path || !module_path[0]) return;

    const wchar_t* slash1 = wcsrchr(module_path, L'\\');
    const wchar_t* slash2 = wcsrchr(module_path, L'/');
    const wchar_t* slash = slash1 > slash2 ? slash1 : slash2;
    const wchar_t* name = slash ? slash + 1 : module_path;

    if (out_dir && out_dir_size > 0) {
        if (slash) {
            int len = int(slash - module_path);
            if (len >= out_dir_size) len = out_dir_size - 1;
            memcpy(out_dir, module_path, len * sizeof(wchar_t));
            out_dir[len] = 0;
        } else {
            wcsncpy_s(out_dir, out_dir_size, L".", _TRUNCATE);
        }
    }

    if (out_base && out_base_size > 0) {
        wcsncpy_s(out_base, out_base_size, name, _TRUNCATE);
        wchar_t* dot = wcsrchr(out_base, L'.');
        if (dot) *dot = 0;
    }
}

static void CleanupOldLogFilesW(const wchar_t* log_dir, const wchar_t* log_base, const wchar_t* current_log_path)
{
    if (!log_dir || !log_dir[0] || !log_base || !log_base[0]) return;

    wchar_t pattern[MAX_PATH * 2];
    _snwprintf_s(pattern, _countof(pattern), _TRUNCATE, L"%s\\%s_*.log", log_dir, log_base);

    LogFileEntryW* entries = (LogFileEntryW*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, MAX_LOG_FILES_TO_SCAN * sizeof(LogFileEntryW));
    if (!entries) return;
    int count = 0;
    bool current_found = false;

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) { HeapFree(GetProcessHeap(), 0, entries); return; }
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (count >= MAX_LOG_FILES_TO_SCAN) break;

        _snwprintf_s(entries[count].path, _countof(entries[count].path), _TRUNCATE, L"%s\\%s", log_dir, fd.cFileName);
        entries[count].last_write = fd.ftLastWriteTime;
        if (current_log_path && _wcsicmp(entries[count].path, current_log_path) == 0)
            current_found = true;
        ++count;
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    int keep_existing = current_found ? MAX_LOG_FILES_TO_KEEP : (MAX_LOG_FILES_TO_KEEP - 1);
    if (keep_existing < 0) keep_existing = 0;
    if (count <= keep_existing) { HeapFree(GetProcessHeap(), 0, entries); return; }

    qsort(entries, count, sizeof(entries[0]), CompareLogFileEntryW);
    int delete_count = count - keep_existing;
    for (int i = 0; i < delete_count; ++i) {
        DeleteFileW(entries[i].path);
    }
    HeapFree(GetProcessHeap(), 0, entries);
}

static void SetupDatedLogPathAndCleanup(HMODULE hModule)
{
    wchar_t module_path[MAX_PATH * 2];
    module_path[0] = 0;
    GetModuleFileNameW(hModule, module_path, _countof(module_path));

    wchar_t log_dir[MAX_PATH * 2];
    wchar_t log_base[MAX_PATH * 2];
    SplitModulePathForLogW(module_path, log_dir, _countof(log_dir), log_base, _countof(log_base));

    SYSTEMTIME st;
    GetLocalTime(&st);
    _snwprintf_s(g_wlog_path, _countof(g_wlog_path), _TRUNCATE, L"%s\\%s_%04u%02u%02u_%02u%02u%02u.log",
        log_dir, log_base, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    WideCharToMultiByte(CP_UTF8, 0, g_wlog_path, -1, g_log_path, sizeof(g_log_path), nullptr, nullptr);
    CleanupOldLogFilesW(log_dir, log_base, g_wlog_path);
}

static void WriteLog(const char* fmt, ...)
{
    if (!g_wlog_path[0]) return;

    FILE* f = nullptr;
    if (_wfopen_s(&f, g_wlog_path, L"a") != 0 || !f) return;

    long pos = ftell(f);
    if (pos == 0) {
        fwrite("\xEF\xBB\xBF", 1, 3, f);
    }

    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(f, "[%04u-%02u-%02u %02u:%02u:%02u.%03u] ", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fputs("\n", f);
    fclose(f);
}

static void ClearLastManualSaveOrLoadState()
{
    last_state.file_name[0] = 0;
    last_state.source[0] = 0;
}

static void RecordLastManualSaveOrLoad(const char* file_name, const char* source)
{
    if (!file_name || !file_name[0])
        return;

    lstrcpynA(last_state.file_name, file_name, sizeof(last_state.file_name));
    lstrcpynA(last_state.source, source ? source : "未知", sizeof(last_state.source));

    WriteLog("记录 '%s'(%s)", last_state.file_name, last_state.source);
}

static bool HasLastManualSaveOrLoad()
{
    return last_state.file_name[0] != 0;
}

static void MarkSaveDialogRecentlyClosed()
{
    g_save_dialog_recently_closed = true;
    g_save_dialog_recent_until = GetTickCount() + SAVE_DIALOG_RECENT_MS;
}

static void ClearSaveDialogRecentState()
{
    g_save_dialog_recently_closed = false;
    g_save_dialog_recent_until = 0;
}

static bool IsSaveDialogRecordAllowed()
{
    if (g_save_dialog_visible)
        return true;

    if (!g_save_dialog_recently_closed)
        return false;

    DWORD now = GetTickCount();
    if ((LONG)(g_save_dialog_recent_until - now) >= 0)
        return true;

    ClearSaveDialogRecentState();
    return false;
}

static void ExtractFileName(const char* save_path, char* file_name, int file_name_size)
{
    file_name[0] = 0;
    if (!save_path || !save_path[0]) return;

    const char* slash1 = strrchr(save_path, '\\');
    const char* slash2 = strrchr(save_path, '/');
    const char* slash = slash1 > slash2 ? slash1 : slash2;
    lstrcpynA(file_name, slash ? slash + 1 : save_path, file_name_size);
}

static bool GetEntryNameByIndex(char* self, int index, char* out_name, int out_name_size)
{
    if (out_name && out_name_size > 0) out_name[0] = 0;
    if (!self || !out_name || out_name_size <= 0 || index < 0) return false;

    char* begin = nullptr;
    char* end = nullptr;
    __try {
        begin = *(char**)(self + LOAD_OBJ_DISP_BEGIN);
        end   = *(char**)(self + LOAD_OBJ_DISP_END);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (!begin || !end || end <= begin) return false;

    int count = int((end - begin) / LOAD_ENTRY_SIZE);
    if (index >= count) return false;

    __try { lstrcpynA(out_name, begin + index * LOAD_ENTRY_SIZE + LOAD_ENTRY_NAME_OFFSET, out_name_size); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return out_name[0] != 0;
}

static int FindEntryIndexByName(char* self, const char* want_name, int* out_count)
{
    if (out_count) *out_count = 0;
    if (!self || !want_name || !want_name[0]) return -1;

    char* begin = nullptr;
    char* end = nullptr;
    __try {
        begin = *(char**)(self + LOAD_OBJ_DISP_BEGIN);
        end   = *(char**)(self + LOAD_OBJ_DISP_END);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
    if (!begin || !end || end <= begin) return -1;

    int count = int((end - begin) / LOAD_ENTRY_SIZE);
    if (out_count) *out_count = count;

    for (int i = 0; i < count; ++i) {
        char name[MAX_PATH];
        name[0] = 0;
        __try {
            lstrcpynA(name, begin + i * LOAD_ENTRY_SIZE + LOAD_ENTRY_NAME_OFFSET, sizeof(name));
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            continue;
        }
        if (name[0] && _stricmp(name, want_name) == 0)
            return i;
    }
    return -1;
}

// 对话框界面类型:[+0x64]==1→读档,[+0x65]==1→存档,皆 0→新游戏选图。
enum DialogKind { DK_NEWGAME = 0, DK_LOAD = 1, DK_SAVE = 2 };
static DialogKind GetDialogKind(char* self)
{
    if (!self) return DK_NEWGAME;
    unsigned char b64 = 0, b65 = 0;
    __try {
        b64 = *(unsigned char*)(self + LOAD_OBJ_MODE_LOAD);
        b65 = *(unsigned char*)(self + LOAD_OBJ_MODE_SAVE);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return DK_NEWGAME;
    }
    if (b65) return DK_SAVE;
    if (b64) return DK_LOAD;
    return DK_NEWGAME;
}

// 读档/存档选择窗口通用后置覆盖:在原生默认选中之后,把选择改成记录文件并刷新 UI。
static bool ApplyRecordedSelection(char* self)
{
    if (!HasLastManualSaveOrLoad() || !self)
        return false;

    DialogKind kind = GetDialogKind(self);
    int count = 0;
    int target = FindEntryIndexByName(self, last_state.file_name, &count);
    if (target < 0) {
        WriteLog("'%s' 不在列表中(共%d项),跳过", last_state.file_name, count);
        return false;
    }

    int old = -1;
    int old_top = -1;
    int old_scroll_tick = -1;
    int old_scroll_count = -1;
    char* scroll = nullptr;
    __try {
        old = *(int*)(self + LOAD_OBJ_SELECTED);
        old_top = *(int*)(self + LOAD_OBJ_TOPINDEX);
        scroll = *(char**)(self + LOAD_OBJ_SCROLLBAR_LIST);
        if (scroll) {
            old_scroll_tick = *(int*)(scroll + SCROLLBAR_TICK);
            old_scroll_count = *(int*)(scroll + SCROLLBAR_TICKS_COUNT);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}

    int visible_rows = (kind == DK_SAVE) ? SAVE_VISIBLE_ROWS : LOAD_VISIBLE_ROWS;
    int top = target - (visible_rows - 1);
    if (top < 0) top = 0;

    __try {
        *(int*)(self + LOAD_OBJ_SELECTED) = target;

        // 存档界面有输入框控件,UpdateForSelectedScenario 和 Redraw 在消息循环前调用
        // 会破坏输入框初始化。只设置索引和滚动,让原生消息循环自行处理。
        if (kind != DK_SAVE) {
            g_inside_auto_select_refresh = true;
            THISCALL_3(void, ADDR_LOAD_UPDATE_SELECTED, self, target, 1);
            g_inside_auto_select_refresh = false;
            *(int*)(self + LOAD_OBJ_SELECTED) = target;
        }

        *(int*)(self + LOAD_OBJ_TOPINDEX) = top;

        scroll = *(char**)(self + LOAD_OBJ_SCROLLBAR_LIST);
        if (scroll) {
            int ticks_count = *(int*)(scroll + SCROLLBAR_TICKS_COUNT);
            int scroll_tick = top;
            if (scroll_tick < 0) scroll_tick = 0;
            if (ticks_count > 0 && scroll_tick >= ticks_count)
                scroll_tick = ticks_count - 1;
            *(int*)(scroll + SCROLLBAR_TICK) = scroll_tick;
            __try {
                DWORD vt = *(DWORD*)scroll;
                if (vt) {
                    typedef void (__thiscall *ScrollbarSetTickProc)(void*, int);
                    ScrollbarSetTickProc set_tick = *(ScrollbarSetTickProc*)(vt + 0x38);
                    if (set_tick) set_tick(scroll, scroll_tick);
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }

        // 存档界面有输入框控件,在消息循环前调用 Redraw 会干扰其初始化。
        // 存档界面的 Redraw 会在消息循环开始后由原生逻辑触发。
        if (kind != DK_SAVE)
            THISCALL_1(void, ADDR_DIALOG_REDRAW, self);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        g_inside_auto_select_refresh = false;
        WriteLog("更新 失败,保持原选择");
        return false;
    }

    int new_top = -1;
    int new_scroll_tick = -1;
    int new_scroll_count = -1;
    __try {
        new_top = *(int*)(self + LOAD_OBJ_TOPINDEX);
        scroll = *(char**)(self + LOAD_OBJ_SCROLLBAR_LIST);
        if (scroll) {
            new_scroll_tick = *(int*)(scroll + SCROLLBAR_TICK);
            new_scroll_count = *(int*)(scroll + SCROLLBAR_TICKS_COUNT);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}

    const char* kind_name = (kind == DK_SAVE) ? "存档" : "读档";
    WriteLog("更新 %s界面 选中=%d(原=%d,共%d项)'%s'",
        kind_name, target, old, count, last_state.file_name);
    return true;
}

// SaveGame hook：存档界面可见时的保存视为手动存档，记录文件名供下次自动选中。
// H3Main::SaveGame 实际签名为 THISCALL_6(VOID, 0x4BEB60, this, save_name, a3, a4, a5, a6)，
// hook 必须接收并传递全部 6 个参数，否则栈错位会导致崩溃。
static int __stdcall HH_SaveGame(HiHook* h, DWORD thisPtr, const char* save_path, DWORD a3, DWORD a4, DWORD a5, DWORD a6)
{
    if (IsSaveDialogRecordAllowed() && save_path && save_path[0]) {
        char file_name[MAX_PATH];
        ExtractFileName(save_path, file_name, sizeof(file_name));
        if (file_name[0])
            RecordLastManualSaveOrLoad(file_name, "手动存档");
    }
    ClearSaveDialogRecentState();

    THISCALL_6(void, h->GetDefaultFunc(), thisPtr, save_path, a3, a4, a5, a6);
    return 0;
}

// 对话框显示 hook(0x584EF4):在对话框消息循环开始之前,自动选中记录文件。
// 此点在 fcn.00584EC0 内部,无论窗口是新建还是复用(连续存档),每次显示都会触发。
static int __stdcall Hook_DialogShow(LoHook* h, HookContext* c)
{
    char* self = (char*)c->esi;
    if (!self)
        return EXEC_DEFAULT;

    DialogKind kind = GetDialogKind(self);
    g_save_dialog_visible = (kind == DK_SAVE);
    if (kind == DK_SAVE)
        ClearSaveDialogRecentState();

    // 读档/存档界面:如果有记录则自动选中
    if ((kind == DK_LOAD || kind == DK_SAVE) && HasLastManualSaveOrLoad())
        ApplyRecordedSelection(self);

    return EXEC_DEFAULT;
}

// 对话框析构前 hook(0x583E10):析构函数入口,ECX=对话框对象,数据尚完整。
// HiHook SPLICE_ THISCALL_:ecx=对话框对象。
// 注意:MSVC C++ 析构函数有隐藏的第二参数 delete_flag(0=仅析构,1=析构+delete),
// hook 必须原样传递,否则栈清理量不匹配会导致崩溃。
// 所有对话框关闭路径都必经此处。如果是读档界面,先记录玩家选中的文件名再调用原函数。
static int __stdcall HH_DialogDestructor(HiHook* h, char* self, int delete_flag)
{
    if (self) {
        DialogKind kind = GetDialogKind(self);
        if (kind == DK_SAVE) {
            g_save_dialog_visible = false;
            MarkSaveDialogRecentlyClosed();
            WriteLog("存档关闭:不直接记录,等待 SaveGame 确认真正保存");
        }

        // 读档界面关闭时记录当前选中文件名。
        // 存档界面关闭不能直接记录：用户可能只是取消/关闭，并没有实际保存。
        if (kind == DK_LOAD) {
            int selected = -1;
            __try {
                selected = *(int*)(self + LOAD_OBJ_SELECTED);
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                selected = -1;
            }
            char name[MAX_PATH] = {0};
            if (selected >= 0)
                GetEntryNameByIndex(self, selected, name, sizeof(name));
            const char* kind_name = "读档关闭";
            if (name[0]) {
                RecordLastManualSaveOrLoad(name, kind_name);
            } else {
                WriteLog("%s:未读到文件名(index=%d)", kind_name, selected);
            }
        }
    }

    THISCALL_2(void, h->GetDefaultFunc(), self, delete_flag);
    return 0;
}

// 注册插件功能。
static void StartPlugin()
{
    // SaveGame 和 DialogDestructor 使用 HiHook(函数边界,安全可行)。
    _PI->WriteHiHook(ADDR_H3MAIN_SAVE_GAME, SPLICE_, EXTENDED_, THISCALL_, HH_SaveGame);
    _PI->WriteHiHook(ADDR_DIALOG_DESTRUCTOR, SPLICE_, EXTENDED_, THISCALL_, HH_DialogDestructor);

    // DialogShow 使用 LoHook(hook 点在函数内部,HiHook 无法替代)。
    _PI->WriteLoHook(ADDR_DIALOG_SHOW, Hook_DialogShow);

    WriteLog("SaveLoadEnhance 已启用。Hook:SaveGame@0x%08X(HiHook),DialogShow@0x%08X(LoHook),DialogDestructor@0x%08X(HiHook)。",
        ADDR_H3MAIN_SAVE_GAME, ADDR_DIALOG_SHOW, ADDR_DIALOG_DESTRUCTOR);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved)
{
    static bool initialized = false;
    if (reason == DLL_PROCESS_ATTACH && !initialized) {
        initialized = true;

        SetupDatedLogPathAndCleanup(hModule);

        ClearLastManualSaveOrLoadState();
        WriteLog("SaveLoadEnhance 正在加载。插件没有 ini;被加载即表示已启用。");

        _P = GetPatcher();
        if (!_P) {
            WriteLog("GetPatcher 失败;插件将保持未激活状态。");
            return TRUE;
        }

        _PI = _P->CreateInstance("HD.Plugin.SaveLoadEnhance");
        if (!_PI) {
            WriteLog("CreateInstance 失败;插件将保持未激活状态。");
            return TRUE;
        }

        WriteLog("Patcher 实例已创建:HD.Plugin.SaveLoadEnhance");
        StartPlugin();
    }
    return TRUE;
}

