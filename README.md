# SaveLoadEnhance — 人性化读档

英雄无敌 3 SoD HD Mod 插件。玩家手动存档或成功读档后，插件在本次游戏进程内记住该存档文件名；下次打开存档/读档界面时，自动选中该文件并滚动到可见位置。

## 功能

- DLL 被 HD Mod 加载即启用；ini 仅用于控制日志开关
- 记录最后一次手动存档或成功读档的文件名
- 取消/关闭存档或读档窗口不会更新记录
- 状态仅保存在进程内存中，重启游戏后清空
- 默认每次启动写入带时间戳的日志，并在插件加载时清理旧日志、保留最近 30 个日志文件
- 可通过 `SaveLoadEnhance.ini` 禁止日志；禁止后不会生成日志，也不会扫描/清理旧日志

## 部署

```text
<游戏目录>\_HD3_Data\Packs\人性化读档\
├── SaveLoadEnhance.dll
├── SaveLoadEnhance.ini          （日志开关与系统支持说明）
└── SaveLoadEnhance_YYYYMMDD_HHMMSS.log
```

在 HD Launcher 插件列表中启用「人性化读档」。

## 配置

```ini
[Logging]
; 0 = 不禁止，正常写日志并保留最近 30 个（默认）
; 1 = 禁止，不生成日志，也不扫描/清理旧日志
DisableLog=0
```

## 依赖

- Heroes3 SoD / HD Mod 32 位进程
- HD Mod patcher_x86 插件环境
- Visual C++ 运行库

不依赖 ERA、ZCN2.dll、H3.TextColor.dll 或其他插件。

## 编译

- Visual Studio v145 工具集
- Configuration: `Release`
- Platform: `Win32`

当前可用构建命令：

```powershell
cd "C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin"
.\MSBuild.exe D:\GitHub\H3\H3SaveLoadEnhance\SaveLoadEnhance.vcxproj /p:Configuration=Release /p:Platform=Win32 /m
```

输出：

```text
D:\GitHub\H3\H3SaveLoadEnhance\Release\SaveLoadEnhance.dll
```

部署可运行 `deploy.ps1`，会复制 DLL 和 ini 到当前脚本写定的游戏 Packs 目录。

## 项目文件

| 文件 | 说明 |
|------|------|
| `SaveLoadEnhance.cpp` | 插件源码，包含 3 个 HiHook 和 2 个 LoHook |
| `SaveLoadEnhance.rc` | DLL 版本信息资源 |
| `resource.h` | 资源头文件 |
| `SaveLoadEnhance.ini` | 日志开关与系统支持说明 |
| `使用说明.txt` | 面向玩家的安装和使用说明 |
| `设计文档.md` | 当前 Hook 设计与关键偏移说明 |

## 当前实现摘要

- 存档：在存档窗口可见或刚关闭后的短窗口内，`H3Main::SaveGame @ 0x4BEB60` 真正被调用后记录文件名。
- 读档：`0x58BFB0` 读档确认处理函数先捕获当前选中文件名；手动读档路径中 `call 0x4BEFF0` 返回后的 `0x58C62E` LoHook 读取 EAX，只有 `LoadGame` 成功返回才记录。
- 自动选中：`0x584EF4` 对话框显示点根据内存记录更新读档/存档列表选择。
- 关闭窗口：读档窗口关闭不记录；存档窗口关闭只建立短暂候选，等待实际 SaveGame 调用确认。

## 已验证版本

- Heroes3 2026.05.01（HD Mod）
- DLL 版本：0.5.2026.702
