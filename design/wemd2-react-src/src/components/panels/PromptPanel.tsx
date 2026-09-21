import { CATEGORIES, filterMemes, getMeme } from "../../data/memes";
import type { PanelProps } from "../../panel-types";
import { MemeView } from "../MemeView";

const BG = "#0B0B0B";
const AMBER = "#D7C49A";
const DIM = "rgba(215,196,154,0.38)";
const LINE = "rgba(215,196,154,0.22)";
const CELL = 48;
const GAP = 2;

export function PromptPanel(p: PanelProps) {
  const items = filterMemes(p.query, p.category);
  const recents = p.recents.map(getMeme).filter(Boolean);
  const cat = CATEGORIES.find((c) => c.id === p.category);

  return (
    <div
      style={{
        width: 640,
        height: 412,
        background: BG,
        color: AMBER,
        border: `1px solid ${AMBER}`,
        fontFamily: "'IBM Plex Mono', monospace",
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
          height: 26,
          display: "flex",
          alignItems: "center",
          padding: "0 10px",
          borderBottom: `1px solid ${LINE}`,
          fontSize: 11,
          letterSpacing: "0.08em",
        }}
      >
        <span>即帖:\PROMPT</span>
        <span className="tabular" style={{ marginLeft: "auto", opacity: 0.5 }}>
          {cat?.count.toLocaleString()}
        </span>
        <button type="button" onClick={p.onClose} style={{ marginLeft: 14, opacity: 0.7 }}>
          [ESC]
        </button>
      </header>

      <div
        style={{
          height: 32,
          display: "flex",
          alignItems: "center",
          padding: "0 10px",
          gap: 8,
          borderBottom: `1px solid ${LINE}`,
        }}
      >
        <span style={{ color: AMBER }}>{">"}</span>
        <input
          autoFocus={p.autoFocus !== false}
          value={p.query}
          onChange={(e) => p.onQuery(e.target.value)}
          placeholder="search tags…"
          style={{
            flex: 1,
            background: "transparent",
            border: "none",
            outline: "none",
            fontSize: 13,
            caretColor: AMBER,
            padding: 0,
          }}
        />
      </div>

      <div
        style={{
          display: "flex",
          gap: 2,
          padding: "6px 8px",
          borderBottom: `1px solid ${LINE}`,
          flexWrap: "wrap",
        }}
      >
        {CATEGORIES.map((c) => {
          const on = c.id === p.category;
          return (
            <button
              type="button"
              key={c.id}
              onClick={() => p.onCategory(c.id)}
              style={{
                fontSize: 11,
                padding: "2px 6px",
                background: on ? AMBER : "transparent",
                color: on ? BG : AMBER,
                letterSpacing: "0.02em",
              }}
            >
              {c.fkey}:{c.name}
            </button>
          );
        })}
      </div>

      <div style={{ padding: "8px 10px 6px", borderBottom: `1px solid ${LINE}` }}>
        <div style={{ fontSize: 10, opacity: 0.45, marginBottom: 6, letterSpacing: "0.12em" }}>
          RECENTS  1–9
        </div>
        <div style={{ display: "flex", gap: GAP }}>
          {recents.slice(0, 9).map((m, i) => {
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
                  width: CELL,
                  height: CELL,
                  position: "relative",
                  outline: on ? `1px solid ${AMBER}` : `1px solid ${LINE}`,
                }}
              >
                <MemeView meme={m} size={CELL} playing={p.hoverId === m.id && m.gif} />
                <span
                  style={{
                    position: "absolute",
                    top: 0,
                    left: 0,
                    minWidth: 11,
                    height: 13,
                    padding: "0 2px",
                    background: AMBER,
                    color: BG,
                    fontSize: 10,
                    lineHeight: "13px",
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
        className="thin-scroll"
        style={{ flex: 1, overflow: "auto", padding: 8, color: DIM }}
      >
        <div
          style={{
            display: "grid",
            gridTemplateColumns: `repeat(12, ${CELL}px)`,
            gap: GAP,
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
                  width: CELL,
                  height: CELL,
                  position: "relative",
                  outline: on ? `1px solid ${AMBER}` : "1px solid transparent",
                }}
              >
                <MemeView meme={m} size={CELL} playing={p.hoverId === m.id && m.gif} />
                {m.gif && (
                  <span
                    style={{
                      position: "absolute",
                      right: 1,
                      bottom: 1,
                      fontSize: 8,
                      background: AMBER,
                      color: BG,
                      lineHeight: "10px",
                      padding: "0 2px",
                    }}
                  >
                    GIF
                  </span>
                )}
              </button>
            );
          })}
        </div>
      </div>

      <footer
        style={{
          height: 22,
          borderTop: `1px solid ${AMBER}`,
          display: "flex",
          alignItems: "center",
          padding: "0 10px",
          fontSize: 10,
          letterSpacing: "0.06em",
          flexShrink: 0,
        }}
      >
        <span className="tabular">
          {items.length}/{cat?.count}  {cat?.name.toUpperCase()}  {p.hoverId && getMeme(p.hoverId)?.gif ? "GIF" : "IMG"}
        </span>
        <span style={{ marginLeft: "auto", opacity: 0.55 }}>ENTER SEND  ·  ESC</span>
      </footer>

      {p.previewId && getMeme(p.previewId) && (
        <div
          style={{
            position: "absolute",
            right: 12,
            bottom: 30,
            width: 140,
            border: `1px solid ${AMBER}`,
            background: BG,
            zIndex: 4,
            pointerEvents: "none",
          }}
        >
          <MemeView meme={getMeme(p.previewId)!} size={138} playing />
          <div
            style={{
              borderTop: `1px solid ${LINE}`,
              padding: "4px 6px",
              fontSize: 10,
            }}
          >
            {getMeme(p.previewId)!.name}
          </div>
        </div>
      )}
    </div>
  );
}
