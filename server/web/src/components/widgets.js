import { h } from './dom.js';
import { store } from '../state/store.js';

export function StatusDot(kind, text) {
  return h('span', {}, h('i', { class: `dot ${kind}` }), text);
}

export function BinBadge(label) {
  const b = store.bin(label);
  return h('span', { style: { color: b.color || 'inherit', fontWeight: 600 } }, b.name_vi || label);
}

export const STEP_TEXT = {
  rotate: 'quay mâm', lid_open: 'mở nắp', dump: 'hất rác', lid_close: 'đóng nắp', measure: 'đo ngăn', return: 'về ngăn 1',
  jog: 'nhích mâm', servo: 'chỉnh servo',
};

/** Info bubble: hover or tap (focus) shows the text. */
export function Tip(text) {
  return h('span', { class: 'tip', tabindex: '0', role: 'note', 'aria-label': text, 'data-tip': text }, 'i');
}

function dispatchLine(r, sort) {
  const d = r.dispatch;
  if (!d) return null;
  const moving = d.state === 'pending' || d.state === 'sent';
  if (moving) {
    const step = sort?.step && sort.step !== 'idle' ? STEP_TEXT[sort.step] || sort.step : null;
    return h('div', { class: 'disp run' }, h('i', { class: 'spin' }), step ? `Đang ${step}…` : `Đang gửi tới ngăn ${r.bin}…`);
  }
  const text = {
    done: `Đã hất vào ngăn ${r.bin} · ${(d.ms / 1000).toFixed(1)} s`, timeout: 'Actuator không phản hồi',
    busy: 'Actuator đang bận', bin_full: `Ngăn ${r.bin} đầy, chưa hất`, error: 'Lỗi actuator',
  }[d.state] || d.state;
  return h('div', { class: `disp ${d.state === 'done' ? 'ok' : 'bad'}` }, d.state === 'done' ? '✓ ' : '! ', text);
}

/** Result: winner + confidence, how far ahead of the runner-up, and the model's score for every bin. */
export function ResultCard(r, sort) {
  if (!r) return h('div', { class: 'res-card muted' }, 'Chưa có kết quả.');
  if (r.has_object === false) return h('div', { class: 'res-card muted' }, 'Khay trống');
  const b = store.bin(r.label);
  const scores = r.scores || r.probs || {};
  const rows = store.get().labels.map((x) => ({ ...x, v: Number(scores[x.id] || 0) })).sort((x, y) => y.v - x.v);
  const [first, second] = rows;
  const margin = r.margin ?? (first && second ? first.v - second.v : 0);
  const [kind, verdict] = r.uncertain ? ['bad', 'Không chắc → bỏ vào Rác khác']
    : margin >= 0.3 ? ['ok', 'Chắc chắn'] : margin >= 0.1 ? ['warn', 'Khá chắc'] : ['bad', 'Phân vân'];
  const gap = second && second.v > 0 ? ` · hơn ${second.name_vi} ${Math.round(margin * 100)} điểm` : '';
  const pct = (v) => (v > 0 && v < 0.01 ? '<1%' : `${Math.round(v * 100)}%`);
  return h('div', { class: 'res-card' },
    h('div', { class: 'result' },
      h('span', { style: { color: b.color } }, `${b.name_vi || r.label}`, h('small', {}, `Ngăn ${r.bin}`)),
      h('span', { class: 'pct', style: { color: b.color } }, `${Math.round((r.confidence || 0) * 100)}%`)),
    h('div', { class: `verdict ${kind}` }, verdict + gap, r.blurry ? h('span', { class: 'muted' }, ' · ảnh hơi mờ') : null),
    rows.length ? h('div', { class: 'scores', title: 'Điểm model cho từng ngăn (YOLO chấm từng loại riêng, không cộng thành 100%)' },
      rows.map((x) => h('div', { class: `score-row ${x.id === first.id ? 'win' : ''}` },
        h('span', { class: 'score-name' }, h('b', {}, x.bin), x.name_vi),
        h('span', { class: 'score-bar' }, h('i', { style: { width: `${Math.max(0, Math.min(1, x.v)) * 100}%`, background: x.color } })),
        h('span', { class: 'score-val mono' }, pct(x.v))))) : null,
    dispatchLine(r, sort));
}

/** Segmented control: [{value,label}], selected value gets filled. */
export function Segmented(options, value, onPick) {
  return h('div', { class: 'seg' }, options.map((o) =>
    h('button', { class: o.value === value ? 'on' : '', onClick: () => onPick(o.value) }, o.label)));
}

/** Switch row: label on the left, toggle on the right. */
export function Switch(label, checked, onToggle) {
  const sw = h('button', { class: `switch ${checked ? 'on' : ''}`, role: 'switch', 'aria-checked': String(!!checked),
    onClick: onToggle }, h('i', {}));
  return h('div', { class: 'switch-row' }, h('span', {}, label), sw);
}

/** Number input with - / + buttons. */
export function Stepper({ value, step = 1, min = -1e9, max = 1e9, onChange }) {
  const input = h('input', { type: 'number', value, step });
  const set = (v) => {
    const n = Math.min(max, Math.max(min, Number(v)));
    input.value = n;
    onChange?.(n);
  };
  input.addEventListener('change', () => set(input.value));
  return h('span', { class: 'stepper' },
    h('button', { onClick: () => set(Number(input.value) - step) }, '−'),
    input,
    h('button', { onClick: () => set(Number(input.value) + step) }, '+'));
}

export function DeviceRow(d, name) {
  return h('div', { class: 'dev' },
    h('i', { class: `dot ${d?.online ? 'ok' : 'bad'}` }),
    h('div', { class: 'grow' }, h('div', { class: 'dev-name' }, name),
      h('div', { class: 'muted mono small0' }, d?.url ? d.url.replace(/^https?:\/\//, '') : 'chưa tìm thấy')));
}

export function HistoryItem(rec, onDelete) {
  const b = store.bin(rec.label);
  const when = new Date(rec.ts * 1000).toLocaleString('vi-VN');
  return h('figure', {},
    rec.thumb ? h('img', { src: rec.thumb, loading: 'lazy', alt: rec.label }) : h('img', { alt: '' }),
    h('figcaption', {},
      h('div', { style: { color: b.color, fontWeight: 600 } }, `${b.name_vi || rec.label} · ${Math.round(rec.confidence * 100)}%`),
      h('div', { class: 'muted', style: { fontSize: 'var(--fs-xs)' } }, when, rec.uncertain ? ' · không chắc' : ''),
      h('button', { onClick: () => onDelete(rec.id), style: { marginTop: '4px' } }, 'Xóa')));
}

const NS = 'http://www.w3.org/2000/svg';
function svg(tag, attrs = {}, ...kids) {
  const el = document.createElementNS(NS, tag);
  for (const [k, v] of Object.entries(attrs)) el.setAttribute(k, v);
  for (const kid of kids) if (kid != null) el.append(kid.nodeType ? kid : document.createTextNode(String(kid)));
  return el;
}

/** Number tile: small label, big value. */
export function StatTile(label, value, hint) {
  return h('div', { class: 'tile' }, h('div', { class: 'muted small' }, label),
    h('div', { class: 'tile-value' }, value), hint ? h('div', { class: 'muted small' }, hint) : null);
}

/** Horizontal bars with the value and share written next to every bar (identity never relies on colour alone). */
export function BarChart(items) {
  const max = Math.max(1, ...items.map((i) => i.value));
  const total = items.reduce((a, i) => a + i.value, 0) || 1;
  return h('div', { class: 'bars' }, items.map((i) => h('div', { class: 'bar-row', title: `${i.label}: ${i.value}` },
    h('span', { class: 'bar-label' }, i.label),
    h('div', { class: 'bar' }, h('i', { style: { width: `${(i.value / max) * 100}%`, background: i.color || 'var(--text)' } })),
    h('span', { class: 'mono bar-val' }, `${i.value} · ${Math.round((i.value / total) * 100)}%`))));
}

/** Donut: share of each item; centre shows the total. Legend with values sits beside it. */
export function Donut(items, centreLabel = 'tổng') {
  const total = items.reduce((a, i) => a + i.value, 0);
  const R = 52, C = 2 * Math.PI * R, GAP = 2;
  const root = svg('svg', { viewBox: '0 0 140 140', width: 140, height: 140, role: 'img', 'aria-label': 'Tỉ lệ theo loại rác' });
  root.append(svg('circle', { cx: 70, cy: 70, r: R, fill: 'none', stroke: 'var(--line)', 'stroke-width': 16 }));
  let acc = 0;
  if (total > 0) for (const i of items) {
    const len = (i.value / total) * C;
    if (len > 0) {
      const arc = svg('circle', { cx: 70, cy: 70, r: R, fill: 'none', stroke: i.color, 'stroke-width': 16,
        'stroke-dasharray': `${Math.max(0, len - GAP)} ${C}`, 'stroke-dashoffset': -acc, transform: 'rotate(-90 70 70)' },
      svg('title', {}, `${i.label}: ${i.value}`));
      root.append(arc);
    }
    acc += len;
  }
  root.append(svg('text', { x: 70, y: 70, 'text-anchor': 'middle', 'font-size': 26, 'font-weight': 600, fill: 'currentColor' }, total),
    svg('text', { x: 70, y: 90, 'text-anchor': 'middle', 'font-size': 11, fill: 'var(--muted)' }, centreLabel));
  const legend = h('div', { class: 'legend' }, items.map((i) => h('div', { class: 'legend-row' },
    h('i', { class: 'swatch', style: { background: i.color } }), h('span', { class: 'grow' }, i.label),
    h('b', { class: 'mono' }, i.value))));
  return h('div', { class: 'donut' }, root, legend);
}

/** Column chart over time: thin rounded columns, faint grid, only the maximum labelled; hover/tap gives the exact value. */
export function Columns(items, { height = 140 } = {}) {
  const W = 560, padL = 28, padB = 20, padT = 14, n = items.length;
  const max = Math.max(1, ...items.map((i) => i.value));
  const top = Math.ceil(max / 4) * 4 || 4;
  const bw = Math.min(28, ((W - padL) / Math.max(n, 1)) * 0.6), step = (W - padL) / Math.max(n, 1);
  const el = svg('svg', { viewBox: `0 0 ${W} ${height + padB + padT}`, width: '100%', role: 'img', 'aria-label': 'Số lần phân loại theo ngày' });
  for (let g = 0; g <= 4; g++) {
    const y = padT + height - (g / 4) * height;
    el.append(svg('line', { x1: padL, x2: W, y1: y, y2: y, stroke: 'var(--line)', 'stroke-width': 1 }),
      svg('text', { x: padL - 6, y: y + 4, 'text-anchor': 'end', 'font-size': 10, fill: 'var(--muted)' }, Math.round((g / 4) * top)));
  }
  const peak = items.findIndex((i) => i.value === max && max > 0);
  items.forEach((it, k) => {
    const x = padL + k * step + (step - bw) / 2, bh = (it.value / top) * height, y = padT + height - bh;
    const g = svg('g', {}, svg('title', {}, `${it.label}: ${it.value}`),
      svg('rect', { x, y: it.value ? y : padT + height - 1, width: bw, height: it.value ? bh : 1, rx: 4, fill: 'var(--text)', opacity: 0.85 }),
      svg('rect', { x: padL + k * step, y: padT, width: step, height, fill: 'transparent' }));
    el.append(g);
    if (k === peak) el.append(svg('text', { x: x + bw / 2, y: y - 4, 'text-anchor': 'middle', 'font-size': 11, 'font-weight': 600, fill: 'currentColor' }, it.value));
    if (n <= 7 || k % 2 === 0) el.append(svg('text', { x: x + bw / 2, y: height + padT + 14, 'text-anchor': 'middle', 'font-size': 10, fill: 'var(--muted)' }, it.label));
  });
  return el;
}

/** Round bin button: number inside, bin name below. */
export function BinDot(bin, { active, onClick }) {
  return h('button', { class: `bin-dot ${active ? 'on' : ''}`, style: { '--c': bin.color }, onClick },
    h('span', { class: 'bin-circle' }, bin.bin), h('span', { class: 'bin-name' }, bin.name_vi));
}
