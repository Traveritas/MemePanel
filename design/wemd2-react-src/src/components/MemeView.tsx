import type { CSSProperties } from "react";
import type { Meme } from "../data/memes";

export function MemeView({
  meme,
  size,
  playing,
}: {
  meme: Meme;
  size: number;
  playing?: boolean;
}) {
  if (meme.kind === "image") {
    return (
      <img
        src={meme.src}
        alt=""
        draggable={false}
        width={size}
        height={size}
        style={{
          width: size,
          height: size,
          objectFit: meme.contain ? "contain" : "cover",
          objectPosition: "center",
          display: "block",
          background: meme.contain ? "#f4f0e8" : "#2a2a2a",
          filter: playing ? "contrast(1.06) saturate(1.08)" : undefined,
        }}
      />
    );
  }

  const len = (meme.text ?? "").replace(/\n/g, "").length;
  const fs =
    len <= 1
      ? size * 0.52
      : len <= 2
        ? size * 0.36
        : len <= 4
          ? size * 0.22
          : len <= 6
            ? size * 0.168
            : size * 0.132;

  return (
    <div
      style={{
        width: size,
        height: size,
        background: meme.bg,
        color: meme.fg,
        display: "flex",
        alignItems: "center",
        justifyContent: "center",
        fontSize: Math.max(9, fs),
        fontWeight: 600,
        lineHeight: 1.12,
        textAlign: "center",
        padding: Math.max(3, size * 0.06),
        whiteSpace: "pre-line",
        fontFamily: "'Noto Sans SC', sans-serif",
        letterSpacing: len <= 2 ? "0.02em" : "0",
      }}
    >
      {meme.text}
    </div>
  );
}

export function GifMark({
  color,
  bg,
  label = "动",
}: {
  color: string;
  bg: string;
  label?: string;
}) {
  return (
    <span
      className="gif-tick"
      style={{
        position: "absolute",
        right: 2,
        bottom: 2,
        fontSize: 9,
        lineHeight: "12px",
        padding: "0 3px",
        background: bg,
        color,
        fontFamily: "'Noto Sans SC', sans-serif",
        pointerEvents: "none",
        fontWeight: 500,
      }}
    >
      {label}
    </span>
  );
}

export function CornerBrackets({
  color = "#fff",
  arm = 8,
  thick = 1,
}: {
  color?: string;
  arm?: number;
  thick?: number;
}) {
  const bar = (style: CSSProperties) => (
    <i
      style={{
        position: "absolute",
        background: color,
        ...style,
      }}
    />
  );
  return (
    <>
      {bar({ top: 0, left: 0, width: arm, height: thick })}
      {bar({ top: 0, left: 0, width: thick, height: arm })}
      {bar({ top: 0, right: 0, width: arm, height: thick })}
      {bar({ top: 0, right: 0, width: thick, height: arm })}
      {bar({ bottom: 0, left: 0, width: arm, height: thick })}
      {bar({ bottom: 0, left: 0, width: thick, height: arm })}
      {bar({ bottom: 0, right: 0, width: arm, height: thick })}
      {bar({ bottom: 0, right: 0, width: thick, height: arm })}
    </>
  );
}
