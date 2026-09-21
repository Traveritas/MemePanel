# 06-terminal NEON-TERM — 交付笔记

STATUS: DONE

## 设计 token 摘要

| Token | 值 | 用途 |
|---|---|---|
| PHOSPHOR | #2AFF88 | 主色：标题/文字/激活态/hover 框/进度块 |
| DIM GREEN | #16A852 α~92 | 普通格子 1px 细框、弱化件 |
| AMBER | #FFB000 | 仅三处：`~GIF~` 角标 / 删除按钮+武装态 / 右缘滚动块 |
| 面板底 | rgba(6,12,8,0.94) 近黑带绿 | 极轻微上亮下暗 |
| 扫描线 | 每 3px 一条 ×0.78 暗纹 | 预渲染进底图 |
| 辉光 vignette | 边缘 26px 带，向磷绿拉 ~α8+ | 预渲染 |
| 边框 | 双线：外 1px PH α160 + 内 1px α60（inset 5px） | 预渲染 |
| 角加强 | 四角十字短杆 2px α235，向外伸 5px（bleed=8 留量） | box-drawing joint 风 |
| 圆角 | 0（全方角，纯 FillRectangleI 硬边） | |
| 字体 | 全站 Consolas（中文 fallback 雅黑）；标题 Impact「MEME://TERMINAL」 | |
| caret | 块状 9×16 磷绿实心块，盖字时反色黑字，530ms 闪烁 | |
| 文字 | 主=磷绿 α0.92 档；输入行「白绿」#D0FFE4 | |

## 结构改动（相对 base）

- **标题条**（新增）：左 Impact 标题 + `v0.6`（管理模式 `v0.6//SYS`）+ 右 `[42] ONLINE` + 绿点（静态 α 近似闪烁）+ `[X]` 关闭（hover 反色绿底黑字）；下接双线分隔（α110/α45）。关闭按钮从检索行移入标题条，`g_closeRc` 同步改在 `panel_layout`。
- **检索行 = 命令行**：`>` 前缀 α255 + 白绿输入 + 块状 caret，无框；IME 组合串绿下划线；`ime_update_position` 前缀偏移同步。分隔线分区（检索下 α52、标签行下 α52）。
- **标签/按钮 = 方括号 token**：`[全部] [动物] [猫猫]`；激活 = 反色磷绿底黑字 + glow(peak .25)；hover = 磷激发提亮。管理按钮 `[重命名] [删除] [去重] [修后缀]`，hover 反色；删除武装 = 琥珀反色，α 随 caret 节拍 238/172 交替（闪烁感静态近似）；武装态按「确认删除」文案定宽（rect 不变，防二次点击落空，沿用 base 策略）。
- **网格**：`term_fill`（图前）/ `term_frame`（图后）拆分——满幅缩略图会盖住先画的框，框必须压在图上。normal=暗绿 1px α92；hover=磷激发 α28 + 2px α235 亮框 + glow；选中=α36 + 2px 框 + **中心琥珀 X**（黑衬底 + 双短线交叉，画在图上方）。
- **hover 名字条**：黑条 α205 + `> filename` 磷绿 tiny 字（NoWrap+省略号截断）。
- **GIF 角标**：`~GIF~` 琥珀 + 黑衬条。
- **滚动指示**：右缘琥珀块串（5×7px 矩形、9px 节距，只画 thumb 段）；**状态行右段** `▓▓▓░░` 绿块（5 块，滚动位置）+ 计数。
- **状态行**：速发 `READY`+闪烁块（hover 时变 `> 名 · 标签` 反馈）；管理 `SYS: <文案>`（武装时琥珀）。
- **悬停大图**：方角黑底 + 绿双线框（外 α190/内 α70）；上方 `VIEW:` 前缀文件名（右对齐）；下方 `WxH IMG`（GIF 播放中 = `WxH NFR` 琥珀）。
- **gen_base**：整体重写（磷屏 + 扫描线 + vignette + 双线框 + 角加强，全部预渲染）；`g_bleed` 3→8（S()，给角加强留出血）。
- **图标**：终端风（近黑绿屏 + 绿方框 + 扫描线 + `>` 提示符 + 块状 caret）。
- **gp.h**：按 GP_LIST 模式增补 `GdipSetStringFormatFlags`（SDK gdiplusflat.h 签名），四个字符串格式全部加 NoWrap——长名省略号截断而非折行被裁。

## 自检结果（三张截图 + 像素探针，24/24 PASS）

`probe.ps1`（本轮实测，exit 0）：
- shot_panel：面板边界检出 L=10 T=10（角加强出血正确）；**扫描线 3px 周期相位 2 最暗（17/17/14，Δ3）**；角十字 (7,10)/(10,7)=RGB(43,239,130)；内线 (18,74,43)；标题绿字 1340px；激活 chip 反色 2118px；标签 chip 92px；GIF 琥珀 1221px；READY 文字/进度块均在；普通格框 (62,122,87)。
- shot_hover：hover 亮框 (433,290)=RGB(48,245,133)；名字条 58px；预览框 (32,192,103)；VIEW 标签 146px；帧信息行 23px；footer 文件名 480px。
- shot_mgr_sel：琥珀 X 115px；选中亮框 (46,244,133)；按钮行绿 315px + 删除琥珀 27px；SYS 状态 526px；chip 反相 2118px。

视觉复核（analyze_image ×2）：标题/[N] ONLINE/块状 caret/反色 chip/3×`~GIF~`/READY+▓块+计数均确认；指出的两问题（名字条溢出、标签乱码）已修复（NoWrap+省略号；index.bin 重打补丁）。

### 修复轮次记录
1. AA 把 1px FillRectangleI 劈成两个半 α 像素（边框减半）→ `fill_rect` 内临时切 SmoothingMode None。
2. `g_footer.left/right` 从未赋值（base 遗留）→ footer 内容锚到 x=0/负坐标，已在 layout 赋值。
3. 框/X 记号被后画的图盖住 → term_fill/term_frame 拆分、X 移到图后。
4. 演示 `out/index.bin` 实际 nTags=0（与 GUIDE 描述不符）→ 用 `patch_index.ps1`/`patch_index2.ps1` 注入 动物/猫猫 + tagmask（动物=38 张、猫猫=猫系）。注意：PS5.1 把无 BOM 的 UTF-8 脚本按 GBK 读，脚本内中文会乱码——第二个补丁改用 char code（U+52A8/7269、U+732B）。

## 已知不足

- 扫描线只叠在面板底图上（brief 指定生成期预渲染），缩略图/文字上方无扫描纹。
- 预览内层 1px 框（inset 4px）会被满幅方图盖住（图仅 inset 1px），外框恒可见；属方角满幅构图的取舍。
- 演示数据的 GIF 缩略图首帧偏暗，截图里 GIF 格观感偏空（数据问题，非渲染）。
- 删除武装闪烁借 caret 530ms 节拍，窗口无焦点（caret timer 停）时静止在 238α。
- Consolas 无中文 → 标签/状态中文走雅黑 fallback，混排宽度已用 NoWrap+省略号兜底。
- `-hover` 大图在 mgr 模式同样出现（brief 未禁止，与 base 行为一致）。

构建：`cmd //c build.bat` 通过，`out/MemePanel.exe` 60,928 字节（<90KB 软上限）。
