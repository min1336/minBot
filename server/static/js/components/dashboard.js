/**
 * Status dashboard — 6-card grid with 5-second polling.
 */

import { getStatus, switchVoiceProvider } from '../api.js';
import { FaceRenderer } from '../face-renderer.js';
import { showToast } from '../app.js';

let pollTimer = null;
let miniFace = null;

export function initDashboard() {
  if (pollTimer) return;

  // Mini face in emotion card
  const canvas = document.getElementById('emotion-mini-face');
  if (canvas && !miniFace) {
    miniFace = new FaceRenderer(canvas, 48);
    miniFace.start();
  }

  // Voice provider change
  const select = document.getElementById('voice-provider-select');
  select.addEventListener('change', async () => {
    try {
      await switchVoiceProvider(select.value);
      showToast(`음성 프로바이더: ${select.value}`, 'success');
    } catch (err) {
      showToast(err.message, 'error');
    }
  });

  // Start polling
  fetchStatus();
  pollTimer = setInterval(fetchStatus, 5000);
}

export function destroyDashboard() {
  if (pollTimer) {
    clearInterval(pollTimer);
    pollTimer = null;
  }
}

async function fetchStatus() {
  try {
    const s = await getStatus();
    updateUI(s);
  } catch {
    // silently fail on poll
  }
}

function updateUI(status) {
  // Battery
  const bar = document.getElementById('battery-bar');
  const batText = document.getElementById('battery-text');
  const pct = status.battery_percent;
  bar.style.width = `${pct}%`;
  bar.style.background = pct > 50 ? 'var(--accent-green)' : pct > 20 ? 'var(--accent-yellow)' : 'var(--accent-red)';
  batText.textContent = `${pct}%`;

  // Connection
  const dot = document.getElementById('connection-dot');
  const connText = document.getElementById('connection-text');
  const rssi = document.getElementById('wifi-rssi');
  dot.className = `dot ${status.is_connected ? 'dot-green' : 'dot-red'}`;
  connText.textContent = status.is_connected ? '연결됨' : '연결 끊김';
  rssi.textContent = `${status.wifi_rssi} dBm`;

  // Emotion
  const emotionText = document.getElementById('emotion-text');
  emotionText.textContent = status.current_emotion;
  if (miniFace) miniFace.setEmotion(status.current_emotion);

  // Uptime
  const uptimeText = document.getElementById('uptime-text');
  const s = status.uptime_seconds;
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  const sec = s % 60;
  uptimeText.textContent = `${h}h ${m}m ${sec}s`;
}
