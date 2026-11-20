import { api } from '../api/client.js';
import { h, toast } from '../components/dom.js';
import { HistoryItem } from '../components/widgets.js';
import { store } from '../state/store.js';

export function mount(root) {
  const list = h('div', { class: 'thumbs' });
  const filter = h('select', {}, h('option', { value: '' }, 'Tất cả'),
    store.get().labels.map((b) => h('option', { value: b.id }, b.name_vi)));
  const clear = h('button', {}, 'Xóa tất cả');
  let alive = true;

  async function load() {
    const data = await api.get(`/history?limit=200&label=${encodeURIComponent(filter.value)}`);
    if (!alive) return;
    list.replaceChildren(...(data.items.length
      ? data.items.map((r) => HistoryItem(r, async (id) => { await api.del(`/history/${id}`); load(); }))
      : [h('div', { class: 'muted' }, 'Chưa có bản ghi.')]));
  }

  filter.addEventListener('change', load);
  clear.addEventListener('click', async () => {
    if (!confirm('Xóa toàn bộ lịch sử?')) return;
    try { await api.del('/history'); load(); } catch (e) { toast(e.message); }
  });

  root.replaceChildren(h('h2', {}, 'Lịch sử'), h('div', { class: 'row' }, filter, clear), list);
  load();
  const off = store.on((s) => { if (s.result?.id) load(); });
  return () => { alive = false; off(); };
}
