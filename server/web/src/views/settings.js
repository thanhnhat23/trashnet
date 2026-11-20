import { api, apiKey, setApiKey } from '../api/client.js';
import { h, toast } from '../components/dom.js';
import { Stepper, Switch, Tip } from '../components/widgets.js';

/* Every setting: readable name, what it does, and what to type. kind: pct = 0..1 shown as %, num, sel, bool. */
const SECTIONS = [
  {
    title: 'Nhận diện', desc: 'Model nhìn rác thế nào và khi nào thì coi là chắc chắn.',
    items: [
      { key: 'det_tau', kind: 'pct', name: 'Độ chắc chắn tối thiểu', model: true,
        tip: 'Model phải chắc tới mức này thì mới bỏ rác vào đúng ngăn; thấp hơn thì đưa vào ngăn Rác khác. Tăng: ít bỏ nhầm ngăn hơn nhưng nhiều rác vào Rác khác hơn. Nên đặt 40–55%.' },
      { key: 'det_conf', kind: 'pct', name: 'Ngưỡng nhìn thấy vật', model: true,
        tip: 'Từ mức này model mới coi là trên khay có vật. Thấp: nhạy hơn nhưng dễ báo nhầm bóng hoặc vết bẩn. Cao: dễ bỏ sót vật nhỏ hoặc mờ. Nên đặt 25–35%.' },
      { key: 'presence_min_diff', kind: 'num', step: 1, min: 1, max: 60, name: 'Độ nhạy khay trắng', unit: 'mức xám',
        tip: 'So với ảnh khay trống: lệch bao nhiêu mức xám thì coi là có vật. Dùng khi model không thấy vật, ví dụ giấy trắng trên khay trắng. Nhỏ: nhạy hơn nhưng dễ nhầm khi ánh sáng đổi. Nên đặt 8–20.' },
      { key: 'vote_frames', kind: 'num', step: 1, min: 1, max: 7, name: 'Số ảnh mỗi lần', unit: 'ảnh',
        tip: 'Mỗi lần phân loại chụp ngần này ảnh rồi lấy kết quả theo đa số. Nhiều ảnh: chắc hơn nhưng chậm hơn khoảng 0,1–0,3 giây mỗi ảnh. Nên đặt 3.' },
      { key: 'blur_min', kind: 'num', step: 5, min: 0, max: 500, name: 'Độ nét tối thiểu', adv: true,
        tip: 'Ảnh kém nét hơn mức này thì chụp lại (tối đa 3 lần). Nếu máy chụp lại liên tục thì giảm xuống. Nên đặt 30–80.' },
      { key: 'min_area_frac', kind: 'pct', step: 0.005, max: 0.2, name: 'Kích thước vật tối thiểu', adv: true,
        tip: 'Tính theo phần trăm khung hình. Vật nhỏ hơn mức này bị bỏ qua (coi là bụi hoặc nhiễu). Nên đặt 1–3%.' },
      { key: 'tau', kind: 'pct', name: 'Ngưỡng tin cậy (model cũ)', model: true, adv: true, legacy: true,
        tip: 'Chỉ dùng khi không có model YOLO (chế độ phân loại cũ).' },
      { key: 'delta', kind: 'pct', name: 'Chênh lệch top-2 (model cũ)', model: true, adv: true, legacy: true,
        tip: 'Chế độ cũ: nếu hai loại cao nhất chênh nhau ít hơn mức này thì coi là không chắc.' },
    ],
  },
  {
    title: 'Tự động', desc: 'Khi bật Tự động phân loại ở trang Live.',
    items: [
      { key: 'auto_interval_sec', kind: 'num', step: 0.5, min: 0.5, max: 30, name: 'Kiểm tra khay mỗi', unit: 'giây',
        tip: 'Máy nhìn khay bao lâu một lần để xem có rác mới chưa. Cần thấy vật 2 lần liên tiếp mới phân loại. Ngắn: phản ứng nhanh hơn nhưng tốn CPU hơn. Nên đặt 1–3.' },
      { key: 'cooldown_sec', kind: 'num', step: 0.5, min: 0, max: 30, name: 'Nghỉ sau mỗi lần', unit: 'giây',
        tip: 'Chờ sau mỗi lần hất để rác rơi hẳn và khay đứng yên rồi mới nhìn tiếp. Nên đặt 1–3.' },
      { key: 'job_timeout_sec', kind: 'num', step: 5, min: 10, max: 120, name: 'Chờ actuator tối đa', unit: 'giây', adv: true,
        tip: 'Quá thời gian này mà mâm chưa xong thì báo "Actuator không phản hồi". Nên đặt 30.' },
    ],
  },
  {
    title: 'Camera', desc: '',
    items: [
      { key: 'capture.res', kind: 'sel', name: 'Độ phân giải chụp',
        options: [['vga', 'VGA 640×480'], ['svga', 'SVGA 800×600'], ['xga', 'XGA 1024×768']],
        tip: 'SVGA là mức cân bằng. XGA nét hơn nhưng chụp chậm hơn và stream giật hơn. Khi giống độ phân giải đang stream, máy lấy ảnh thẳng từ stream nên không bị đứng hình.' },
      { key: 'capture_from_stream', kind: 'bool', name: 'Chụp từ luồng stream',
        tip: 'Bật: lấy ảnh từ stream đang chạy nên nhanh hơn và camera không bị đứng hình khi phân loại. Tắt: lần nào cũng chụp riêng bằng /capture.' },
      { key: 'capture.flash', kind: 'bool', name: 'Bật flash khi chụp', adv: true,
        tip: 'Nháy đèn mỗi lần chụp. Khi bật, máy phải chụp riêng (không lấy từ stream).' },
      { key: 'capture.q', kind: 'num', step: 1, min: 5, max: 30, name: 'Nén JPEG', adv: true,
        tip: 'Số nhỏ thì ảnh đẹp hơn nhưng file lớn hơn. Nên đặt 10–12.' },
    ],
  },
];

const getK = (o, k) => k.split('.').reduce((x, p) => x?.[p], o);
const setK = (o, k, v) => { const ps = k.split('.'); const last = ps.pop(); ps.reduce((x, p) => x[p], o)[last] = v; };

export function mount(root) {
  let cfg = null, saved = '', meta = {};
  const saveBtn = h('button', { class: 'primary', disabled: true }, 'Lưu thay đổi');
  const bar = h('div', { class: 'savebar' }, h('span', { class: 'small0' }, 'Có thay đổi chưa lưu'), saveBtn);
  const body = h('div', { class: 'settings' });

  const values = () => { const { api_key, device_keys, _meta, ...v } = cfg; return v; };
  const dirty = () => { const d = JSON.stringify(values()) !== saved; saveBtn.disabled = !d; bar.classList.toggle('show', d); };

  saveBtn.addEventListener('click', async () => {
    try {
      const r = await api.patch('/settings', { values: values() });
      cfg = { ...r, _meta: meta };
      saved = JSON.stringify(values()); dirty(); toast('Đã lưu');
    } catch (e) { toast(e.message); }
  });

  function control(it) {
    const v = getK(cfg, it.key);
    const set = (x) => { setK(cfg, it.key, x); dirty(); };
    if (it.kind === 'bool') {
      return h('button', { class: `switch ${v ? 'on' : ''}`, role: 'switch', 'aria-checked': String(!!v),
        onClick: () => { set(!getK(cfg, it.key)); render(); } }, h('i', {}));
    }
    if (it.kind === 'sel') {
      const sel = h('select', {}, it.options.map(([val, t]) => h('option', { value: val, selected: val === v }, t)));
      sel.addEventListener('change', () => set(sel.value));
      return sel;
    }
    if (it.kind === 'pct') {
      const eff = v ?? meta.model?.[it.key] ?? 0;
      const max = it.max ?? 1, step = it.step ?? 0.01;
      const fmt = (x) => `${Math.round(Number(x) * 1000) / 10}%`;
      const out = h('span', { class: 'mono val' }, fmt(eff));
      const range = h('input', { type: 'range', min: 0, max, step, value: eff });
      const auto = it.model ? h('button', { class: `chipbtn ${v == null ? 'on' : ''}`, title: 'Dùng giá trị model đã hiệu chỉnh',
        onClick: () => { set(null); render(); } }, 'Theo model') : null;
      range.addEventListener('input', () => { out.textContent = fmt(range.value); set(Number(range.value)); auto?.classList.remove('on'); });
      return h('span', { class: 'ctl' }, range, out, auto);
    }
    return h('span', { class: 'ctl' }, Stepper({ value: v, step: it.step, min: it.min, max: it.max, onChange: set }),
      it.unit ? h('span', { class: 'muted small0 unit' }, it.unit) : null);
  }

  const row = (it) => h('div', { class: 'set-row' }, h('div', { class: 'set-name' }, it.name, Tip(it.tip)), control(it));

  function render() {
    const open = [...body.querySelectorAll('details.set-adv')].map((d) => d.open);
    const theme = h('select', {}, [['', 'Theo hệ thống'], ['light', 'Sáng'], ['dark', 'Tối']].map(([v, t]) =>
      h('option', { value: v, selected: v === (localStorage.getItem('tn_theme') || '') }, t)));
    theme.addEventListener('change', () => {
      if (theme.value) document.documentElement.dataset.theme = theme.value;
      else delete document.documentElement.dataset.theme;
      try { localStorage.setItem('tn_theme', theme.value); } catch { /* ignore */ }
    });
    const key = h('input', { type: 'password', value: apiKey(), placeholder: 'để trống nếu không đặt' });
    key.addEventListener('change', () => { setApiKey(key.value); toast('Đã lưu key cho trình duyệt này'); });

    const cards = SECTIONS.map((sec) => {
      const items = sec.items.filter((it) => !(it.legacy && meta.detect_mode));
      const main = items.filter((it) => !it.adv), adv = items.filter((it) => it.adv);
      return h('section', { class: 'set-card' },
        h('header', {}, h('h3', {}, sec.title), sec.desc ? h('p', {}, sec.desc) : null),
        main.map(row),
        adv.length ? h('details', { class: 'set-adv' }, h('summary', {}, 'Nâng cao'), adv.map(row)) : null);
    });
    cards.forEach((c, i) => { const d = c.querySelector('details'); if (d && open[i]) d.open = true; });
    body.replaceChildren(h('h2', {}, 'Cài đặt'), ...cards,
      h('section', { class: 'set-card' }, h('header', {}, h('h3', {}, 'Giao diện & bảo mật')),
        h('div', { class: 'set-row' }, h('div', { class: 'set-name' }, 'Chủ đề'), theme),
        h('div', { class: 'set-row' }, h('div', { class: 'set-name' }, 'API key',
          Tip('Chỉ cần khi server có đặt API key. Key được lưu trong trình duyệt này và gửi kèm các lệnh điều khiển.')), key)),
      h('p', { class: 'muted small0', style: { textAlign: 'center' } }, `Server: ${meta.build || 'bản cũ (chưa có kết quả sớm)'}`),
      bar);
    dirty();
  }

  api.get('/settings').then(async (s) => {
    meta = s._meta || {};
    meta.build = (await api.get('/info').catch(() => ({}))).build;
    cfg = s;
    saved = JSON.stringify(values());
    render();
  }).catch((e) => toast(e.message));

  root.replaceChildren(body);
  return () => {};
}
