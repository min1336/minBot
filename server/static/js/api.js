/**
 * REST API client with JWT authentication.
 * All fetch calls include the Bearer token and handle 401 auto-redirect.
 */

let _token = null;

export function setToken(token) { _token = token; }
export function getToken() { return _token; }
export function clearToken() { _token = null; }

async function request(path, options = {}) {
  const headers = { ...options.headers };
  if (_token) {
    headers['Authorization'] = `Bearer ${_token}`;
  }
  if (!(options.body instanceof FormData) && options.body && typeof options.body === 'object') {
    headers['Content-Type'] = 'application/json';
    options.body = JSON.stringify(options.body);
  }

  const res = await fetch(path, { ...options, headers });

  if (res.status === 401) {
    clearToken();
    window.dispatchEvent(new CustomEvent('auth:expired'));
    throw new Error('Unauthorized');
  }

  if (!res.ok) {
    const detail = await res.json().catch(() => ({}));
    throw new Error(detail.detail || `HTTP ${res.status}`);
  }

  return res.json();
}

// ── Auth ──────────────────────────────────────────────────────────────

export async function login(password) {
  const data = await request('/api/auth/token', {
    method: 'POST',
    body: { password },
  });
  _token = data.access_token;
  return data;
}

// ── Status ────────────────────────────────────────────────────────────

export async function getStatus() {
  return request('/api/status');
}

// ── Personality ───────────────────────────────────────────────────────

export async function getPersonality() {
  return request('/api/personality');
}

export async function updatePersonality(config) {
  return request('/api/personality', {
    method: 'PUT',
    body: config,
  });
}

// ── Voice Provider ────────────────────────────────────────────────────

export async function switchVoiceProvider(provider) {
  return request('/api/voice-provider', {
    method: 'POST',
    body: { provider },
  });
}

// ── Voice Samples ─────────────────────────────────────────────────────

export async function uploadVoiceSamples(files) {
  const formData = new FormData();
  for (const file of files) {
    formData.append('files', file);
  }
  return request('/api/voice-samples', {
    method: 'POST',
    body: formData,
  });
}

// ── Conversations ─────────────────────────────────────────────────────

export async function getConversations(limit = 20) {
  return request(`/api/conversations?limit=${limit}`);
}
