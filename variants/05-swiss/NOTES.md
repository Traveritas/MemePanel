# 05 瑞士网格 SWISS — 交付说明

STATUS: DONE

## v3 抽屉与横切（第三轮，「管理模式」删除 · 右键就地编辑）

### 改动清单

**数据模型 v2（src/store.c/h）**
- `Entry` 增加 `wchar_t* indexText;`（HeapAlloc，NULL=空）；`store_add` 置 NULL、`store_delete_at` 与加载失败路径均释放。
- `MP_VERSION` 1→2：每 entry 在 used 之后追加 u32 字符数 + UTF-16（0=空不跟数据）。
- **v1 兼容迁移**：ver==1 按旧布局读完（indexText=NULL），内存态直接可用，下次 store_save 自然写成 v2；ver 既非 1 也非 2 才整体丢弃。**实测**：v2 首次运行时 out/index.bin 为 v1，加载→store_scan→store_save 后变为 v2，42 entries / 2 tags / 38 tagged 全保留、indexText 全空（shot 注入不落盘）。

**管理模式删除（src/main.c）**
- 删除：`g_mgr`、`mgr_enter/mgr_exit/mgr_refresh`、`draw_mgr/draw_mgr_button`、`g_btnRc/BTN_*`、`g_status`、`g_renameEi`、F2 重命名整套（`name_valid/mgr_rename_begin/end`）、`-mgr` 参数、`IDM_MGR`。`mgr_rename_entry`→`file_rename_entry`（修后缀/抽屉共用）；`sel_*` 选择族保留（HeapSize 自适应容量）。

**检视抽屉（核心）**
- 布局：面板右侧加宽 S(300)，2px 黑竖线全高分隔（与横梁同权重；横梁/发丝线/标签行止于竖线）。**网格区几何与常态逐像素一致**（drawerLeft == 常态面板右缘），列数/格宽/最近行不变——探针 delta=375px 精确验证。
- 内容（pad S(20)，节奏 4/8）：单目标 = 96px 白衬纸预览（contain+1px 发丝框）+ 右侧 ~160px 元数据列（文件名 8.5px #555 单行省略、大小·格式 11px 灰 KB/MB 自算+扩展名大写、下划线「打开位置」= `ShellExecuteW explorer /select`，瞬间用 g_modal 抑制失焦收起）→「标签」节（11px 加重节标题 g_fSect + chips 自动换行：已打标黑底白字实心块/未打标灰字 hover 下划线；「+ 新标签」chip 原地变内联输入框，Enter=store_tag_add+应用于目标且保持展开连续建多个，Esc/失焦收起清空）→「索引文字」节（输入框视觉同搜索框，仅单目标可编辑，改动即时写 entry->indexText——HeapReAlloc 不接受 NULL 已分支处理——并 store_save；8.5px 灰提示）→ 底部删除按钮（accent 2px 描边；武装 3 秒变 accent 实底「确认删除」，**宽度固定按武装文案量取防跳动**；删后抽屉关闭+扫描+静默修后缀）。
- 批量态：顶部「已选 N 张」12px（去重结果附「· 疑似重复」）+ ✕ 清除选择；预览区变计数块（白衬纸+首图 contain 小缩略图——60×112 子矩形必然留白衬纸 letterbox+「N 张」）；标签 chips 按首个目标方向批量 toggle（沿用旧胶囊逻辑）；索引节显灰字「多选时不可编辑索引」。
- 目标规则：右键格在选择集内且 |集|≥2 → 批量；否则单目标（选择集不动；|集|=1 等价走单目标，保留索引可编辑）。Ctrl+点击网格=toggle 选择（**四角 2px accent 角标记** draw_sel_marks，区别于 hover 全框）；点击空白清选择（批量抽屉随之关）；普通点击=发送不变；网格区照常浏览/hover/滚轮；失焦仍整体隐藏（抽屉随之关）；右键网格空白=收起抽屉。
- 窗口几何：开=宽+S(300)、左缘不动、右缘出屏整体左移补偿；关=缩回同理（最小宽 S(400) 兜底）。悬停 168px 大图锚到**网格区**右下（gridR-S(14)），不与抽屉重叠；滚动条随 gridR。

**三缓冲输入焦点路由（IME/编辑最细部分）**
- `enum { EDIT_SEARCH, EDIT_INDEX, EDIT_NEWTAG }` + `EBuf{b[128],len,caret}`×3；WM_CHAR / GCS_RESULTSTR / 退格/删除/Home/End 全部经 `eb_active()` 作用于焦点缓冲；组合串 g_comp 同一时刻只画在焦点框（`compHere = focused && g_complen`）。
- 通知分派：搜索=refilter；索引=index_commit（即时写回+save）；新标签=仅重绘。`edit_set_focus` 离开 NEWTAG 时收起内联输入并清空（失焦=收起）。
- `ime_update_position` 按焦点取对应框 rect+缓冲 caret 位置（三框 rect 均在布局/绘制期生成）。
- 分层 Esc：抽屉输入框→回搜索框；抽屉开→关抽屉；否则隐藏面板。

**搜索语义 + prefs.cfg**
- `entry_matches`：标签名命中 ∪ indexText 命中 ∪（文件名 ⊕ 开关）；`refilter` 改用它。
- prefs.cfg = u32 'MPP1' + u32 flags（bit0=包含文件名，默认开），读写照抄 theme.cfg（.tmp+MoveFileExW 原子写）。**headless 实测**：默认 `-text cat` 有结果；写 flags=0 后同查询空墙出 LEER（252px 文字）、`-text 动` 仍命中 38 张（标签路径不受开关影响）。

**托盘/右键菜单重构**
- 托盘：打开面板 / 主题色 ▸ / **维护 ▸ 去重扫描…** / 搜索：包含文件名（checkable）/ 重新扫描 / 退出。「管理」删除。
- 去重：分桶+哈希算法原样搬迁；结果 = 重复项 sel_set + （隐藏则 panel_show）+ 打开抽屉批量态「已选 N 张 · 疑似重复」；无重复走托盘气泡。
- **修后缀自动化**：`maint_fixext` 无 UI，每次 store_scan 后调用（启动/重新扫描/导入/抽屉删除后）。
- 格子右键菜单：标签 toggle（显示名走 `tag_label` 德语化）+ 分隔线 + 「编辑…」（打开抽屉）。**ID 冲突历史 bug 修复**：TAG 2100-2163 / EDIT 2200 / THEME 2210-2215 / MAINT 2220 / SEARCHFLAG 2221，SHOW/RESCAN/EXIT 不动，全部互不相交。

**键盘导航**
- 方向键在网格内移动 `g_hover`（=键盘选中框，视觉与 hover 同源：accent 框+编号块，探针 -hover 12 验证）；越出可视区 g_first 按行滚动跟随；Enter=发送 hover≥0 ? 对应项 : 首个可见项；PageUp/Down 翻页保留。**焦点在抽屉输入框时方向键=caret 移动**（CJK 编辑需要），搜索焦点时方向键=网格导航（Home/End/Backspace 仍作用于搜索 caret，设计取舍见「已知不足」）。

**shot 无头参数**
- `-drawer N`（默认 5；目标 indexText 为空则注入 L"开心 猫 午睡" 便于验证填充态）、`-drawer batch`（选择 {2,5,9}+批量态）。其余照旧；`-mgr` 删除。

### v3 探针结果（probe.ps1 重写，6 张截图 79 项检查全 PASS）

- `shot_panel`（1185×872）：v2 全部基线检查原样保留 PASS（四角干净/横梁 3px/caret 171px/激活标签块/发丝线/accent 零越界）。
- `shot_hover(-hover 5)`：accent 框四边+编号+名字条灰字+168px 大图黑框。
- `shot_keyboard(-hover 12)`：第 13 格 accent 框四边+编号块（键盘态与 hover 同源）。
- `shot_drawer(-drawer 5)`（1560×872，delta=375±2 精确）：2px 竖分隔线两列 1730/1728 全黑；横梁止于分隔线（左黑右纸白）；预览 contain 内容 12768px+1px 发丝框 117px；元数据列三行全在（文件名 154px/大小 63px/下划线 70px）；「标签」节标题 184px+双实心 chips 3853px+「+ 新标签」空心态（txt 261px/solid 0）；索引框白底 8742px+黑下线 632px+填充文字 362px+无 caret（焦点在搜索）+提示 319px；删除按钮 accent 描边左 52px/上 114px+墨字 305px；网格区 caret/墙内容完好。
- `shot_drawer_batch`：3 格四角角标记（TL/BR 各 3 处 accent 命中，中边无 accent=非全框）；「已选 3 张」338px+✕ 45px；计数块内容 5101px+白衬纸 letterbox 3236px；TIERE 实心 1696px / KATZEN 空心（txt 326px/solid 0）两态并存；索引节灰字 406px 且无输入下线（0px）；删除描边在。
- `shot_blue_drawer(-accent 1F4AFF)`：caret/删除描边全蓝（B−R≥80）且这些位置零红；UI 区（含抽屉）零 #FF2B1E。
- 数据层端到端（见上）：v1→v2 迁移保留标签；prefs 开关改变搜索行为符合定义。
- 视觉复核（模型看图 2 次）：单/批量抽屉结构、分隔线、元数据、chips 两态、角标记均确认无重叠错位。

### v3 已知不足

- 搜索框内方向键被网格导航占用（caret 只能 Home/End/Backspace/点击编辑）——按「方向键=网格移动、Enter=速发」的产品方向取舍；若回归需要，加「焦点在搜索且有内容时左右键=caret」分支即可。
- 选择集只含 1 项且右键该项时按**单目标**呈现（与「目标=整个选择集」字面略有出入，但集合相同且保留了索引可编辑性，行为严格更优）。
- 抽屉开/合为瞬时 SetWindowPos，无 150ms 宽度过渡（设计文档列为可选优化）。
- 批量抽屉不做索引文字批量写（spec 如此：显示「多选时不可编辑索引」）；重命名功能随管理模式删除（用户不关心文件名；需要时可经「打开位置」在资源管理器改名，扫描自动跟进）。
- `-drawer batch` 的 {2,5,9} 为 0 基过滤索引（第 3/6/10 项）。
- 预览白衬纸在方图目标下被完全覆盖不可见（衬纸仍在，批量计数块的 letterbox 证明其存在）。
- g_sfCF 沿袭 v1 保留未用（无害）。

### v3 待真机项（无头无法验证）

- **IME 三缓冲路由真机行为**：组合窗/候选窗跟随三框位置、组合中切换焦点的组合串归属（当前归属切后的焦点缓冲）、Enter 上屏在索引/新标签框的表现。代码走查：WM_CHAR/RESULTSTR/COMPSTR/STARTCOMPOSITION 四路均经 eb_active()/ime_update_position() 焦点分派，一致。
- Ctrl+点击选择手感、武装删除 3 秒计时、抽屉开合时窗口左移补偿的视觉效果。
- 托盘「维护 ▸ 去重扫描…」真库耗时（42 张演示库瞬时）；重复项角标记+批量抽屉联动。

---

## v2 精修（第二轮，用户反馈驱动）

### 任务 1：删除四角多余文字（"左上、右上、左下、右下的文字都多余了"）

改动清单（src/main.c）：

- **删除标题行**：`draw_header()`（MEMEPANEL/VERWALTUNG + 42 BILDER 计数）整个移除，`g_title`、`g_fTitle` 一并删除。
- **新顶部结构**：面板顶边框下第一条内容行 = 搜索行（`searchT = PT + S(14)`）→ 全宽 S(2)=3px@120dpi 黑横梁（`beamY = searchB + S(8)`，现在唯一的重线条 = 新刊头，`draw_beam()`）→ 标签/操作行（`beamY + S(2) + S(10)`）→ 1px 发丝线 → 图片墙。
- **删除 footer 行**：`draw_footer()`（左悬停文件名 + 右`当前/总数`页码）整个移除，`g_footer` 删除；`gridB = panel.bottom - S(10)`，footer 空间还给图片墙；悬停大图改为锚定面板底边。管理模式状态行/右下计数文字同步删除（`g_status` 写入逻辑保留、不再绘制，机制不动待重设计）。
- **名字条淡化一档**：高 S(16)→S(14)，文字 9px→8.5px（新字体 `g_fNano = 17*dpi/192`），颜色 C_INK→#555555。
- 滚动条原样保留（track 随 gridB 延伸到底）。
- 命中矩形无变化面（g_searchBox/g_closeRc/g_chipRc/g_grid/g_btnRc 均由同一 layout 生成）。

### 任务 2：主题色可设置（#FF2B1E 不再硬编码）

- `static COLORREF g_accent = RGB(255,43,30)` + `acc_argb(a)`（COLORREF→GDI+ ARGB；**COLORREF 布局 0x00BBGGRR，低字节是 R**——第一轮曾写反导致 R/B 互换，已修）。`C_RED(a)` 宏重定义为 `acc_argb(a)`，全部红色使用点（hover 红框+编号块、激活标签前置小块、块状 caret、管理武装删除底色、图标左上小方块）自动跟随。
- **持久化**：exe 同目录 `theme.cfg` = u32 magic `0x31545054`('TPT1') + u32 RGB（R<<16|G<<8|B），共 8 字节。启动 `theme_load()`（无文件/短读/坏 magic = 默认正红）；变更 `theme_save()` 走 .tmp + `MoveFileExW(REPLACE_EXISTING)` 原子写（照抄 store_save 模式，无 HeapReAlloc 需求）。
- **设置入口**：托盘右键菜单新增「主题色」弹出子菜单：正红（默认）/钴蓝 #1F4AFF/常青 #008A3E/琥珀 #FF7A00/墨黑 #111111（当前项打勾）/「自定义…」。ID = IDM_THEME_BASE 2200..2205。选中即 `theme_apply()` = 生效 + 落盘 + repaint。
- **自定义对话框**：`LoadLibraryW("commdlg32.dll")` + `GetProcAddress("ChooseColorW")` 动态调用（**导入表仍是 7 个 DLL，零新增**，dumpbin 验证）；CHOOSECOLORW 来自 `#include <commdlg.h>`（纯结构）；Flags = CC_FULLOPEN|CC_RGBINIT，lpCustColors = 静态 16 格（首开初始化为预设色板）。对话框期间 `g_modal` 抑制 WM_ACTIVATEAPP 自动收起面板。
- **无头验证**：`-accent RRGGBB`（hex 大小写均可，`whex_` 解析；非 6 位 hex 忽略）只覆盖内存 g_accent，不写 theme.cfg。

### 演示数据二次修复（非代码）

`out/index.bin` 在某次历史运行后 nTags 归零（store_scan 每次运行会 store_save 重写索引，标签本应保留，丢失原因不明）。已再次按 v1 规则重建：tag0=动物、tag1=猫猫，cat-shock*→双标签，capybara/chicken/dog/frog/hamster/panda/rabbit/seal/dance/zzanim→动物，其余不打标；size/mtime/used 原样（both=4 / tier=34）。

### v2 探针结果（probe.ps1 重写，5 张截图 54 项检查全 PASS）

- `shot_panel`：标题带/底带零文字墨迹（0 px）；墙高 564→640 px、容量 8→9 行（+76px）；横梁 beamY=72 处 3 连续全宽黑行（frac≥0.9）；红 caret 171px、激活标签黑块 1888px+前置红块 64px、TIERE/KATZEN 289px；红色零越界（bad=0）。
- `shot_hover`：第 6 格红框 4/4+红编号；名字条白底（246,244,242）+ #555 灰字 35px；预览黑框锚定新底边；底带无红（footer 已删）。
- `shot_mgr_sel`：大格红框+红编号、按钮组 2570px、发丝线在；管理顶带/底带零文字（VERWALTUNG 与状态行已删）。
- `shot_blue / shot_blue_hover`（-accent 1F4AFF）：caret/前置方块/红框四边/编号全部变蓝（B−R≥80）且这些位置零红；UI 区无 #FF2B1E 邻域红（bad=0；图片区 93 个红像素为表情图自带内容，informational）。
- theme.cfg 读写实测（headless）：坏 magic→默认红 caret 171px；有效钴蓝条目→蓝 caret 171px（读路径端到端）；写路径为 store_save 逐字复刻（代码走查覆盖）。
- 视觉复核（模型看图 1 次）：确认无标题文字、搜索行+红块 caret 居首、全宽粗横梁、标签行、无 footer、四角干净、无错位。

### v2 已知不足

- 42 张演示图 < 单屏容量（16 列×9 行），墙的实际绘制止于第 3 行内容——版心下半留白是网格语义（发丝线不穿过无内容区），非 bug；但空态观感偏空，可考虑演示数据加量。
- ChooseColorW 对话框路径无法无头验证（弹框禁令），仅代码走查 + 导入表验证；theme_save 原子写同 store_save 模式，同为走查覆盖。
- 管理模式反馈（重命名提示/删除确认文案/去重结果）随状态行删除而不可见——按指示保留机制，待管理模式重设计轮处理。
- 预存在（未改动）：IDM_MGR=2100 与 IDM_TAG_BASE 相邻，格上右键菜单里 tag0 的命令会先命中 `>= IDM_TAG_BASE` 分支（v1 起如此，本轮未动）。

---

## v1 交付（第一轮，供追溯）

### 设计 token 摘要

| token | 值 |
|---|---|
| 面板底 | rgba(250,250,249,0.96) 纯白玻璃（预乘 240,240,239 / α245），方角、零阴影零渐变 |
| 外框 | 2 物理像素 #111111（1.5px 逻辑 @120dpi），四边矩形硬边 |
| 横梁 | ~~标题下~~ → v2：搜索行下 S(2)=3px 全宽 #111 实心条 |
| 内部网格线 | 1px rgba(17,17,17,0.15)（实测渲出 ≈209 vs 纸白 242，清晰可见），只画在格缝里 |
| 墨黑 | #111111 文字/按钮边/横梁/GIF 角标/滚动条 thumb |
| 强调色 | g_accent（默认正红 #FF2B1E；v2 起可经 theme.cfg/托盘/-accent 更换） |
| 字体 | 全站唯一家族 Segoe UI：标签 FW_SEMIBOLD S(12) 大写；正文 S(13)；小字 S(11)；角标 S(9)；名字条 8.5px（v2） |
| 网格数学 | cols = (可用宽+gap)/(minCell+gap)，cell = (可用宽-(cols-1)*gap)/cols —— 列精确吃满内容宽，右缘无残条 |
| 间距模数 | padX=S(20)，gap=S(3)，节奏全部 4/8 的倍数（S 值） |
| 左对齐铁律 | 标签/搜索框左缘 = 面板左 + S(20)（实测 ±2px） |

德语式命名：全部→ALLE、动物→TIERE、猫猫→KATZEN（`tag_label()`；未知标签自动转大写）。

### v1 结构改动（src/main.c，仅此一文件）

- **gen_base**：删除 164° 渐变 + 径向高光 + 圆角距离场，改为逐像素平铺纸白 + `frame_rect` 2px 黑外框。
- **panel_layout / 绘制**：详见上文 v2 段（标题/footer 已在 v2 删除）；draw_search_box（白底+黑下边线+强调色块状 caret+黑✕）/ draw_chip（大写文字标签，激活=黑底白字方块+前置强调色块，hover=黑下划线）/ draw_recents、draw_grid（密排墙+列/行发丝线+GIF 角标+hover 框/编号/名字条）/ draw_preview（方角+2px 黑框）/ draw_mgr_button（黑边直角，hover 黑底白字，武装=强调色底白字）。
- **删除**：rr_path/fill_rr/stroke_rr/glass_cell/draw_glow/make_glow/free_statics/g_penEdge/g_fMono（零圆角零发光后全部无用）。
- **rebuild_surface**：SmoothingMode=0 硬边；仅关闭按钮斜线 ✕ 临时 AA。
- **draw_image_contain**：GdipSetClipRectI 硬裁剪（方角）。
- **命中矩形**：全部由同一 panel_layout/draw 几何生成。
- **图标**：纸白方角底+黑粗框+左上强调色块+黑色笑脸。
- **IME**：组合窗位置随文本起点；组合串下划线黑色。

### v1 自检结果（已被 v2 探针取代，供追溯）

- 构建 `cmd //c build.bat` 通过；exe 55808 字节（上限 90KB）。
- 修复轮次：2 轮（AA 硬边化 + 演示标签数据修复 + chip 溢出保护恢复）。
- v1 已知不足：滚动条在标准截图不可见；标签 >~10 个时管理操作行可能争宽（不折行）；footer 分页为「当前可见数/总数」——footer 已在 v2 删除；`g_sfCF` 保留未用（无害）。

### v3.1 真机反馈修复（2026-09-19 午）

用户真机验收：多选 ✓ / 托盘与去重 ✓ / 键盘导航 ✓；两个 bug 已修：

1. **右键打不开抽屉**：格子右键菜单 TrackPopupMenu 传了 NULL owner（托盘菜单传 hwnd 所以正常）——NULL-owner 弹出菜单在置顶分层窗口下不显示/秒关。**基线 v0.3 起同款写法同病**（当年「管理…」点不到的另一半元凶）。修复：owner=hwnd + g_menuUp 期间抑制 WM_ACTIVATEAPP 自动收起 + PostMessage(WM_NULL)；托盘菜单同样补防护。
2. **IME 组合窗位置诡异**：焦点框 rect 空态回退（抽屉未绘制/历史残留时不定位）、屏幕坐标钳制进窗口矩形、rcArea 填窗口矩形（原来全零）、WM_IME_SETCONTEXT 激活瞬间也定位（部分输入法先于 START 定位）。真机复测仍异常的话需要用户提供截图进一步定位（数学路径与绘制已核对一致）。
3. 真机顺带验证：theme.cfg 持久化正确（用户选钴蓝→保存→后续运行仍钴蓝）；重建时结束了用户运行中的测试实例。

### v3.2 真机反馈二轮（2026-09-19 午后）

1. **右键直接展开抽屉**（用户决定）：原右键菜单整体移除——标签 toggle 与抽屉 chips 重复、仅剩「编辑…」一项，中间层多余。右键格子 = drawer_open_for（目标规则不变：格在选择集内→批量，否则单项）；右键空白仍收起抽屉。IDM_EDIT 废弃。
2. **IME 错位二轮修复**（用户提供截图：候选窗飘到网格第三行 x+650/y+200 并伸出面板右缘 = IME 完全无视我们设的位置）：①ImmSetCompositionFontW 喂字体（自绘框无 EDIT 字体信息，IME 缺字体会用自家默认锚点估算——头号嫌疑）；②组合窗 FORCE_POSITION 锚 caret 下方；③候选窗改 CFS_EXCLUDE 排除输入框屏幕矩形（CFS_CANDIDATEPOS 常被微软拼音无视——这正是飘走形态）；④空 rect 回退 + 钳制保留。待真机复测。

### v3.3 IME 定位仪表盘（2026-09-19）

两轮标准修法（组合字体 + CFS_EXCLUDE）后用户复测仍偏右下固定距离 → 停止盲试，上仪表：`-imedebug` 启动参数在面板上叠加调试层——把我们传给输入法的锚点画成 accent 十字、排除矩形画成蓝框、并直接渲染原始数字（focus / sp 屏幕坐标 / sp 客户区坐标 / 窗口原点）。用户打字截图后数字就在图里，一次定位坐标空间错位（屏幕 vs 客户区、窗口原点叠加、IME 内部变换）。视觉输出不受影响（仅 -imedebug 时绘制）。

### v3.4 IME 错位根治（2026-09-19，仪表定位后）

仪表数据（-imedebug 截图）：focus=1、锚点十字精确落在 caret、蓝框精确套住索引框、但候选窗右偏 40-80px/下偏 40-60px——右偏量 ≈ 组合串宽度。结论：**坐标传递正确，微软拼音把候选窗锚在「组合串末尾」**；组合串被自绘抑制后不可见，视觉即"向右漂一截"。修法：锚点 x 左移当前组合串宽度（gtext_w(g_comp)），让 IME 认知的串末尾落回真实 caret；随每击键（GCS_COMPSTR → ime_update_position）动态更新。垂直偏移为 IME 候选行高的正常堆叠。右键直开抽屉同轮真机确认正常。

### v3.4b 仪表结论（2026-09-19，等 v3.5 修复中）

三轮真机仪表数据汇总：sp 三次完全不变 (2071,310)，rc=(92,110,662,146)（570 宽的网格区旧矩形，非任何当前输入框），pre=0 comp=0（组合中却 comp=0 = 调用发生在 g_comp 清零后或过期快照）。本地无头复现 rect 正确（截图流程先画一帧）→ 真机时序下拿到**绘制期赋值的过期 rect** 是唯一自洽解释。修复方向 = rect 计算迁入 panel_layout 单一数据源 + 关闭/批量态清零（v3.5，子智能体执行中）。若修后真机 rc 正确而候选窗仍飘 → 锤死微软拼音无视 IMM32 定位 → 上自绘候选窗。

### v3.5 rect 单一数据源（2026-09-19，绘制期赋值根治）

**病因（仪表已定位）**：检视抽屉三输入框 rect（g_searchBox / g_dIndexBox / g_dNewTagBox）原先主要在**绘制函数里**赋值（draw_input_box 调用处 / draw_drawer 内联布局）；ime_update_position 在输入法消息到来时读取，拿到的是过期残留（真机实测 focus=1 时 rc=(92,110,662,146)，570 宽旧布局矩形 → 候选窗飘进网格区）。本地无头恰好正确只因截图流程先画了一帧。

**改动清单（仅 src/main.c）**

- 新增 `static void drawer_layout(void)`（定义在 tag_label 之后）：抽屉全部内容 rect 的唯一计算点——批量头部 ✕（g_dClearRc）/计数块（g_dPrevRc）/单目标预览+元数据三行（g_dPrevRc/g_dNameRc/g_dSizeRc/g_dOpenRc）→「标签」节标题 y（新静态 g_dSecY）→ chips 换行全序列（g_dChipRc[]/g_dChipId[]/g_nDChips，宽度 = `gtext_w(label,-1,g_fUi)+S(16)`，与绘制**同字体同参数**，命中与像素逐像素一致）→「+ 新标签」chip/内联输入框（g_dNewChipRc/g_dNewTagBox，g_newtagOpen 两态同一段换行逻辑）→「索引文字」节标题 y（g_dIdxY）+ 索引框（g_dIndexBox）→ 删除钮（g_dDelRc，仍按武装文案定宽）。**函数开头无条件全量清零**；关闭 / 批量（索引框不可编辑）/ 目标失效（扫描后越界）三种态天然零残留。
- 触发点五个：panel_layout 末尾（尺寸变化/开关抽屉）；panel_repaint 开头（新标签展开/加标签/切换目标等状态变化后每帧同步，等价替代旧「绘制期重排」）；drawer_set_open(FALSE) 显式清零；drawer_reset_state（panel_hide 会话结束清零，防跨会话残留）；edit_set_focus 离开 NEWTAG 收起内联输入时同步（收起后 chips 行数/索引框 y 会变，紧随其后的 ime_update_position 即读到新几何）。
- draw_drawer / draw_dchip 改纯消费者：draw_dchip 签名 `(int*,int*,txt,on,id)` → `(RECT rc, txt, on)`（不再登记命中）；draw_drawer 内所有 `g_d* = ` 赋值删除（grep 验证：赋值仅存 drawer_layout 一处），节标题/批量灰注/索引提示位置改读 g_dSecY/g_dIdxY/g_dIndexBox.bottom。视觉零变化。
- 搜索框 rect 本就在 panel_layout（确认，未动）。
- ime_update_position 未改（空 rect 回退保留）；shot 无头流程删除「先画一帧」的 panel_repaint 补丁及其注释——rect 已由 panel_layout 在布局期算好，这正是真机 bug 的复现条件差异被消除的证明。
- 构建：`cmd //c build.bat` 零警告零错误，out/MemePanel.exe 65536 字节。（构建前遇 LNK1104：上一真机调试实例仍在跑，按纪律 taskkill /IM MemePanel.exe /F 后通过。）

**环境注记**：out/theme.cfg 在真机主题持久化验证（v3.1）后残留钴蓝 #1F4AFF，导致默认截图的红基线探针 22 项 FAIL（accent 元素全蓝）；已按基线恢复默认正红 FF2B1E（TPT1+RGB 各 4 字节），非代码问题。

**探针结果**

- 新增 probe_layout.ps1（12 项全 PASS，ASCII）：shot_layout1（-drawer 5 -imedebug -accent FF2B1E）accent 十字横向连续 28px@起点(1251,287)、竖臂中心列 1265 共 26px，均在索引框 caret 区（x>1200, y 240..320）；蓝排除框左上角 (1208,258) = drawer_layout 算出的 g_dIndexBox 左上角**逐像素精确**；索引框黑下线 628px、填充文字「开心 猫 午睡」553px 仍在旧探针位置（y≈294）。shot_layout2（-drawer batch）：抽屉区零十字、零蓝框（索引框 rect 已清零 → 无错位定位），chips/计数块完好。shot_layout3（不开抽屉）：不崩溃，搜索 caret 171px/横梁 3px/激活标签块 1888px 与基线一致。
- probe.ps1：ALL CHECKS PASSED（6 张 79 项，数值与 v3 记录逐一相同——几何未动）。probe_ime.ps1：十字 (1264,275)、蓝框 (1208,258)，与 shot_layout1 交叉印证。
- 三张 shot_layout*.png 与上一版对应截图字节数完全相同（247998/224615/195559），旁证零像素漂移。

**已知不足 / 待真机**

- 真机复测项：焦点=索引框时仪表 rc 应为 (1208,258,1531,296)（不再是 570 宽旧矩形）；候选窗跟随 caret。若修后 rc 正确而候选窗仍右飘 → 结论升级为微软拼音无视 IMM32 定位，需自绘候选窗（v3.4b 预案）。
- 点击命中仍读「上一帧」rect（点击 → 命中 → 状态变化 → 重绘的既有时序，与本次修复无关；edit_set_focus 路径已例外收紧到新几何）。
- drawer_layout 每帧全量重算（≤33 次 gtext_w 量宽），与旧 draw_drawer 绘制期逐 chip 量宽成本相当，无回归。


### v3.6 构建戳（2026-09-19）

真机三轮仪表数字与 v3.5 前完全相同（rc=(92,110,662,146) 在 v3.5 代码里不可能存在；本地 v3.5 无头读数为 rc=(1208,258,1531,296) 正确）→ 判定用户运行的是旧进程（单实例常驻托盘，旧实例不退出时新双击只弹「已在运行」）。调试行加 __TIME__ 构建戳（B13:59:xx f=... 格式），今后截图自报 exe 版本。

### v3.7 IME 偏移实验装置（2026-09-19）

用户关键线索：换输入法偏移依旧一样（排除单一 IME 怪癖 → 病因在我们窗口的共性）；且窗口位置从未变过（无法排除坐标系随位置变化的可能）。装置：①`-pos X,Y` 窗口定位覆盖；②imedebug 模式画绿色参考十字 A(100,100)/B(600,300)/C(1100,500)（客户区已知坐标）→ 截图可像素级解出 IME 浮窗的真实变换（固定偏移/随位置缩放/随拼音长度变化）。隐形 caret 保留在真实光标位（不加补偿，先测净行为）。

### v3.8 IME 偏移破案（2026-09-19，-pos 三组实验）

三组 -pos 实验（窗口 687,145 / 100,100 / 2003,205）浮窗全部 = 窗口原点 + (103,117)，与窗口位置/输入法/拼音长度全部无关 → 候选窗 = CFS_EXCLUDE 排除矩形的**邻接摆放**（所有 CTF 输入法都遵守；抽屉会话的「紧贴索引框下缘」观察亦吻合）。病根：v3.2 起把整个输入框作为排除矩形 → 候选窗摆在「框」旁而非「光标」旁。修复：排除矩形改为 caret 点（8×20 屏幕小矩形）→ 邻接=贴光标。compW 补偿（错误理论）已移除。

### v3.10 IMR_QUERYCHARPOSITION 应答（2026-09-19）

v3.9 日志铁证：imedef 搬迁成功且稳定、caret 就位、sp 正确——候选窗依然 win+(103,117) 不动。「设置」类 IMM32 接口全灭后，启用从未应答过的「询问」协议：WM_IME_REQUEST/IMR_QUERYCHARPOSITION（输入法向自绘文本应用查询组合字符屏幕位置的官方机制；之前返回 FALSE=没答案→默认位）。应答内容：pt=焦点框文本起点+前缀宽+组合串前 dwPos 字符宽，cLineHeight=框高，rcDocument=框屏幕矩形。日志记 IMR_QUERY dwPos=... 行以验证输入法是否真的来问。若仍无效 → 唯一余路 TSF（ITextStoreACP 最小实现）。

### ✅ v3.10 结案（2026-09-19）：IMR_QUERYCHARPOSITION 应答 = 根治

**用户真机确认修复成功。** 完整因果链（9 轮排查的最终结论）：
- 症状：候选窗永远钉在窗口原点 + (103,117)，与输入法品牌、窗口位置、拼音长度全部无关。
- 根因：自绘输入框应用从未应答 WM_IME_REQUEST/IMR_QUERYCHARPOSITION（输入法询问组合字符屏幕位置的官方协议）→ 输入法拿不到答案 → 全部退回默认摆放 (103,117)。
- 修复：WndProc 增加 IMR_QUERYCHARPOSITION 分支——按询问的 dwCharPos 精确回答「文本起点+已提交前缀宽+组合串前 N 字符宽」的屏幕坐标 + 行高 + 文档矩形，返回 TRUE。
- 排查路径上被证明无效/冗余但保留的（工作状态珍贵，不再动）：CFS_FORCE_POSITION/CFS_EXCLUDE（被无视）、组合字体设置、隐形系统 caret（无害，某些输入法可能用）、「IME」默认窗搬迁（无效但无害）。清理留待合并主线时评审。
- 方法论教训：**应答类协议（IME 问我们答）与设置类接口（我们单向通知）是两个世界；输入法 UI 不听广播只等回电。** 版本角标 + 启动留痕日志 + run.bat 三件套防住了新旧进程混淆这个反复坑；-pos 实验 + 参考十字是定位坐标类 bug 的利器。
