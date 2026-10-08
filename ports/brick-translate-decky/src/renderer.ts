export interface Item {
  box: [number, number, number, number]; zh: string; orig: string;
  kind: string; color: string; align: "left" | "center";
  source_height?: number;
}
export const palette: Record<string, [string, string]> = {
  white: ["#fff9ea", "#111b32"], black: ["#171b21", "#fff5d5"],
  cyan: ["#8ef7ff", "#062735"], yellow: ["#ffe785", "#392608"],
  red: ["#ffb1bd", "#3b0a20"], blue: ["#b8d8ff", "#10234a"],
  green: ["#b4ffc1", "#10351e"], gray: ["#e5e9ee", "#202731"],
};

function linesFor(ctx: CanvasRenderingContext2D, text: string, width: number): string[] {
  const chunks = text.match(/[A-Za-z0-9]+(?:['’.-][A-Za-z0-9]+)*|\s+|[^\x00-\x7F]|./gu) ?? [];
  const tokens = chunks.flatMap(token => ctx.measureText(token).width > width ? Array.from(token) : [token]);
  const lines: string[] = [];
  let line = "";
  for (const token of tokens) {
    if (line && ctx.measureText(line + token).width > width && !/^[。，、！？；：）」』,.!?;:)]/.test(token)) {
      lines.push(line.trim()); line = token.trimStart();
    } else line += token;
  }
  if (line.trim()) lines.push(line.trim());
  return lines;
}

interface Placement {x: number; y: number; w: number; h: number; size: number; lines: string[]; item: Item}
export function drawTranslations(canvas: HTMLCanvasElement, items: Item[], scale: number): Placement[] {
  const ctx = canvas.getContext("2d");
  if (!ctx) return [];
  const W = canvas.width, H = canvas.height, unit = H / 720;
  const placed: Placement[] = [];
  ctx.clearRect(0, 0, W, H);
  for (const item of items) {
    const grounded = item.source_height !== undefined;
    const padding = (grounded ? 3 : 10) * unit;
    const minSize = Math.round((grounded ? 14 : 32) * unit * scale);
    let x = item.box[0] * W, y = item.box[1] * H;
    let w = Math.max(item.box[2] * W - x, minSize * 2);
    const boxHeight = (item.box[3] - item.box[1]) * H;
    const maximum = grounded ? Math.round(Math.max(minSize, Math.min(48 * unit * scale, item.source_height! * H * 0.85 * scale))) :
      Math.round((item.kind === "title" ? 72 : 40) * unit * scale);
    ctx.font = `600 ${maximum}px "Noto Sans CJK SC", "Noto Sans SC", "Microsoft YaHei", sans-serif`;
    if (item.kind !== "dialogue" && item.kind !== "caption" && item.zh.length <= 28) {
      w = Math.max(w, Math.min(W * 0.4, ctx.measureText(item.zh).width + padding * 2));
    }
    const tokens = item.zh.match(/[A-Za-z0-9]+(?:['’.-][A-Za-z0-9]+)*|[^\x00-\x7F]|./gu) ?? [];
    w = Math.max(w, ...tokens.map(token => Math.min(W - padding * 2, ctx.measureText(token).width + padding * 2)));
    w = Math.min(w, W - 2 * padding);
    x = Math.max(padding, Math.min(x, W - w - padding));
    let size = maximum, lines: string[] = [];
    while (true) {
      ctx.font = `600 ${size}px "Noto Sans CJK SC", "Noto Sans SC", "Microsoft YaHei", sans-serif`;
      lines = linesFor(ctx, item.zh, Math.max(w - padding * 2, size));
      if (lines.length * size * 1.3 <= boxHeight || size <= minSize) break;
      size = Math.max(minSize, size - Math.max(1, Math.round(unit * 2)));
    }
    // Expand dense paragraphs horizontally instead of making unreadable tiny text.
    if (lines.length * size * 1.3 > H * 0.45) {
      w = Math.min(W - padding * 2, Math.max(w, W * 0.7));
      x = Math.max(padding, Math.min(x, W - w - padding));
      lines = linesFor(ctx, item.zh, w - padding * 2);
    }
    w = Math.min(W - padding * 2, Math.max(w, ...lines.map(line => ctx.measureText(line).width + padding * 2)));
    x = Math.max(padding, Math.min(x, W - w - padding));
    const h = lines.length * size * 1.3 + padding * 2;
    if (h > H - padding * 2) continue;
    if (item.kind !== "dialogue") y += Math.max(0, (boxHeight - h) / 2);
    y = Math.max(padding, Math.min(y, H - h - padding));
    const xs = [x, padding, W - w - padding], ys = [y, padding, H - h - padding];
    for (const prior of placed) {
      xs.push(prior.x + prior.w + padding, prior.x - w - padding);
      ys.push(prior.y + prior.h + padding, prior.y - h - padding);
    }
    const overlap = (cx: number, cy: number) => placed.some(p =>
      cx < p.x + p.w + padding && cx + w + padding > p.x && cy < p.y + p.h + padding && cy + h + padding > p.y);
    const candidates = xs.flatMap(cx => ys.map(cy => ({x: cx, y: cy})))
      .filter(p => p.x >= padding && p.x + w <= W - padding && p.y >= padding && p.y + h <= H - padding && !overlap(p.x, p.y));
    candidates.sort((a, b) => Math.hypot(a.x - x, a.y - y) - Math.hypot(b.x - x, b.y - y));
    if (!candidates.length) continue; // Full text remains available in Decky's translation list.
    x = candidates[0].x; y = candidates[0].y;
    const [fill, stroke] = palette[item.color] ?? palette.white;
    ctx.font = `600 ${size}px "Noto Sans CJK SC", "Noto Sans SC", "Microsoft YaHei", sans-serif`;
    ctx.textBaseline = "top"; ctx.textAlign = item.align;
    ctx.lineJoin = "round"; ctx.lineWidth = Math.max(2, size * 0.4);
    ctx.strokeStyle = stroke; ctx.fillStyle = fill;
    const tx = item.align === "center" ? x + w / 2 : x + padding;
    lines.forEach((line, i) => {
      ctx.strokeText(line, tx, y + padding + i * size * 1.3);
      ctx.fillText(line, tx, y + padding + i * size * 1.3);
    });
    placed.push({x, y, w, h, size, lines, item});
  }
  return placed;
}
