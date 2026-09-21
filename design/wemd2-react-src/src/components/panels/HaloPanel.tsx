import { CATEGORIES, filterMemes, getMeme } from "../../data/memes";
import type { PanelProps } from "../../panel-types";
import { MemeView } from "../MemeView";

// 「琉璃 HALO」— GDI 真实上限：AlphaBlend 软阴影 / GradientFill 渐变 / GDI+ 抗锯齿 / UpdateLayeredWindow 半透明
const INK = "#E7EBF6";
const DIM = "rgba(231,235,246,0.55)";
const FAINT = "rgba(231,235,246,0.32)";
const EDGE = "rgba(255,255,255,0.12)";
const EDGE_HI = "rgba(255,255,255,0.22)";
const ACCENT = "#7AA2FF";
const ACCENT2 = "#B28BFF";
const GLASS = "rgba(255,255,255,0.045)";
const REC = 68;
const CELL = 56;

export function HaloPanel(p: PanelProps) {
  const items = filterMemes(p.query, p.category);
  const recents = p.recents.map(getMeme).filter(Boolean);
  const hovered = p.hoverId ? getMeme(p.hoverId) : undefined;
  const preview = p.previewId ? getMeme(p.previewId) : undefined;

  return (
    <div
      style={{
        width: 700,
        height: 476,
        borderRadius: 18,
        // UpdateLayeredWindow 逐像素 alpha：整窗半透明 + 顶部一线径向高光
        background:
          "radial-gradient(620px 190px at 10% -6%, rgba(255,255,255,0.10), transparent 62%), linear-gradient(164deg, rgba(28,31,44,0.92), rgba(17,19,29,0.88))",
        border: `1px solid ${EDGE}`,
        // AlphaBlend 预计算阴影位图：近距 + 远投两层
        boxShadow:
          "inset 0 1px 0 rgba(255,255,255,0.13), 0 3px 8px rgba(0,0,0,0.30), 0 32px 72px rgba(0,0,0,0.52)",
        color: INK,
        fontFamily: "'Noto Sans SC', 'Segoe UI', sans-serif",
        display: "flex",
        flexDirection: "column",
        padding: "14px 16px 10px",
        userSelect: "none",
        position: "relative",
        overflow: "hidden",
      }}
      onMouseDown={(e) => {
        if ((e.target as HTMLElement).tagName !== "INPUT") e.preventDefault();
      }}
    >
      {/* 检索行：玻璃胶囊，聚焦时一线渐变光 */}
      <div style={{ display: "flex", alignItems: "center", gap: 12, marginBottom: 12 }}>
        <div
          style={{
            flex: 1,
            display: "flex",
            alignItems: "center",
            gap: 9,
            height: 38,
            borderRadius: 11,
            padding: "0 13px",
            background: "rgba(255,255,255,0.055)",
            border: `1px solid ${EDGE}`,
            boxShadow: "inset 0 2px 6px rgba(0,0,0,0.26)",
          }}
        >
          <span style={{ color: ACCENT, fontSize: 13 }}>⌕</span>
          <input
            autoFocus={p.autoFocus !== false}
            value={p.query}
            onChange={(e) => p.onQuery(e.target.value)}
            placeholder="搜索表情…"
            style={{
              flex: 1,
              background: "transparent",
              border: "none",
              outline: "none",
              fontSize: 13,
              color: INK,
              caretColor: ACCENT,
              fontFamily: "inherit",
              letterSpacing: "0.02em",
            }}
          />
          <span style={{ fontSize: 10.5, color: FAINT }}>Ctrl+Shift+.</span>
        </div>
        <span
          style={{
            fontSize: 11,
            color: FAINT,
            fontFamily: "'IBM Plex Mono', 'Consolas', monospace",
            minWidth: 64,
            textAlign: "right",
          }}
        >
          {items.length} items
        </span>
        <button
          type="button"
          onClick={p.onClose}
          style={{
            width: 28, height: 28, borderRadius: 9,
            background: GLASS, border: `1px solid ${EDGE}`,
            color: FAINT, fontSize: 13, lineHeight: 1, cursor: "pointer",
          }}
        >
          ×
        </button>
      </div>

      {/* 最近：数字键标是玻璃小片，hover 上浮加微光 */}
      <div style={{ fontSize: 10.5, color: FAINT, margin: "0 0 6px 3px", letterSpacing: "0.08em" }}>
        最近 · 按 1–7 直接发送
      </div>
      <div style={{ display: "flex", gap: 9, marginBottom: 12 }}>
        {recents.slice(0, 7).map((m, i) => {
          if (!m) return null;
          const on = p.hoverId === m.id || p.selectedId === m.id;
          return (
            <button
              type="button"
              key={m.id}
              onMouseEnter={() => p.onHover(m.id)}
              onMouseLeave={() => p.onHover(null)}
              onClick={() => p.onSend(m.id)}
              onContextMenu={(e) => {
                e.preventDefault();
                p.onContext(m.id, e.clientX, e.clientY);
              }}
              style={{
                width: REC, height: REC, borderRadius: 14, position: "relative", cursor: "pointer",
                border: `1px solid ${on ? "rgba(122,162,255,0.65)" : EDGE}`,
                background: on
                  ? "linear-gradient(150deg, rgba(122,162,255,0.20), rgba(255,255,255,0.02))"
                  : "linear-gradient(150deg, rgba(255,255,255,0.07), rgba(255,255,255,0.015))",
                boxShadow: on
                  ? "0 0 18px rgba(122,162,255,0.35), 0 6px 14px rgba(0,0,0,0.30), inset 0 1px 0 rgba(255,255,255,0.16)"
                  : "0 6px 14px rgba(0,0,0,0.28), inset 0 1px 0 rgba(255,255,255,0.12)",
                transform: on ? "translateY(-2px)" : "none",
                transition: "transform 90ms ease, box-shadow 90ms ease",
                overflow: "hidden",
              }}
            >
              <MemeView meme={m} size={REC} playing={p.hoverId === m.id && m.gif} />
              <span
                style={{
                  position: "absolute", left: 5, top: 5, fontSize: 10,
                  fontFamily: "'IBM Plex Mono', 'Consolas', monospace",
                  color: "#DFE6FF",
                  background: "rgba(13,15,23,0.72)",
                  border: "1px solid rgba(255,255,255,0.14)",
                  borderRadius: 5, padding: "0 5px",
                }}
              >
                {i + 1}
              </span>
            </button>
          );
        })}
      </div>

      {/* 标签：active 是 GradientFill 渐变胶囊，只给「当前」发光 */}
      <div style={{ display: "flex", gap: 7, marginBottom: 12 }}>
        {CATEGORIES.slice(0, 7).map((c) => {
          const on = c.id === p.category;
          return (
            <button
              type="button"
              key={c.id}
              onClick={() => p.onCategory(c.id)}
              style={{
                fontSize: 12,
                borderRadius: 99,
                padding: "4px 13px",
                cursor: "pointer",
                color: on ? "#EEF1FF" : DIM,
                border: `1px solid ${on ? "rgba(122,162,255,0.60)" : EDGE}`,
                background: on
                  ? "linear-gradient(120deg, rgba(122,162,255,0.30), rgba(178,139,255,0.22))"
                  : GLASS,
                boxShadow: on ? "0 0 16px rgba(122,162,255,0.30), inset 0 1px 0 rgba(255,255,255,0.10)" : "none",
                fontFamily: "inherit",
              }}
            >
              {c.name}
            </button>
          );
        })}
      </div>

      {/* 网格：玻璃格 + hover 亮边微光 + 名字条 */}
      <div
        className="thin-scroll"
        style={{
          flex: 1,
          overflow: "auto",
          display: "grid",
          gridTemplateColumns: `repeat(10, ${CELL}px)`,
          gap: 8,
          alignContent: "start",
          padding: "2px 2px 8px",
        }}
      >
        {items.map((m) => {
          const on = p.hoverId === m.id || p.selectedId === m.id || p.flashId === m.id;
          return (
            <button
              type="button"
              key={m.id}
              onMouseEnter={() => p.onHover(m.id)}
              onMouseLeave={() => p.onHover(null)}
              onClick={() => p.onSend(m.id)}
              onContextMenu={(e) => {
                e.preventDefault();
                p.onContext(m.id, e.clientX, e.clientY);
              }}
              style={{
                width: CELL, height: CELL, borderRadius: 11, position: "relative", cursor: "pointer",
                border: `1px solid ${on ? "rgba(122,162,255,0.75)" : "rgba(255,255,255,0.075)"}`,
                background: on
                  ? "radial-gradient(125% 125% at 30% 18%, rgba(122,162,255,0.20), rgba(255,255,255,0.03) 62%)"
                  : "rgba(255,255,255,0.03)",
                boxShadow: on
                  ? "0 0 18px rgba(122,162,255,0.32), 0 4px 10px rgba(0,0,0,0.26)"
                  : "inset 0 1px 0 rgba(255,255,255,0.06)",
                transform: on ? "translateY(-2px)" : "none",
                transition: "transform 90ms ease, box-shadow 90ms ease",
                overflow: "hidden",
              }}
            >
              <MemeView meme={m} size={CELL} playing={p.hoverId === m.id && m.gif} />
              {m.gif && (
                <span
                  style={{
                    position: "absolute", right: 3, bottom: 3,
                    fontSize: 8, letterSpacing: "0.08em",
                    color: "rgba(231,235,246,0.85)",
                    background: "rgba(13,15,23,0.66)",
                    border: "1px solid rgba(255,255,255,0.10)",
                    borderRadius: 4, padding: "0 3px",
                  }}
                >
                  GIF
                </span>
              )}
              {on && (
                <span
                  style={{
                    position: "absolute", left: 0, right: 0, bottom: 0,
                    fontSize: 9, color: "#fff", textAlign: "center",
                    background: "linear-gradient(transparent, rgba(8,9,14,0.9) 55%)",
                    padding: "9px 3px 3px",
                    whiteSpace: "nowrap", overflow: "hidden", textOverflow: "ellipsis",
                  }}
                >
                  {m.name}
                </span>
              )}
            </button>
          );
        })}
      </div>

      {/* 底部：题注式信息行 */}
      <div
        style={{
          display: "flex",
          alignItems: "center",
          gap: 10,
          padding: "8px 3px 2px",
          fontSize: 11,
          color: DIM,
        }}
      >
        {hovered ? (
          <>
            <span style={{ color: INK }}>{hovered.name}</span>
            <span style={{ color: FAINT }}>{hovered.tags.slice(0, 3).join(" / ")}</span>
          </>
        ) : (
          <span style={{ color: FAINT }}>单击即贴 · 右键管理标签 · 拖入导入</span>
        )}
        <span
          style={{
            marginLeft: "auto",
            fontFamily: "'IBM Plex Mono', 'Consolas', monospace",
            color: FAINT,
            fontSize: 10,
          }}
        >
          HALO · layered 32bpp
        </span>
      </div>

      {/* 悬停大图：软阴影浮层 */}
      {preview && (
        <div
          style={{
            position: "absolute",
            right: 14,
            bottom: 44,
            zIndex: 4,
            pointerEvents: "none",
          }}
        >
          <div
            style={{
              width: 168,
              height: 168,
              borderRadius: 14,
              overflow: "hidden",
              position: "relative",
              border: `1px solid ${EDGE_HI}`,
              boxShadow: "0 4px 10px rgba(0,0,0,0.35), 0 26px 56px rgba(0,0,0,0.55)",
            }}
          >
            <MemeView meme={preview} size={168} playing={preview.gif} />
          </div>
        </div>
      )}
    </div>
  );
}
