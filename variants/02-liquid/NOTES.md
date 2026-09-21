# 02 液态玻璃 LIQUID — 交付笔记

STATUS: DONE

## 设计 token 摘要

- **底盘**（gen_base 手写像素）：垂直三段线性 rgba(150,170,210,.55) → rgba(60,70,95,.72) → rgba(25,30,45,.85) + 顶部径向白光 peak 0.22（中心 +10% 宽、-6% 高）。比原版明显更通透（原版 α .90-.94）。
- **圆角**：外壳 S(24)（RAD_SHELL）、格子/字段/托盘 S(14)（RAD_CELL）、胶囊 = 高度/2（999 胶囊）。
- **强调对**：#6FD3FF 青 ↔ #B28BFF 紫（激活胶囊渐变、勾角标、glow）；色散暖端 #FFB86B。
- **文字**：C_INK = rgba(240,247,255,~.95)；阴影文字 `gtext_shadow()`（先 1px 下移 ARGB(210,6,8,16) 暗色 pass 再亮色 pass）用于 hover 名字条 / footer 文件名 / GIF 角标 / 激活胶囊与武装态按钮。
- **发光**：glowSoft peak 0.22（hover 柔光，青色）、glowStrong peak 0.30（激活胶囊外发光，brief 指定）。

## 质感件（全部预渲染一次）

1. **折射光带** `g_bandTex` 8×256 sin² 白剖面（peak 0.13、总宽 S(60)），贴在面板上部 1/3（y = T + 0.33·ph），clip 面板圆角。
2. **镜面 streak ×2** `streak()`：左上→右下对角平行四边形，宽 S(80)/α26 与 S(40)/α16（brief 0.08/0.05 的可见化取整），两半各一次垂直 LineBrush 渐隐。
3. **色散边**三层 1px：外 #FFB86B α70（左缘覆 #6FD3FF α70）→ 中白 α90 → 内暖 α40（左右覆冷 α40，上下保持暖 = 对称交换）。验收方向：左缘冷、右缘暖。
4. **格底液态贴图** `make_cell_tex` 64×64：normal 顶 0.10→底 0.02，hover 顶 0.20→底 0.05；hover 额外底缘 1px 受光线 α150（液滴聚光）。
5. **激活胶囊**：青→紫对角渐变 + 上缘白高光弧（胶囊路径 ∩ 大椭圆 clip，上 40% 高度白渐变 α102→0）+ glowStrong 外发光 + 左上 3px 反光点 α150。

## 结构改动

- **dock 合体**（brief 要求）：最近行 + 标签行合并为一条内凹玻璃托盘 `draw_dock()`——左端胶囊（全部/标签，垂直居中）→ 细竖分隔线 → 右端 7 个最近格（右对齐，最新在最右）。`panel_layout` 速发分支重写：dock 高 S(68)，放不下的最近格 `SetRectEmpty`（不画不命中）；`draw_chip` 右界让开最近格（速发）/按钮组（管理）。网格起点 = dock.bottom + S(14)，比原三段式紧凑 30 逻辑px。
- **draw_search_box**：暗液体腔 α(88,8,12,22) + 底缘内透光；有输入时描边转青（强调色只干一件事：激活态）。
- **draw_grid**：格子半径 S(14)；hover 名字条暗渐变 + 双 pass 阴影文字；管理模式选中 = 青描边 + 青→紫渐变勾角标（角标改到**图片之后**画——原版画在 contain 图下面会被盖住，mgr 截图里角标首次真正可见）。
- **draw_preview**：深液腔 + 白边 + 顶冷/底暖色散微边（改到图之后画，同上遮挡原因）。
- **draw_mgr_button**：真胶囊（rad = h/2）；武装态 = 青紫渐变底 + 红边 + 半粗体阴影文字。
- 图标 tint 改液态蓝青系。

## 重要工程发现（写给后面的人）

**本机 GDI+ 的 `GdipCreateLineBrush[I]` 对 `WrapModeClamp`(4) 一律返回 InvalidParameter（2）**——点对/FromRectI/float 版全灭，最小复现已验证。这意味着**原版 HALO 代码里所有 GP_WRAP_CLAMP 线渐变（激活胶囊渐变、hover 名字条暗渐变）在这台机器上从未渲染过**。修法：全部换成 `GP_WRAP_TILE`(0)（gp.h 新增常量并注明原因）。本变体所有渐变填充域都落在单个 tile 周期内，Tile 与 Clamp 视觉等价。附带收益：hover 名字条暗渐变首次真正生效（probe (458,236)=(6,4,5)）。
另：线渐变填充圆角一律走 `fill_rr_gradient()`（clip 路径 + FillRectangleI），不依赖 FillPath×LineBrush 组合。

gp.h/gp.c 增补绑定（签名逐条抄自 SDK gdiplusflat.h）：`GdipFillEllipseI`、`GdipAddPathEllipseI`、`GdipStartPathFigure`、`GdipCreateLineBrush`(float)、`GdipCreateLineBrushFromRectI`（后两个为排查时加入，main.c 未用，保留备用）+ `GP_CM_INTERSECT`/`GP_WRAP_TILE` 常量 + `GpRect` 类型。

其他小修：entry() 里 `ReleaseDC(fdc, fdc)`（HDC 当 HWND 传）改为 `ReleaseDC(NULL, fdc)`，消掉 C4133。

## 演示数据修补

out/index.bin 原为 nTags=0（不知何时被清掉），dock/管理模式的标签胶囊无从演示。已按文件名启发式补回：`动物`(bit0) = capybara/chicken/dog/frog/hamster/panda/rabbit/seal/zzanim/dance*，`猫猫`(bit1) = cat*；42 条目的 name/size/mtime/used 原样保留（store_scan 按 name 匹配、保留 tagmask，安全）。

## 自检结果（像素探针，全部 PASS）

| 检查 | 采样 | 结果 |
|---|---|---|
| 色散左冷 | shot_panel (3,436) (4,436) | B>R（67>54 / 136>111）PASS |
| 色散右暖 | (1180,436)；mgr (1539,530) | R>B（110>93 两处）PASS |
| 折射光带 | (98,258)=94 vs (98,339)=81 | diff 13 ≥8 PASS |
| streak | y=74 峰 137 vs 肩部 ~121-124 | diff ≥13 ≥8 PASS |
| 底盘暗部可读 | (600,830) | (30,36,52) 深色，白字 α235 高对比 PASS |
| 激活胶囊渐变 | (44,132)=(96,141,173) 青 → (95,132)=(108,127,173) 偏紫 | PASS |
| 胶囊顶弧 | (70,112) lum=176（基线 ~135） | PASS |
| hover 名字条 | (458,236)=(6,4,5) | PASS |
| preview 色散线 | (1000,609) 冷 G↑ / (1000,815) 暖 R↑ | PASS |
| mgr 选中角标 | (432,155)=(128,188,245) 渐变青 | PASS |
| 标签胶囊存在 | chip 区文字亮采样 x100..300 | 6 处 PASS |

视觉复核（analyze_image，2 次尝试均成功）：第 1 轮指出「只有全部、无动物/猫猫」→ 属数据问题（见上），修补后第 2 轮六项全 PASS（dock 构图/胶囊渐变/分隔线/右对齐缩略图/网格/白边/色散/streak/光带），仅一条无意义的 nit（搜索框并无占位文本）。

构建：`cmd //c build.bat` 零警告，out/MemePanel.exe **65536 字节**（上限 90KB）。合成路径全部沿用「静态预渲染 + 每帧 memcpy」模式，新增逐帧成本仅为每格一次贴图 DrawImage + 2 条描边（与原 fill_rr 同量级）。

## 已知不足

- 色散边为 1px×3 层，在缩略图（GALLERY 截图）里几乎不可辨，需放大看边缘像素；探针可证。若要更张扬可把外层 α70 提到 α110+，但会开始抢内容。
- 折射光带/streak 的 alpha 严格贴 brief 数值（0.13 / 0.08·0.05），在亮壁纸背景下观感会比在暗演示背景下弱。
- `GdipCreateLineBrush`/`FromRectI` 两个绑定未被 main.c 使用（排查工具遗物，各 8 字节指针）。
- 演示标签的归类是文件名启发式（cat*→猫猫、其余动物→动物），green.png/dupA/dupB/fakeqq.gif/dance.gif 未打标（dance 已归动物）。
- dock 在极窄窗口（< ~S(640) 内容宽）会依次丢弃最近格（右侧起的左端格子置空），此时分隔线不画、胶囊右界退化为面板右缘——行为已处理，未做窄窗视觉实测。
