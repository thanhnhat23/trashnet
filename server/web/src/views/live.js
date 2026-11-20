import { api } from '../api/client.js';
import { h, toast } from '../components/dom.js';
import { BinDot, ResultCard, STEP_TEXT, StatusDot, Switch } from '../components/widgets.js';
import { store } from '../state/store.js';

export function mount(root) {
  const img = h('img', { alt: 'camera' });
  const overlay = h('div', { class: 'bbox', style: { display: 'none' } });
  const stage = h('div', { class: 'stage' }, img, overlay);
  const resultBox = h('div', {});
  const stateLine = h('div', { class: 'muted small' });
  const alerts = h('div', {});
  const switches = h('div', {});
  const capture = h('button', { class: 'primary btn-block' }, 'Chụp & phân loại');
  const ref = h('button', {}, 'Đặt khay trống');
  let overlayTimer;
  let flashOn = false;
  let tab = 'auto';
  let running = 0, runStart = 0;   // bin whose scenario was started from the remote, and when
  const tabsBox = h('div', { class: 'tabs' });
  const autoPane = h('div', {});
  const remotePane = h('div', {});

  const run = (fn) => async () => { try { await fn(); } catch (e) { toast(e.message); } };
  const guard = (btn, fn) => btn.addEventListener('click', async () => {
    btn.disabled = true;
    try { await fn(); } catch (e) { toast(e.message); } finally { btn.disabled = false; }
  });

  guard(capture, async () => {
    const r = await api.post('/sort/capture', {});
    if (r.error === 'no_reference') toast('Hãy bấm "Đặt khay trống" khi khay chưa có rác.');
    else if (r.error) toast(r.error === 'bin_full' ? 'Ngăn đầy: hãy đổ rác trước' : r.error);
    store.set({ result: r });
  });
  guard(ref, async () => { const r = await api.post('/camera/reference'); toast(r.ok ? 'Đã lưu ảnh khay trống' : r.error); });

  async function reconnect(btn) {
    btn.disabled = true; btn.textContent = 'Đang tìm…';
    try {
      const r = await api.post('/devices/rescan');
      store.set({ devices: Object.fromEntries(r.devices.map((x) => [x.role, x])) });
      toast(r.devices.every((x) => x.online) ? 'Đã kết nối lại' : 'Chưa thấy thiết bị: kiểm tra nguồn và Wi-Fi');
    } catch (e) { toast(e.message); } finally { btn.disabled = false; btn.textContent = 'Kết nối lại'; }
  }

  function drawBox(r) {
    if (!r?.box || !r.frame) return;
    const [x1, y1, x2, y2] = r.box;
    const [w, hgt] = r.frame;
    const imgRatio = w / hgt, boxRatio = stage.clientWidth / stage.clientHeight;
    const scale = imgRatio > boxRatio ? stage.clientWidth / w : stage.clientHeight / hgt;
    const offX = (stage.clientWidth - w * scale) / 2, offY = (stage.clientHeight - hgt * scale) / 2;
    Object.assign(overlay.style, {
      display: 'block', left: `${offX + x1 * scale}px`, top: `${offY + y1 * scale}px`,
      width: `${(x2 - x1) * scale}px`, height: `${(y2 - y1) * scale}px`, borderColor: store.bin(r.label).color,
    });
    clearTimeout(overlayTimer);
    overlayTimer = setTimeout(() => { overlay.style.display = 'none'; }, 4000);
  }

  // Re-render a part only when the state it shows changed (SSE ticks every second; rebuilding everything made
  // buttons flicker and lose hover/focus).
  const last = {};
  const changed = (part, key) => { const k = JSON.stringify(key); if (last[part] === k) return false; last[part] = k; return true; };

  function render() {
    const s = store.get();
    const moving = ['pending', 'sent'].includes(s.result?.dispatch?.state);
    if (changed('result', [s.result, moving ? s.sort.step : null, s.labels.length])) {
      resultBox.replaceChildren(ResultCard(s.result, s.sort));
      if (!last.boxTs || last.boxTs !== s.result?.ts) { last.boxTs = s.result?.ts; drawBox(s.result); }
    }
    const step = STEP_TEXT[s.sort.step] || s.sort.step;
    const STATE_TEXT = { capture: 'chụp ảnh', detect: 'nhận diện', classify: 'nhận diện', presence: 'tìm vật', dispatch: 'gửi lệnh', wait: step || 'chờ mâm', cooldown: 'chờ rác rơi' };
    if (changed('state', [s.sort.state, step])) {
      stateLine.replaceChildren(StatusDot(s.sort.state === 'idle' ? 'ok' : 'warn',
        s.sort.state === 'idle' ? 'Sẵn sàng' : `Đang ${STATE_TEXT[s.sort.state] || s.sort.state}…`));
      capture.disabled = s.sort.state !== 'idle';
    }

    const down = ['cam', 'actuator'].filter((r) => !s.devices[r]?.online).map((r) => (r === 'cam' ? 'ESP32-CAM' : 'Actuator'));
    const a = s.actuator;
    const full = (a?.full || []).map((f, i) => (f ? i + 1 : 0)).filter(Boolean);
    if (!changed('alerts', [down, full, !!a?.tray_lost])) { /* unchanged */ } else {
    const items = [];
    if (down.length) {
      const b = h('button', { class: 'primary' }, 'Kết nối lại');
      b.addEventListener('click', () => reconnect(b));
      items.push(h('div', { class: 'alert' }, h('span', { class: 'grow' }, StatusDot('bad', `${down.join(', ')} chưa kết nối`)), b));
    }
    if (full.length) items.push(h('div', { class: 'alert' }, h('span', { class: 'grow' }, StatusDot('bad', `Ngăn đầy: ${full.join(', ')}. Hãy đổ rác trước khi phân loại tiếp.`))));
    if (a?.tray_lost) items.push(h('div', { class: 'alert' }, h('span', { class: 'grow' }, StatusDot('warn', 'Mất vị trí mâm: chỉnh về ngăn 1 rồi bấm "Đặt làm Home" ở tab Thiết bị.'))));
    alerts.replaceChildren(...items);
    }

    if (changed('switches', [s.sort.auto, flashOn])) switches.replaceChildren(
      Switch('Tự động phân loại', s.sort.auto, run(async () => { await api.post('/sort/auto', { enabled: !store.get().sort.auto }); })),
      Switch('Đèn flash', flashOn, run(async () => { flashOn = !flashOn; await api.post('/camera/flash', { on: flashOn }); render(); })));

    if (changed('tabs', tab)) tabsBox.replaceChildren(...[['auto', 'Tự động'], ['remote', 'Điều khiển']].map(([k, label]) =>
      h('button', { class: tab === k ? 'on' : '', onClick: () => { tab = k; render(); } }, label)));
    autoPane.style.display = tab === 'auto' ? '' : 'none';
    remotePane.style.display = tab === 'remote' ? '' : 'none';
    if (running && a?.state === 'idle' && Date.now() - runStart > 1500) running = 0;
    const busy = s.sort.state !== 'idle' || (a && a.state && a.state !== 'idle');
    if (changed('remote', [running, busy, a?.state, a?.step, s.labels.length])) remotePane.replaceChildren(
      h('div', { class: 'remote' }, s.labels.slice().sort((x, y) => x.bin - y.bin).map((b) =>
        BinDot(b, { active: running === b.bin, onClick: async () => {
          if (running || busy) { toast('Đang chạy, chờ xong rồi bấm tiếp'); return; }
          running = b.bin; runStart = Date.now(); render();
          try { await api.post('/actuator/control', { action: 'bin', bin: b.bin }); } catch (e) { running = 0; toast(e.message); render(); }
        } }))),
      h('div', { class: 'remote-foot muted small' }, a?.state && a.state !== 'idle'
        ? `Đang ${a.step && a.step !== 'idle' ? (STEP_TEXT[a.step] || a.step) : 'chạy'}…` : 'Chạm vào loại rác để chạy cả kịch bản'));
  }

  autoPane.append(resultBox, stateLine, capture,
    h('div', { class: 'panel', style: { marginTop: 'var(--s3)' } }, switches, h('div', { class: 'btn-line' }, ref)));
  // Stream: fps badge + watchdog. The <img> stays silent when MJPEG stalls, so ask the server how fresh the
  // last frame is and reconnect after 3 s without frames.
  const fps = h('span', { class: 'fps' }, '');
  stage.append(fps);
  const connect = () => { img.src = `/api/v1/camera/stream?t=${Date.now()}`; };
  let stale = 0;
  const watch = setInterval(async () => {
    if (document.hidden) return;
    try {
      const st = await api.get('/camera/stats');
      const dead = st.clients === 0 || st.age_ms == null || st.age_ms > 3000;
      fps.textContent = dead ? 'mất hình' : `${Math.round(st.fps)} fps`;
      fps.className = `fps ${dead ? 'bad' : st.fps < 6 ? 'warn' : ''}`;
      stale = dead ? stale + 1 : 0;
      if (stale >= 2 && store.get().devices.cam?.online) { stale = 0; connect(); }
    } catch { fps.textContent = ''; }
  }, 1500);
  const onVis = () => { if (!document.hidden) connect(); else img.src = ''; };   // no bandwidth for a hidden tab
  document.addEventListener('visibilitychange', onVis);
  connect();
  const off = store.on(render);
  render();

  root.classList.add('wide');
  root.replaceChildren(h('div', { class: 'grid live' },
    h('div', {}, h('h2', {}, 'Camera'), stage),
    h('div', {},
      alerts,
      tabsBox,
      autoPane, remotePane)));

  return () => { root.classList.remove('wide'); off(); img.src = ''; clearTimeout(overlayTimer); clearInterval(watch); document.removeEventListener('visibilitychange', onVis); };
}
