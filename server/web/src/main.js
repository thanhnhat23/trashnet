import { api } from './api/client.js';
import { connectEvents } from './api/events.js';
import { h } from './components/dom.js';
import { store } from './state/store.js';
import * as devices from './views/devices.js';
import * as history from './views/history.js';
import * as live from './views/live.js';
import * as settings from './views/settings.js';
import * as stats from './views/stats.js';

const VIEWS = { live, history, stats, devices, settings };
const ICONS = {
  live: 'M2 8a2 2 0 0 1 2-2h2l1.5-2h5L14 6h2a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2zM10 15a3 3 0 1 0 0-6 3 3 0 0 0 0 6z',
  history: 'M10 3a7 7 0 1 0 7 7M10 6v4l3 2M3 3v4h4',
  stats: 'M4 16V9M10 16V4M16 16v-5',
  devices: 'M5 3h10v14H5zM8 14h4',
  settings: 'M10 7a3 3 0 1 0 0 6 3 3 0 0 0 0-6zM10 2v2M10 16v2M2 10h2M16 10h2M4.3 4.3l1.4 1.4M14.3 14.3l1.4 1.4M4.3 15.7l1.4-1.4M14.3 5.7l1.4-1.4',
};
function icon(k) {
  const s = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  s.setAttribute('viewBox', '0 0 20 20'); s.setAttribute('class', 'ico');
  s.setAttribute('fill', 'none'); s.setAttribute('stroke', 'currentColor'); s.setAttribute('stroke-width', '1.6');
  s.setAttribute('stroke-linecap', 'round'); s.setAttribute('stroke-linejoin', 'round');
  const pa = document.createElementNS('http://www.w3.org/2000/svg', 'path'); pa.setAttribute('d', ICONS[k]); s.append(pa);
  return s;
}
const NAMES = { live: 'Live', history: 'Lịch sử', stats: 'Thống kê', devices: 'Thiết bị', settings: 'Cài đặt' };
const view = document.getElementById('view');
const nav = document.getElementById('nav');
let unmount = null;

function route() {
  const name = (location.hash.replace('#/', '') || 'live');
  const key = VIEWS[name] ? name : 'live';
  unmount?.();
  unmount = VIEWS[key].mount(view) || null;
  nav.replaceChildren(...Object.keys(VIEWS).map((k) =>
    h('a', { href: `#/${k}`, class: k === key ? 'on' : '' }, icon(k), h('span', {}, NAMES[k]))));
}

function chip(ok, name) {
  return h('span', { class: 'chip', title: `${name}: ${ok ? 'kết nối' : 'mất kết nối'}` }, h('i', { class: `dot ${ok ? 'ok' : 'bad'}` }), h('span', { class: 'chip-t' }, name));
}

function renderStatus() {
  const { devices: d, connected } = store.get();
  document.getElementById('status').replaceChildren(
    chip(connected, 'Server'), chip(d.cam?.online, 'Cam'), chip(d.actuator?.online, 'Act'));
}

try {
  const saved = localStorage.getItem('tn_theme');
  if (saved) document.documentElement.dataset.theme = saved;
} catch { /* storage unavailable */ }

const info = await api.get('/info').catch(() => ({ labels: [] }));
store.set({ labels: info.labels });
store.on(renderStatus);
connectEvents();
renderStatus();
window.addEventListener('hashchange', route);
route();
