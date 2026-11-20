const BASE = '/api/v1';

export function apiKey() {
  try { return localStorage.getItem('tn_key') || ''; } catch { return ''; }
}

export function setApiKey(value) {
  try { localStorage.setItem('tn_key', value); } catch { /* storage unavailable */ }
}

async function call(method, path, body) {
  const headers = {};
  if (body !== undefined) headers['content-type'] = 'application/json';
  if (apiKey()) headers['x-api-key'] = apiKey();
  const res = await fetch(BASE + path, {
    method, headers, body: body === undefined ? undefined : JSON.stringify(body),
  });
  const data = await res.json().catch(() => ({}));
  if (!res.ok) throw Object.assign(new Error(data.detail || res.statusText), { status: res.status });
  return data;
}

export const api = {
  get: (p) => call('GET', p),
  post: (p, b = {}) => call('POST', p, b),
  patch: (p, b = {}) => call('PATCH', p, b),
  del: (p) => call('DELETE', p),
};
