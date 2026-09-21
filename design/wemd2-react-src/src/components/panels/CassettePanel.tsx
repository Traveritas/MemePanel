import { CATEGORIES, filterMemes, getMeme } from "../../data/memes";
import type { PanelProps } from "../../panel-types";
import { MemeView } from "../MemeView";

const BG = "#1A1612";
const WELL = "#12100E";
const INK = "#E8DCC8";
const BRASS = "#B08A4F";
const LINE = "#3A3228";
const LARGE = 72;
const SMALL = 36;

export function CassettePanel(p: PanelProps) {
  const items = filterMemes(p.query, p.category);
  const recents = p.recents.map(getMeme).filter(Boolean);
  const cat = CATEGORIES.find((c) => c.id === p.category);
  const hovered = p.hoverId ? getMeme(p.hoverId) : undefined;

  return (
    <div
      style={{
        width: 548,
        height: 368,
        background: BG,
        color: INK,
        border: `1px solid ${LINE}`,
        borderRadius: 6,
        fontFamily: "'Noto Sans SC', sans-serif",
        display: "flex",
        overflow: "hidden",
        userSelect: "none",
        position: "relative",
        boxShadow: "4px 6px 0 0 rgba(0,0,0,0.45)",
      }}
      onMouseDown={(e) => {
        if ((e.target as HTMLElement).tagName !== "INPUT") e.preventDefault();
      }}
    >
      <div style={{ width: 5, background: BRASS, flexShrink: 0 }} />
      <div style={{ flex: 1, display: "flex", flexDirection: "column", minWidth: 0 }}>
        <header
          style={{
            height: 28,
            display: "flex",
            alignItems: "center",
            padding: "0 10px 0 8px",
            gap: 10,
          }}
        >
          <span
            style={{
              fontFamily: "'Noto Serif SC', serif",
              fontSize: 13,
              color: BRASS,
              letterSpacing: "0.28em",
            }}
          >
            匣
          </span>
          <span
            style={{
              fontSize: 10,
              letterSpacing: "0.18em",
              opacity: 0.45,
              fontFamily: "'IBM Plex Mono', monospace",
            }}
          >
            JÌTIĒ
          </span>
          <span
            className="tabular"
            style={{
              marginLeft: "auto",
              fontSize: 11,
              color: BRASS,
              fontFamily: "'IBM Plex Mono', monospace",
            }}
          >
            {cat?.count.toLocaleString()}
          </span>
          <button type="button" onClick={p.onClose} style={{ fontSize: 11, opacity: 0.5, marginLeft: 8 }}>
            收
          </button>
        </header>

        <div style={{ padding: "0 10px 8px", display: "flex", gap: 8, alignItems: "center" }}>
          <div
            style={{
              flex: 1,
              height: 26,
              background: WELL,
              border: `1px solid #0A0908`,
              boxShadow: "inset 0 1px 0 0 #000",
              display: "flex",
              alignItems: "center",
              padding: "0 8px",
              gap: 6,
            }}
          >
            <span style={{ fontSize: 10, color: BRASS, letterSpacing: "0.14em" }}>检索</span>
            <input
              autoFocus={p.autoFocus !== false}
              value={p.query}
              onChange={(e) => p.onQuery(e.target.value)}
              placeholder="…"
              style={{
                flex: 1,
                background: "transparent",
                border: "none",
                outline: "none",
                fontSize: 12,
                padding: 0,
                color: INK,
              }}
            />
          </div>
        </div>

        <div style={{ padding: "0 10px 8px", display: "flex", gap: 12, flexWrap: "wrap" }}>
          {CATEGORIES.slice(0, 6).map((c) => {
            const on = c.id === p.category;
            return (
              <button
                type="button"
                key={c.id}
                onClick={() => p.onCategory(c.id)}
                style={{
                  fontSize: 11,
                  letterSpacing: "0.08em",
                  color: on ? BRASS : "rgba(232,220,200,0.5)",
                  borderBottom: on ? `1px solid ${BRASS}` : "1px solid transparent",
                  paddingBottom: 2,
                }}
              >
                {c.name}
              </button>
            );
          })}
        </div>

        <div style={{ padding: "0 10px 10px" }}>
          <div
            style={{
              fontSize: 9,
              letterSpacing: "0.22em",
              color: BRASS,
              marginBottom: 6,
              opacity: 0.8,
            }}
          >
            最近
          </div>
          <div style={{ display: "flex", gap: 6 }}>
            {recents.slice(0, 6).map((m, i) => {
              if (!m) return null;
              const on = p.hoverId === m.id || p.flashId === m.id;
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
                    width: LARGE,
                    height: LARGE,
                    position: "relative",
                    outline: on ? `1px solid ${BRASS}` : `1px solid ${LINE}`,
                  }}
                >
                  <MemeView meme={m} size={LARGE} playing={p.hoverId === m.id && m.gif} />
                  <span
                    style={{
                      position: "absolute",
                      top: 0,
                      left: 0,
                      width: 12,
                      height: 12,
                      background: BRASS,
                      color: BG,
                      fontSize: 9,
                      lineHeight: "12px",
                      fontFamily: "'IBM Plex Mono', monospace",
                    }}
                  >
                    {i + 1}
                  </span>
                </button>
              );
            })}
          </div>
        </div>

        <div
          style={{
            margin: "0 10px",
            borderTop: `1px dotted ${LINE}`,
            height: 1,
          }}
        />

        <div
          className="thin-scroll"
          style={{ flex: 1, overflow: "auto", padding: "8px 10px", color: LINE }}
        >
          <div
            style={{
              display: "grid",
              gridTemplateColumns: `repeat(13, ${SMALL}px)`,
              gap: 4,
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
                    width: SMALL,
                    height: SMALL,
                    position: "relative",
                    outline: on ? `1px solid ${BRASS}` : "1px solid transparent",
                  }}
                >
                  <MemeView meme={m} size={SMALL} playing={p.hoverId === m.id && m.gif} />
                </button>
              );
            })}
          </div>
        </div>

        <footer
          style={{
            height: 22,
            borderTop: `1px solid ${LINE}`,
            display: "flex",
            alignItems: "center",
            padding: "0 10px",
            fontSize: 10,
            letterSpacing: "0.08em",
            color: BRASS,
            fontFamily: "'IBM Plex Mono', monospace",
            flexShrink: 0,
          }}
        >
          <span
            style={{
              width: 6,
              height: 6,
              borderRadius: 6,
              background: hovered?.gif ? BRASS : "#3A3228",
              marginRight: 8,
            }}
          />
          <span className="tabular">
            {hovered ? hovered.name : `${items.length} VISIBLE`}
          </span>
          <span style={{ marginLeft: "auto", opacity: 0.7 }}>CLICK PASTE  ESC</span>
        </footer>
      </div>

      {p.previewId && getMeme(p.previewId) && (
        <div
          style={{
            position: "absolute",
            right: 10,
            top: 64,
            width: 128,
            border: `1px solid ${BRASS}`,
            background: WELL,
            zIndex: 4,
            pointerEvents: "none",
          }}
        >
          <MemeView meme={getMeme(p.previewId)!} size={126} playing />
        </div>
      )}
    </div>
  );
}
