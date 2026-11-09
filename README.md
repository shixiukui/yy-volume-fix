# YY Volume Fix

PC YY 单人音量恢复补丁。修复重进频道或重启 YY 后，滑块保留原值、实际播放音量却恢复默认的问题。

## 安装

1. 从系统托盘彻底退出 YY。
2. 解压发布包，把 `version.dll` 放到 `YY.exe` 同目录。如果已有同名文件，先确认来源并备份。
3. 用原来的快捷方式启动 YY，进入频道，将需要记住音量的用户各调一次。
4. 重进频道或重启 YY 后，等对方说话，确认实际音量保持在设定值。

音量按频道和用户保存在 YY 安装目录中的 `YYVolume<频道号>.bin`。要停用补丁，退出 YY 后移走补丁 DLL。如果安装时备份过原有同名 DLL，将它放回原位。

## 兼容性

- 支持已校验的 PC YY 9.60.0.0 32 位构建。补丁会检查 `bizchannel.dll` 的 SHA-256。
- 安装目录需要有写权限，完整路径受 `MAX_PATH` 限制。
- 605452 的重启恢复在前一版 DLL 上已实测。本版按频道分别保存，其他频道尚未逐个实测。

## 编译

需要 PowerShell、Visual Studio x86 C++ Build Tools、Windows SDK 和 MinHook 源码。将 MinHook 放在任意目录，然后在仓库根目录运行以下命令，将参数替换成实际路径。输出目录必须尚不存在。

```powershell
.\tools\yy_proxy\build.ps1 -MinHookRoot '.\third_party\minhook' -OutputDirectory '.\out'
```

生成的文件为 `out\version.dll`。补丁逻辑位于 [volume_restore.cpp](tools/yy_proxy/volume_restore.cpp)，代理转发位于 [version_proxy.cpp](tools/yy_proxy/version_proxy.cpp)。
