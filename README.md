# API 任务栏额度插件

在 Windows 任务栏中常驻显示 CC Switch 供应商的剩余额度，无需打开 CC Switch 界面。支持 Kimi 和智谱 GLM（含多个同平台账号），最多同时显示两个供应商。

## 显示效果

只选一个供应商时，上下两行显示 5 小时和每周窗口的剩余时间（保留 1 位小数）与剩余百分比：

```
4.7h 98%
4.8d 84%
```

选两个供应商时，每行显示一个供应商的名称、5 小时窗口剩余小时数及百分比、每周窗口剩余天数及百分比：

```
Kimi 4.7h 98% 4.8d 84%
GLM 2.1h 45% 3.2d 60%
```

刷新失败时保留最后一次成功数据，并显示 `!` 标记。

## 工作原理

本插件是 TrafficMonitor 1.86 的自定义插件，借助其任务栏窗口嵌入任务栏。后台线程每 5 分钟以只读方式打开 CC Switch 数据库，列出带凭据且支持额度查询的 Claude 供应商，请求所选供应商的额度接口并更新显示。令牌只保存在内存中，用完立即清除，不写入任何配置或日志文件。供应商选择保存在 `%LOCALAPPDATA%\KimiQuotaTaskbar\settings.json`。

## 操作

- 单击任务栏数值：立即刷新
- 双击：弹出供应商选择窗口，勾选最多两个要显示的供应商，每行附该供应商的额度详情
- 右键：打开 TrafficMonitor 菜单（含「刷新额度」「选择供应商」「额度详情」命令）

## 安装

1. 安装 TrafficMonitor 1.86 x64 Lite 到 `C:\Users\Lim_z\Apps\TrafficMonitor`
2. 运行 `build.cmd` 构建，先退出 TrafficMonitor，再将 `build\out\KimiQuotaPlugin.dll` 复制到 TrafficMonitor 的 `plugins` 目录
3. 启动 TrafficMonitor，在选项的任务栏窗口设置中勾选插件显示项「API 接口额度」
4. 开机自启动由 TrafficMonitor 的注册表 Run 项实现，本机已配置

## 卸载

1. 删除注册表项 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` 中的 `TrafficMonitor` 值
2. 退出 TrafficMonitor，删除 `plugins\KimiQuotaPlugin.dll`
3. 删除 `%LOCALAPPDATA%\KimiQuotaTaskbar`

## 维护

- Windows 大版本更新后，运行 `python tools\check_taskbar_parent.py` 复验任务栏嵌入是否仍然有效
- `build\out\live_quota_check.exe` 可独立查询所有可查供应商的额度，用于排查网络或凭据问题
- 修改源码后运行 `build.cmd` 重新构建，解析测试随构建自动执行
