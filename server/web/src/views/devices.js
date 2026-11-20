import { api } from '../api/client.js';
import { h, toast } from '../components/dom.js';
import { DeviceRow, STEP_TEXT, Segmented, Stepper } from '../components/widgets.js';
import { store } from '../state/store.js';

const GROUPS = [
  ['Mâm quay', [
    ['neutral_us', 'Xung đứng yên (µs)', 1],
    ['delta_us', 'Biên độ quay Δ (µs)', 5],
    ['t90_fwd_ms', 'Quay 90° thuận (ms)', 10],
    ['t90_rev_ms', 'Quay 90° nghịch (ms)', 10],
    ['t180_fwd_ms', 'Quay 180° thuận (ms)', 10],
    ['t180_rev_ms', 'Quay 180° nghịch (ms)', 10],
    ['ramp_ms', 'Tăng/giảm tốc (ms)', 10],
    ['brake_ms', 'Phanh (ms)', 5],
    ['brake_us', 'Lực phanh (µs)', 5],
  ]],
  ['Servo hất', [
    ['dump_rest', 'Góc nghỉ', 1],
    ['dump_target', 'Góc hất', 1],
    ['dump_fwd_ms', 'Thời gian mở (ms)', 50],
    ['dump_hold_ms', 'Giữ mở (ms)', 100],
    ['dump_rev_ms', 'Thời gian đóng (ms)', 50],
  ]],
  ['Nắp', [
    ['lid_rest', 'Góc nắp đóng', 1],
    ['lid_open', 'Góc nắp mở', 1],
    ['lid_settle_ms', 'Chờ nắp mở (ms)', 50],
  ]],
  ['Cảm biến', [
    ['full_cm', 'CB1: ngưỡng đầy (cm)', 0.5],
    ['lid_cm', 'CB2: ngưỡng mở nắp (cm)', 0.5],
  ]],
];

export function mount(root) {
  const devices = h('div', {});
  const status = h('div', { class: 'pills' });
  const alerts = h('div', {});
  const calibBox = h('div', {});
  let calib = null;

  const act = (body, msg) => async () => {
    try { await api.post('/actuator/control', body); if (msg) toast(msg); } catch (e) { toast(e.message); }
  };

  function renderDevices() {
    const s = store.get();
    devices.replaceChildren(DeviceRow(s.devices.cam, 'ESP32-CAM'), DeviceRow(s.devices.actuator, 'Actuator'));
    const a = s.actuator;
    if (!a?.online) { status.replaceChildren(h('span', { class: 'muted' }, 'Actuator offline')); }
    else {
      const busy = a.state && a.state !== 'idle';
      const mk = (t, cls = '') => h('span', { class: `pill ${cls}` }, t);
      status.replaceChildren(
        mk(busy ? `Đang ${STEP_TEXT[a.step] || 'chạy'}` : 'Sẵn sàng', busy ? 'warn' : 'ok'),
        mk(`Ngăn ${a.position}`),
        mk(`Rác ${a.dist_full_cm ?? '--'} cm`),
        mk(`Nắp ${a.dist_lid_cm ?? '--'} cm`),
        ...(a.full ? a.full.map((f, i) => mk(`${i + 1}`, f ? 'bad' : 'ok')) : []));
    }
    const warn = a?.tray_lost ? 'Mất vị trí mâm: nhích mâm về ngăn 1 rồi bấm "Đặt làm Home".'
      : (a?.needs_home ? 'Mâm có thể đã lệch: kiểm tra ngăn 1, nếu lệch thì jog và "Đặt làm Home".' : null);
    alerts.replaceChildren(...(warn ? [h('div', { class: 'alert' }, warn)] : []));
  }

  function manualRow(role, label) {
    const input = h('input', { type: 'text', placeholder: `${label}: http://192.168.x.x`, value: '' });
    const key = h('input', { type: 'password', placeholder: 'key' });
    return h('div', { class: 'row' }, input, key, h('button', {
      onClick: async () => {
        try { await api.post('/devices/manual', { role, url: input.value, key: key.value || null }); toast('Đã lưu'); } catch (e) { toast(e.message); }
      },
    }, 'Lưu'));
  }

  async function saveCalib() {
    try { await api.post('/actuator/calib', calib); toast('Đã lưu'); } catch (e) { toast(e.message); }
  }

  function renderCalib() {
    if (!calib) { calibBox.replaceChildren(h('div', { class: 'muted' }, 'Không đọc được (actuator offline?)')); return; }
    const trim = calib.trim_ms || [0, 0, 0, 0];
    const field = ([key, label, step]) => h('label', { class: 'field' }, label,
      Stepper({ value: calib[key], step, onChange: (v) => { calib[key] = v; } }));
    const groups = GROUPS.map(([title, fields]) => {
      const extra = [];
      if (title === 'Mâm quay') {
        extra.push(h('label', { class: 'field' }, 'Ngăn 4 quay theo',
          Segmented([{ value: 1, label: 'Thuận' }, { value: -1, label: 'Nghịch' }], calib.bin4_dir ?? 1,
            (v) => { calib.bin4_dir = v; renderCalib(); })));
        extra.push(h('label', { class: 'field' }, 'Trim ngăn (ms)',
          h('span', { class: 'trim' },
            trim.map((v, i) => Stepper({ value: v, step: 10, onChange: (n) => { calib.trim_ms[i] = n; } })))));
      }
      if (title === 'Nắp') {
        extra.push(h('div', { class: 'btn-line' }, h('button', {
          onClick: async () => { [calib.lid_rest, calib.lid_open] = [calib.lid_open, calib.lid_rest]; await saveCalib(); renderCalib(); },
        }, 'Đảo chiều nắp')));
      }
      return h('details', { class: 'acc' }, h('summary', {}, title),
        h('div', { class: 'acc-body' }, ...fields.filter(([k]) => k in calib).map(field), ...extra));
    });
    calibBox.replaceChildren(...groups, h('button', { class: 'primary btn-block', onClick: saveCalib }, 'Lưu hiệu chuẩn'));
  }

  async function loadCalib() {
    try { calib = await api.get('/actuator/calib'); } catch { calib = null; }
    renderCalib();
  }

  const rescan = h('button', { class: 'primary' }, 'Kết nối lại');
  rescan.addEventListener('click', async () => {
    rescan.disabled = true; rescan.textContent = 'Đang tìm…';
    try {
      const r = await api.post('/devices/rescan');
      store.set({ devices: Object.fromEntries(r.devices.map((x) => [x.role, x])) });
      toast(r.devices.every((x) => x.online) ? 'Đã kết nối lại' : 'Chưa thấy thiết bị');
    } catch (err) { toast(err.message); } finally { rescan.disabled = false; rescan.textContent = 'Kết nối lại'; }
  });

  const bins = [1, 2, 3, 4].map((n) => h('button', { onClick: act({ action: 'rotate', bin: n }) }, `Ngăn ${n}`));
  const jog = [[-1, 30], [-1, 10], [1, 10], [1, 30]].map(([dir, ms]) =>
    h('button', { onClick: act({ action: 'jog', dir, ms }) }, `${dir > 0 ? '+' : '−'}${ms}`));

  root.replaceChildren(
    h('h2', {}, 'Thiết bị'),
    h('div', { class: 'panel' }, devices, status, h('div', { class: 'btn-line', style: { marginTop: 'var(--s2)' } }, rescan),
      h('details', { class: 'acc flat' }, h('summary', {}, 'Nhập địa chỉ tay'), h('div', { class: 'acc-body' }, manualRow('cam', 'CAM'), manualRow('actuator', 'Actuator')))),
    alerts,
    h('h2', {}, 'Thử nhanh'),
    h('div', { class: 'panel' }, h('h3', {}, 'Quay mâm tới'), h('div', { class: 'btn-line' }, bins)),
    h('div', { class: 'panel' }, h('h3', {}, 'Nắp và hất'),
      h('div', { class: 'btn-line' },
        h('button', { onClick: act({ action: 'lid_open' }) }, 'Mở nắp'),
        h('button', { onClick: act({ action: 'lid_close' }) }, 'Đóng nắp'),
        h('button', { onClick: act({ action: 'dump' }) }, 'Hất rác'))),
    h('div', { class: 'panel' }, h('h3', {}, 'Căn chỉnh mâm (ms)'),
      h('div', { class: 'btn-line' }, jog, h('button', { onClick: act({ action: 'home_set' }, 'Đã đặt Home') }, 'Đặt làm Home'))),
    h('h2', {}, 'Hiệu chuẩn'), calibBox);

  renderDevices();
  loadCalib();
  const off = store.on(renderDevices);
  return off;
}
