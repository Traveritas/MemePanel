import { CATEGORIES, filterMemes, getMeme } from "../../data/memes";
import type { PanelProps } from "../../panel-types";
import { CornerBrackets, MemeView } from "../MemeView";

const BG = "#737373";
const INK = "#F4F4F4";
const DIM = "rgba(244,244,244,0.55)";
const CELL = 68;
const GAP = 3;
const FILM = 44;

export function LightboxPanel(p: PanelProps) {
  const items = filterMemes(p.query, p.category);
  const recents = p.recents.map(getMeme).filter(Boolean);
  const hovered = p.hoverId ? getMeme(p.hoverId) : undefined;
  const preview = p.previewId ? getMeme(p.previewId) : undefined;

  return (
    <div
      style={{
        width: 844,
        height: 392,
        background: BG,
        color: INK,
        boxShadow: "0 0 0 1px rgba(0,0,0,0.35)",
        fontFamily: "'Noto Sans SC', sans-serif",
        display: "flex",
        flexDirection: "column",
        userSelect: "none",
        position: "relative",
      }}
      onMouseDown={(e) => {
        if ((e.target as HTMLElement).tagName !== "INPUT") e.preventDefault();
      }}
    >
      <div
        style={{
          height: 36,
          display: "flex",
          alignItems: "center",
          padding: "0 12px",
          gap: 16,
        }}
      >
        <div style={{ display: "flex", gap: 14, fontSize: 11, letterSpacing: "0.16em" }}>
          {CATEGORIES.slice(0, 7).map((c) => {
            const on = c.id === p.category;
            return (
              <button
                type="button"
                key={c.id}
                onClick={() => p.onCategory(c.id)}
                style={{
                  opacity: on ? 1 : 0.5,
                  position: "relative",
                  paddingBottom: 4,
                }}
              >
                {c.name}
                {on && (
                  <i
                    style={{
                      position: "absolute",
                      left: "50%",
                      bottom: 0,
                      width: 12,
                      height: 1,
                      background: INK,
                      transform: "translateX(-50%)",
                    }}
                  />
                )}
              </button>
            );
          })}
        </div>

        <div
          style={{
            marginLeft: "auto",
            display: "flex",
            alignItems: "center",
            gap: 8,
            minWidth: 220,
            justifyContent: "flex-end",
          }}
        >
          <input
            autoFocus={p.autoFocus !== false}
            value={p.query}
            onChange={(e) => p.onQuery(e.target.value)}
            placeholder="检索"
            style={{
              width: 160,
              background: "transparent",
              border: "none",
              outline: "none",
              textAlign: "right",
              fontSize: 13,
              letterSpacing: "0.08em",
              padding: 0,
              color: INK,
              caretColor: INK,
            }}
          />
          <button type="button" onClick={p.onClose} style={{ marginLeft: 8, opacity: 0.55, fontSize: 14 }}>
            ×
          </button>
        </div>
      </div>

      <div
        className="thin-scroll"
        style={{ flex: 1, overflow: "auto", padding: "4px 12px 8px", color: "rgba(255,255,255,0.25)" }}
      >
        <div
          style={{
            display: "grid",
            gridTemplateColumns: `repeat(11, ${CELL}px)`,
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
                }}
              >
                <MemeView meme={m} size={CELL} playing={p.hoverId === m.id && m.gif} />
                {on && <CornerBrackets color="#fff" arm={7} thick={1} />}
              </button>
            );
          })}
        </div>
      </div>

      <div
        style={{
          borderTop: "1px solid rgba(255,255,255,0.22)",
          padding: "8px 12px 10px",
          display: "flex",
          alignItems: "center",
          gap: 8,
        }}
      >
        {recents.slice(0, 9).map((m, i) => {
          if (!m) return null;
          const on = p.hoverId === m.id;
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
              style={{ width: FILM, height: FILM, position: "relative", opacity: on ? 1 : 0.92 }}
            >
              <MemeView meme={m} size={FILM} />
              <span
                style={{
                  position: "absolute",
                  left: 2,
                  top: 1,
                  fontSize: 9,
                  fontFamily: "'IBM Plex Mono', monospace",
                  textShadow: "0 0 2px #000",
                }}
              >
                {i + 1}
              </span>
            </button>
          );
        })}
        <div
          style={{
            marginLeft: "auto",
            fontSize: 11,
            letterSpacing: "0.06em",
            opacity: 0.8,
            textAlign: "right",
            minWidth: 220,
            color: DIM,
            fontFamily: "'IBM Plex Mono', monospace",
          }}
        >
          {hovered ? (
            <>
              {hovered.name}
              <span style={{ marginLeft: 10 }}>{hovered.tags.slice(0, 2).join(" / ")}</span>
            </>
          ) : (
            <span>单击即贴 · 底片为最近</span>
          )}
        </div>
      </div>

      {preview && (
        <div
          style={{
            position: "absolute",
            left: 12,
            bottom: 64,
            zIndex: 4,
            pointerEvents: "none",
          }}
        >
          <div style={{ width: 160, height: 160, position: "relative", boxShadow: "0 0 0 1px rgba(0,0,0,0.35)" }}>
            <MemeView meme={preview} size={160} playing={preview.gif} />
            <CornerBrackets color="#fff" arm={10} thick={1} />
          </div>
        </div>
      )}
    </div>
  );
}
