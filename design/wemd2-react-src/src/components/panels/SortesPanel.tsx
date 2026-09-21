import { CATEGORIES, filterMemes, getMeme } from "../../data/memes";
import type { PanelProps } from "../../panel-types";
import { GifMark, MemeView } from "../MemeView";

const INK = "#1C1916";
const PAPER = "#EFE8DC";
const VERM = "#B43A2F";
const RULE = "rgba(28,25,22,0.18)";
const CELL = 56;
const GAP = 6;

export function SortesPanel(p: PanelProps) {
  const items = filterMemes(p.query, p.category);
  const recents = p.recents.map(getMeme).filter(Boolean);
  const cat = CATEGORIES.find((c) => c.id === p.category);

  return (
    <div
      style={{
        width: 712,
        height: 476,
        background: PAPER,
        color: INK,
        border: `1px solid ${INK}`,
        boxShadow: `3px 3px 0 0 ${INK}`,
        fontFamily: "'Noto Serif SC', serif",
        display: "flex",
        flexDirection: "column",
        userSelect: "none",
        position: "relative",
        overflow: "hidden",
      }}
      onMouseDown={(e) => {
        if ((e.target as HTMLElement).tagName !== "INPUT") e.preventDefault();
      }}
    >
      <header
        style={{
          height: 28,
          display: "flex",
          alignItems: "center",
          padding: "0 10px 0 8px",
          borderBottom: `1px solid ${INK}`,
          gap: 8,
          flexShrink: 0,
        }}
      >
        <span
          style={{
            width: 16,
            height: 16,
            background: VERM,
            color: PAPER,
            fontSize: 11,
            display: "flex",
            alignItems: "center",
            justifyContent: "center",
            lineHeight: 1,
            fontWeight: 600,
          }}
        >
          帖
        </span>
        <span style={{ fontSize: 12, letterSpacing: "0.14em" }}>即帖 · 活字</span>
        <span className="tabular" style={{ marginLeft: "auto", fontSize: 11, opacity: 0.45, fontFamily: "'IBM Plex Mono', monospace" }}>
          Ctrl+Shift+M
        </span>
        <button
          type="button"
          onClick={p.onClose}
          style={{ fontSize: 12, marginLeft: 12, opacity: 0.7 }}
        >
          收起
        </button>
      </header>

      <div style={{ display: "flex", flex: 1, minHeight: 0 }}>
        <aside
          style={{
            width: 100,
            borderRight: `1px solid ${INK}`,
            display: "flex",
            flexDirection: "column",
            padding: "8px 0 8px",
            flexShrink: 0,
          }}
        >
          <div
            style={{
              fontSize: 10,
              letterSpacing: "0.22em",
              opacity: 0.4,
              padding: "0 12px 8px",
            }}
          >
            索引
          </div>
          {CATEGORIES.map((c) => {
            const on = c.id === p.category;
            return (
              <button
                type="button"
                key={c.id}
                onClick={() => p.onCategory(c.id)}
                style={{
                  display: "flex",
                  alignItems: "baseline",
                  padding: "5px 10px 5px 12px",
                  fontSize: 12,
                  background: on ? "rgba(180,58,47,0.08)" : "transparent",
                  position: "relative",
                  textAlign: "left",
                }}
              >
                {on && (
                  <i
                    style={{
                      position: "absolute",
                      left: 0,
                      top: 7,
                      width: 2,
                      height: 12,
                      background: VERM,
                    }}
                  />
                )}
                <span style={{ color: on ? VERM : INK }}>{c.name}</span>
                <span
                  className="tabular"
                  style={{
                    marginLeft: "auto",
                    fontSize: 10,
                    opacity: 0.4,
                    fontFamily: "'IBM Plex Mono', monospace",
                  }}
                >
                  {c.count}
                </span>
              </button>
            );
          })}
          <div style={{ marginTop: "auto", padding: "8px 8px 0" }}>
            <div
              style={{
                border: `1px dashed ${RULE}`,
                height: 44,
                display: "flex",
                alignItems: "center",
                justifyContent: "center",
                fontSize: 10,
                opacity: 0.45,
                letterSpacing: "0.04em",
                lineHeight: 1.35,
                textAlign: "center",
              }}
            >
              拖入导入
            </div>
          </div>
        </aside>

        <section style={{ flex: 1, minWidth: 0, display: "flex", flexDirection: "column" }}>
          <div
            style={{
              height: 36,
              display: "flex",
              alignItems: "center",
              padding: "0 12px",
              borderBottom: `1px solid ${RULE}`,
              gap: 10,
            }}
          >
            <span style={{ fontSize: 12, letterSpacing: "0.16em", opacity: 0.55 }}>检索</span>
            <div style={{ flex: 1, position: "relative", height: 22 }}>
              <input
                autoFocus={p.autoFocus !== false}
                value={p.query}
                onChange={(e) => p.onQuery(e.target.value)}
                placeholder="猫 / 急了 / 代码"
                style={{
                  width: "100%",
                  height: 22,
                  background: "transparent",
                  border: "none",
                  outline: "none",
                  borderBottom: `1px solid ${INK}`,
                  fontSize: 13,
                  padding: 0,
                  borderRadius: 0,
                }}
              />
            </div>
            <span
              className="tabular"
              style={{ fontSize: 11, opacity: 0.4, fontFamily: "'IBM Plex Mono', monospace" }}
            >
              {items.length}
            </span>
          </div>

          <div style={{ padding: "8px 12px 4px" }}>
            <div
              style={{
                fontSize: 10,
                letterSpacing: "0.2em",
                opacity: 0.4,
                marginBottom: 6,
              }}
            >
              最近
            </div>
            <div style={{ display: "flex", gap: GAP }}>
              {recents.slice(0, 8).map((m, i) => {
                if (!m) return null;
                const on = p.selectedId === m.id || p.hoverId === m.id;
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
                      width: CELL,
                      height: CELL,
                      position: "relative",
                      outline: on ? `1px solid ${VERM}` : `1px solid ${RULE}`,
                      outlineOffset: 0,
                    }}
                  >
                    <MemeView meme={m} size={CELL} playing={p.hoverId === m.id && m.gif} />
                    <span
                      style={{
                        position: "absolute",
                        top: 0,
                        left: 0,
                        width: 12,
                        height: 12,
                        background: VERM,
                        color: PAPER,
                        fontSize: 9,
                        lineHeight: "12px",
                        fontFamily: "'IBM Plex Mono', monospace",
                      }}
                    >
                      {i + 1}
                    </span>
                    {m.gif && <GifMark color={PAPER} bg={VERM} />}
                  </button>
                );
              })}
            </div>
          </div>

          <div
            className="thin-scroll"
            style={{
              flex: 1,
              overflow: "auto",
              padding: "8px 12px 8px",
              color: "rgba(28,25,22,0.35)",
            }}
          >
            <div
              style={{
                display: "grid",
                gridTemplateColumns: `repeat(9, ${CELL}px)`,
                gap: GAP,
              }}
            >
              {items.map((m) => {
                const on = p.selectedId === m.id || p.hoverId === m.id;
                const flash = p.flashId === m.id;
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
                      width: CELL,
                      height: CELL,
                      position: "relative",
                      outline: flash || on ? `1px solid ${VERM}` : `1px solid ${RULE}`,
                      outlineOffset: 0,
                    }}
                  >
                    <MemeView meme={m} size={CELL} playing={p.hoverId === m.id && m.gif} />
                    {m.gif && <GifMark color={PAPER} bg={VERM} />}
                  </button>
                );
              })}
            </div>
          </div>
        </section>
      </div>

      <footer
        style={{
          height: 24,
          borderTop: `1px solid ${INK}`,
          display: "flex",
          alignItems: "center",
          padding: "0 10px",
          fontSize: 11,
          flexShrink: 0,
        }}
      >
        <span>
          {cat?.name ?? "全部"}
          <span className="tabular" style={{ opacity: 0.5, marginLeft: 8, fontFamily: "'IBM Plex Mono', monospace" }}>
            {cat?.count.toLocaleString()} 张
          </span>
        </span>
        <span style={{ marginLeft: "auto", opacity: 0.45, letterSpacing: "0.04em" }}>
          单击即贴 · 右键打标 · Esc
        </span>
      </footer>

      {p.previewId && getMeme(p.previewId) && (
        <Preview memeId={p.previewId} />
      )}
    </div>
  );
}

function Preview({ memeId }: { memeId: string }) {
  const m = getMeme(memeId);
  if (!m) return null;
  return (
    <div
      style={{
        position: "absolute",
        right: 14,
        bottom: 36,
        width: 148,
        background: PAPER,
        border: `1px solid ${INK}`,
        boxShadow: `3px 3px 0 0 ${INK}`,
        zIndex: 4,
        pointerEvents: "none",
      }}
    >
      <MemeView meme={m} size={146} playing={m.gif} />
      <div
        style={{
          borderTop: `1px solid ${INK}`,
          padding: "5px 8px 6px",
          fontSize: 11,
        }}
      >
        <div>{m.name}</div>
        <div style={{ opacity: 0.45, fontSize: 10, marginTop: 2 }}>{m.tags.join(" · ")}</div>
      </div>
    </div>
  );
}
