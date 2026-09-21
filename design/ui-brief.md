# MemePanel · UI 设计需求文档（交给设计 AI）

> 用途：把本文档交给另一个 AI，请它产出 3-4 个方向的高保真 UI 创意设计稿（图片）。
> 本文只定义"要解决什么"和硬约束，**视觉风格完全开放**。

---

## 1. 产品一句话

**Windows 桌面表情包速发面板**：常驻系统托盘，全局热键（Ctrl+Shift+.）呼出一个悬浮面板，浏览/搜索/筛选自己的表情包收藏（以 GIF 动图为主，也有 JPG/PNG），点击任意一张即自动粘贴发送到当前聊天窗口（QQ/微信/Telegram）。点击到发出的全程目标 ≤2 秒。

## 2. 用户与使用场景

- 中文互联网用户，重度斗图爱好者；收藏量 500–5000 张，持续增长。
- **两种使用模式**（设计需同时照顾）：
  - **速发（70%）**：高频复用固定的几十张梗图，要求"呼出→点一下→消失"，越快越好；熟到可以不看面板盲点。
  - **逛选（30%）**：想找乐子/找反击灵感，慢慢浏览、按标签翻、被意外的图击中。
- 面板是**临时浮层**：出现时不打断聊天窗口，用完即隐（Esc 或点击外部自动收起）。它更像"抽出来的一叠牌"，而不是一个常驻应用窗口。

## 3. 界面必须承载的功能元素

| 元素 | 说明 | 必选/可选 |
|---|---|---|
| 表情缩略图网格 | 主体。GIF 显示首帧即可；格子大小需要平衡"密度"与"看清楚" | 必选 |
| 最近使用 | 排最前的横条或区块——高频复用是第一使用模式，参考 Discord/微信表情面板的共识 | 必选 |
| 搜索框 | 按文件名搜索，中文输入 | 必选 |
| 标签筛选 | 用户标签（如"猫猫/熊猫头/代码/流汗系列"），以胶囊/侧栏/任何你觉得对的形式呈现 | 必选 |
| hover 态 | 显示文件名；有"可以点"的反馈 | 必选 |
| 使用频率的视觉化 | 可选加分项：用过的图在视觉上有存在感差异（大小/位置/角标等，形式不限） | 可选 |
| 计数/状态信息 | "N 张 · 当前标签"这类轻量信息 | 可选 |

不需要：设置页、多选批量操作、用户头像、登录、任何账号体系。

## 4. 内容长什么样（重要，影响配色判断）

- 格子里是**互联网梗图**：表情包截图、猫猫/熊猫头斗图、综艺截图、沙雕 GIF——颜色杂、亮度不一、无统一色调。
- 文件名是中文梗名："猫猫敲键盘.gif""小丑竟是我.gif""流汗黄豆.png""典.jpg"。
- 这意味着：**界面底色需要能"托住"五颜六色的杂图**（设计师自行判断深色还是浅色）。

## 5. 设计目标（气质关键词）

- **有记忆点**：一眼记住、截图发群里别人会问"这是什么软件"。它是关于"玩"的工具，应该有幽默感或愉悦感，但克制——不是玩具，是每天用几十次的效率工具。
- **极简且五脏俱全**：信息密度高但不拥挤；每个像素都有理由。
- **快的感觉**：还没点开就能感觉到它"很轻很快"。
- 面板尺寸建议：桌面悬浮面板，约 **880×560 px**（4:3 偏横）；也接受更有创意的形态（横条、竖抽屉等），但需说明理由。

## 6. 硬性技术约束（落地为纯 Win32 GDI 自绘，请认真遵守）

可以做：纯色填充、圆角、描边、**硬阴影（无模糊的偏移阴影）**、细网格纹理/点阵图案、图片微旋转、大小混排、任意字体排印。
做不到（请避免出现在设计中）：毛玻璃/高斯模糊背景、复杂多色渐变（简单双色渐变勉强可以）、实时粒子/发光、滚动阻尼动画、视差。

## 7. 负面清单（已试过并被否掉的方向）

- ❌ 通用 Raycast/Alfred 式"深色圆角面板+列表"（被评价为"没有个性、平平淡淡"）
- ❌ 新粗野主义+贴纸/拍立得主题化（米色纸板、斜贴拍立得——被评价为"为主题化而主题化"）
- ❌ 炫技式交互组件（鱼眼放大、条码缩略等——被评价为"为了设计而设计"）
- ✅ 正确姿势：**形态克制、常规组件（网格/chips/搜索），但整体气质独特**；创意体现在气质、排印、色彩、细节节奏上，而不是发明新控件。

## 8. 产出要求

- 3-4 个**风格差异明显**的方向，每方向一张完整面板设计稿（含真实感内容：梗图缩略图、中文文件名、标签）。
- 每张附 ≤50 字的设计意图说明。
- 至少一个方向展示 hover/交互态细节（局部放大即可）。
- 桌面环境呈现：面板悬浮在模糊的桌面/聊天窗口之上，体现"浮层"感。

---

## 附：英文 Prompt（可直接粘贴给生图 AI）

```
Design 3-4 high-fidelity UI concepts for "MemePanel" — a Windows desktop quick-paste panel for meme stickers (mostly animated GIFs), invoked by a global hotkey, floating above the desktop/chat window like a summoned card deck.

FUNCTIONAL ELEMENTS (must all appear): a thumbnail grid of internet meme images (cat memes, panda-head reaction pics, variety-show screenshots — colorful, messy content); a "Recent" strip at the top (most-used memes, Discord-style recents-first logic); a search box; tag filter chips (Chinese labels: 全部 / 猫猫 / 熊猫头 / 代码 / 流汗系列); Chinese meme filenames under hovered thumbnails (e.g. 猫猫敲键盘.gif, 小丑竟是我.gif); a subtle count ("128 张").

CONTEXT: Chinese power user, heavy sticker-battle culture, panel is a transient overlay (dismiss with Esc), goal = find & paste a meme into QQ/WeChat within 2 seconds. Panel size ~880×560px floating over a blurred desktop.

DESIGN DIRECTION: each concept must feel memorable and full of personality — this is a tool about PLAY, with humor and joy, yet precise and lightweight (used dozens of times daily). Typography, color attitude, spatial rhythm and small details should carry the personality; use conventional components (grid, chips, search) — do NOT invent gimmicky controls.

HARD TECH CONSTRAINTS (will be reimplemented in plain Win32 GDI): allowed — solid fills, rounded corners, hard offset shadows (no blur), subtle dot/halftone patterns, slight image rotation, mixed tile sizes, expressive typography. NOT allowed — glassmorphism, gaussian blur, complex gradients, glow, particles.

AVOID: generic Raycast/Alfred dark launcher look; neo-brutalist cream paperboard theme; novelty interactions. 

Deliver: 3-4 clearly different style directions, each a full-panel hero shot with realistic meme content and Chinese labels, plus a short intent note; include at least one hover-state detail close-up.
```
