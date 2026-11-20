import { api } from '../api/client.js';
import { h } from '../components/dom.js';
import { Columns, Donut, StatTile } from '../components/widgets.js';
import { store } from '../state/store.js';

export function mount(root) {
  const body = h('div', {});
  let alive = true;

  api.get('/history/stats').then((s) => {
    if (!alive) return;
    const bins = store.get().labels;
    const items = bins.map((b) => ({ label: b.name_vi, value: s.by_label[b.id] || 0, color: b.color }));
    body.replaceChildren(
      h('div', { class: 'tile-row' },
        StatTile('Đã phân loại', s.total),
        StatTile('Độ tin cậy trung bình', `${Math.round((s.avg_confidence || 0) * 100)}%`),
        StatTile('Không chắc', s.uncertain, 'đưa vào ngăn khác')),
      h('h2', {}, 'Theo loại rác'),
      Donut(items),
      h('h2', {}, '14 ngày gần nhất'),
      s.by_day.length ? Columns(s.by_day.map((d) => ({ label: d.day.slice(5), value: d.n })))
        : h('div', { class: 'muted' }, 'Chưa có dữ liệu.'));
  });

  root.replaceChildren(body);
  return () => { alive = false; };
}
