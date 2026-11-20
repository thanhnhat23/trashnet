import { store } from '../state/store.js';

/** Subscribe to server-sent events and mirror them into the store. */
export function connectEvents() {
  const es = new EventSource('/api/v1/events');
  const parse = (e) => JSON.parse(e.data);

  es.addEventListener('hello', (e) => {
    const d = parse(e);
    store.set({
      connected: true,
      devices: Object.fromEntries(d.devices.map((x) => [x.role, x])),
      sort: { state: d.state, auto: d.auto },
    });
  });
  es.addEventListener('device', (e) => {
    const d = parse(e);
    store.set({ devices: { ...store.get().devices, [d.role]: d } });
  });
  es.addEventListener('sort', (e) => store.set({ sort: parse(e) }));
  es.addEventListener('result', (e) => store.set({ result: parse(e) }));
  es.addEventListener('actuator', (e) => store.set({ actuator: parse(e) }));
  es.onerror = () => store.set({ connected: false });
  es.onopen = () => store.set({ connected: true });
  return () => es.close();
}
