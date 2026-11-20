/** Minimal observable store shared by views. */
const state = {
  labels: [],
  devices: {},
  sort: { state: 'idle', auto: false },
  actuator: null,
  result: null,
  connected: false,
};
const listeners = new Set();

export const store = {
  get: () => state,
  set(patch) {
    Object.assign(state, patch);
    listeners.forEach((fn) => fn(state));
  },
  on(fn) {
    listeners.add(fn);
    return () => listeners.delete(fn);
  },
  bin(label) {
    return state.labels.find((b) => b.id === label) || state.labels.find((b) => b.id === 'other') || {};
  },
};
