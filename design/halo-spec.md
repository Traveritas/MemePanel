# 「琉璃 HALO」目标 UI 规格（GDI 落地版）

> 定稿于 2026-09-18。高保真预览：`jiti-design-5.html`（选「05 琉璃」），React 参考实现：`wemd2-react-src/src/components/panels/HaloPanel.tsx`。

## 1. 一句话

深色半透明**玻璃浮层**：边缘一线高光、脚下真软阴影；渐变与发光只给「当前/激活」这一个时刻；结构沿用被验证的共识（最近优先、检索、标签、网格）。整窗半透明让壁纸透光——它属于这台桌面，不属于某个软件。

## 2. 视觉规格（tokens）

```
面板底色    线性渐变 164°：rgba(28,31,44,0.92) → rgba(17,19,29,0.88)
           + 顶部径向高光 radial(620×190 @ 10% -6%, rgba(255,255,255,0.10)→透明 62%)
边框        1px rgba(255,255,255,0.12)（外）+ inset 顶边 1px rgba(255,255,255,0.13)
阴影        两层：0 3px 8px rgba(0,0,0,0.30) + 0 32px 72px rgba(0,0,0,0.52)
圆角        三档：外壳 18 / 最近格与大图 14 / 网格格 11 / 胶囊 99
文字        主 #E7EBF6 · 次 rgba(231,235,246,0.55) · 弱 0.32；计数等宽 Consolas
强调色      ACCENT #7AA2FF → ACCENT2 #B28BFF（唯一渐变对，只用于激活胶囊与选中光）
玻璃格      底 rgba(255,255,255,0.03~0.055) + 边 rgba(255,255,255,0.075~0.12) + inset 顶亮线
hover 态    上浮 2px + 亮边 + 径向微光 rgba(122,162,255,0.20) + 名字条（底部渐变遮罩）
```

## 3. 面板结构（700×476 基准，DPI 缩放）

```
┌────────────────────────────────────────────┐
│ [⌕ 搜索表情…        Ctrl+Shift+.]  123 items ×│  检索行（38px 玻璃胶囊，内凹 inset 阴影）
│ 最近 · 按 1–7 直接发送                        │  小标签行（10.5px 弱字）
│ [1][2][3][4][5][6][7]  ← 68px 玻璃格×7        │  数字键标 = 玻璃小片（左上角）
│ (全部)(猫猫)(熊猫头)(代码)…  ← 激活=渐变胶囊+柔光 │  标签行
│ ▦ ▦ ▦ ▦ ▦ ▦ ▦ ▦ ▦ ▦  ← 56px 玻璃格网格×10列   │  GIF 角标（右下小片）
│                                              │  hover 出名字条；选中出微光
│ 文件名 · 标签      单击即贴 · 右键标签 · HALO   │  题注式信息行
└────────────────────────────────────────────┘
   悬停 ≥280ms → 右下角 168px 大图浮层（14 圆角+双层软阴影）
```

交互（全部已在 v0.3 有基础）：
- 数字键 1-7 → 直接发送最近条对应项（Enter 发送首个可见）
- Esc / 失焦 → 收起；滚轮翻页；右键格子 → 标签菜单
- 空状态：玻璃空页 + 引导文案

## 4. GDI 落地实现清单（v0.4 任务分解，按依赖顺序）

### 4.1 窗口架构改造（最大工程点）
- 弃用当前 `WS_EX_LAYERED + SetLayeredWindowAttributes`（整窗统一 alpha）
- 改 **`UpdateLayeredWindow`**：每帧（或脏区时）把整面板画进 32bpp DIB → 一次提交
  - DIB 尺寸 = 窗口尺寸（含阴影出血区，建议面板四周预留 40px 阴影画布，窗口比视觉面板大）
  - premultiplied alpha：所有绘制经由内存合成层（自己管理，或全部用 AlphaBlend 合成到主 DIB）
- **EDIT 处理**（关键取舍，二选一）：
  a. 保留 EDIT 子窗口： layered 主窗口 + 非 layered 子 EDIT 可显示但有白底问题 → 用 `WS_EX_LAYERED` 子窗口 + `SetLayeredWindowAttributes` 调色，仍生硬
  b. **自绘输入框**（推荐）：拦截 WM_CHAR/IME（`ImmGetContext/ImmSetCompositionWindow` 跟随光标）自绘文字+光标。中文输入法走 `WM_IME_COMPOSITION`（GCS_RESULTSTR）取结果串。工作量 +1 天，但换来整窗完全一致的玻璃质感
- 淡入动画改在 DIB alpha 上做（或保留 SetLayeredWindowAttributes 过渡期兼容）

### 4.2 质感件（每个都是独立可验证的小件）
1. **软阴影位图**：启动时按面板尺寸生成一张径向 alpha 阴影 32bpp DIB（圆角矩形扩张+径向衰减，两层），缓存；每帧 AlphaBlend 到主 DIB 一次。窗口 resize 时重生成
2. **顶部径向高光 / 渐变底**：生成一次 32bpp 渐变面板底位图（含 164° 双色线性 + 顶部径向叠加 + 1px 亮边），resize 重生成；平时 BitBlt/AlphaBlend 即可
3. **渐变激活胶囊**：`GradientFill`（msimg32）矩形+两端圆角 mask（或 GDI+ 圆角路径填充），柔光 = 预渲染 glow 椭圆 alpha 图 AlphaBlend 在胶囊下层
4. **玻璃格 hover 微光**：径向 alpha 图（一次生成）+ AlphaBlend；hover 上浮 = 绘制坐标 -2px，无需动画引擎
5. **抗锯齿**：外壳圆角/大图圆角 clip 用 GDI+（`GdipCreatePathRoundedRect` 思路：AddArc×4）或 2x 超采样缩回；文字维持 ClearType
6. **悬停大图浮层**：同软阴影件复用，280ms 定时已有（previewId 逻辑在交互层）

### 4.3 链接库变化
`build.bat` 增加：`msimg32.lib`（AlphaBlend/GradientFill）；可选 `gdiplus.lib`（若用 GDI+；纯 C flat API，`GdiplusStartup` 一次）。

### 4.4 性能预算（守住 v0.3 基线）
- 静态件（面板底/阴影/glow/渐变胶囊底）全部预渲染位图化：每帧只有 BitBlt/AlphaBlend 合成
- 预期每帧成本：<40 次 AlphaBlend（可见格数级）+ 1 次全窗 UpdateLayeredWindow——1280×800 32bpp 约 4MB/帧提交，实测通常仍 60fps；若抖动：脏区合成（只重绘变化矩形到 DIB 再整窗提交，UpdateLayeredWindow 本身只传 pDirtyRect）
- 常驻内存预估 +3~6MB（预渲染位图集），总量目标仍 ≤20MB

## 5. 验收清单（v0.4 完成的定义）

- [ ] 玻璃面板与壁纸正确合成（边缘无黑边/白框，DWM 下圆角抗锯齿）
- [ ] 阴影随窗口移动无残影；resize 后重生成正确
- [ ] 激活胶囊渐变+柔光；hover 微光与名字条；悬停 280ms 大图
- [ ] 搜索输入（含中文 IME）在自绘输入框下完整可用
- [ ] 热键呼出 <50ms；万张滚动 60fps；常驻 ≤20MB
- [ ] exe ≤ 64KB 软目标（预估 +8-15KB：新增 GDI+/msimg32 调用与预渲染代码）
