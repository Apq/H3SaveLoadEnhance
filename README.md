# SaveLoadEnhance — 人性化读档

## 功能

英雄无敌3 SoD HD Mod 插件。玩家手动存档或读档后，插件在进程内记住该存档文件名；下次打开存档/读档界面时，自动选中该存档所在行并滚动到可见位置。

- DLL 被 HD Mod 加载即启用，无配置项
- 仅记录手动存档和读档，自动存档跳过
- 状态仅存内存，重启清空
- 写入带时间戳的日志，自动保留最近 30 个

## 依赖

仅依赖原版游戏 EXE + HD Mod 框架（`patcher_x86.hpp`）。不依赖 ZCN2.dll、H3.TextColor.dll、ERA、SoD_SP 或其他插件。

## 部署

```text
D:\Heroes3\Heroes3_2026.05.01\_HD3_Data\Packs\人性化读档\
├── SaveLoadEnhance.dll
├── SaveLoadEnhance.ini          （系统支持说明，无配置项）
└── SaveLoadEnhance_YYYYMMDD_HHMMSS.log
```

在 HD Launcher 插件列表中启用「人性化读档」即可。

## 编译

- Visual Studio（v145 工具集），Configuration=Release，Platform=Win32
- 编译后运行 `deploy.ps1` 部署

## 项目文件

| 文件 | 说明 |
|------|------|
| `SaveLoadEnhance.cpp` | 源码（单文件，3 个 Hook） |
| `resource.h` | 版本信息资源头文件 |
| `SaveLoadEnhance.rc` | DLL 版本信息资源 |
| `SaveLoadEnhance.ini` | 系统支持说明（无配置项，仅注释） |

## 已验证版本

- Heroes3 2026.05.01（HD Mod）
- DLL 版本：0.4.2026.628
