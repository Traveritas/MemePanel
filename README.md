# MemePanel

表情包速发面板。常驻托盘，`Ctrl+Shift+.` 呼出，搜索/选中/Enter 即发（GIF 保留动画）。**纯 C + Win32 + GDI+，无 CRT、零运行时依赖，单文件 exe（约 110 KB）**。视觉为 SWISS 黑白设计：纸白方角面 + 2px 墨框 + 单一强调色，零圆角零阴影零渐变。

👉 下载：[Releases](https://github.com/Traveritas/MemePanel/releases)（exe 放任意目录直接运行，库与配置随 exe 走，绿色便携）

## 功能

- **快发**：标签/索引文字/文件名即时搜索；方向键导航 + Enter 发送（自定义 GIF/PNG 格式 + CF_HDROP + CF_DIB 三格式同贴，QQ/微信/Telegram 保留 GIF 动画）；悬停大图预览、视口内 GIF 播放
- **最近行**：面板第一行固定展示最近使用（满员自动轮换），墨线分节强调
- **标签**：64 位掩码标签（最多 64 个），主面板标签行过滤（溢出滚轮横滚、吸附标签边界），独立标签管理窗（重命名/新建/删除）
- **导入**：内容寻址（FNV-1a 64 哈希命名）+ 全局判重（同图只存一份），原文件名保留为显示名；支持文件/文件夹/剪贴板粘贴导入
- **检视抽屉**：元数据、打标、索引文字、F2 重命名、收藏、两段式武装删除；Ctrl+点击批量操作
- **维护**：库目录重扫、去重扫描（结果就地显示）
- **设置窗**：中/英语言、开机自启、呼出方式、文件名搜索、最近行开关、不透明度（30–100% 八档）、主题强调色

## 构建

需 VS2022 C++ 生成工具（x64，vswhere 自动定位）：

```
cd app
build.bat
```

产物 `app\out\MemePanel.exe`。重新链接前若实例在跑，先 `taskkill /IM MemePanel.exe /F`。

## 仓库结构

```
app/        主程序源码（src/main.c 单文件主体 ~5900 行；构建/冒烟脚本；内嵌 WebView2 SDK 为历史遗留）
variants/   设计探索期变体（05-swiss 为当选主方向，其余归档）
design/     设计稿与规格文档
HANDOFF.md  版本沿革 + 决策/踩坑全记录（唯一权威历史）
BACKLOG.md  待办与已否决项
```

详细操作手册见 [app/README.md](app/README.md)；无头测试探针（`-shot`/`-win`/`-wheelsnap` 等）见 HANDOFF.md。
