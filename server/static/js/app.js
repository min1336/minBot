/**
 * Application bootstrap — tab router, auth state, module loading.
 */

import { setToken, clearToken, getToken } from './api.js';
import { initLogin } from './components/login.js';
import { initDashboard, destroyDashboard } from './components/dashboard.js';
import { initChatPanel, destroyChatPanel } from './components/chat-panel.js';
import { initPersonalityPanel, loadPersonality } from './components/personality-panel.js';
import { initConversationsPanel } from './components/conversations-panel.js';
import { initDisplayModal } from './components/display-modal.js';
import { initVoiceModal } from './components/voice-modal.js';

// ── Screens ───────────────────────────────────────────────────────────

const loginScreen = document.getElementById('login-screen');
const mainScreen = document.getElementById('main-screen');

export function showMain() {
  loginScreen.classList.remove('active');
  loginScreen.classList.add('hidden');
  mainScreen.classList.remove('hidden');
  mainScreen.classList.add('active');
  initDashboard();
}

export function showLogin() {
  destroyDashboard();
  destroyChatPanel();
  clearToken();
  mainScreen.classList.remove('active');
  mainScreen.classList.add('hidden');
  loginScreen.classList.remove('hidden');
  loginScreen.classList.add('active');
}

// ── Tab Router ────────────────────────────────────────────────────────

const tabs = document.querySelectorAll('.header-tabs .tab');
const tabContents = document.querySelectorAll('.tab-content');
let activeTab = 'dashboard';

function switchTab(name) {
  activeTab = name;
  tabs.forEach(t => t.classList.toggle('active', t.dataset.tab === name));
  tabContents.forEach(tc => {
    const isActive = tc.id === `tab-${name}`;
    tc.classList.toggle('active', isActive);
    tc.classList.toggle('hidden', !isActive);
  });

  if (name === 'dashboard') initDashboard();
  else destroyDashboard();

  if (name === 'personality') loadPersonality();
  if (name === 'conversations') initConversationsPanel();
}

tabs.forEach(t => {
  t.addEventListener('click', () => switchTab(t.dataset.tab));
});

// ── Modals ────────────────────────────────────────────────────────────

function setupModal(btnId, modalId) {
  const btn = document.getElementById(btnId);
  const modal = document.getElementById(modalId);
  if (!btn || !modal) return;

  const close = modal.querySelector('.modal-close');
  const backdrop = modal.querySelector('.modal-backdrop');

  const show = () => { modal.classList.remove('hidden'); modal.classList.add('visible'); };
  const hide = () => { modal.classList.remove('visible'); modal.classList.add('hidden'); };

  btn.addEventListener('click', show);
  if (close) close.addEventListener('click', hide);
  if (backdrop) backdrop.addEventListener('click', hide);
}

setupModal('btn-display-modal', 'display-modal');
setupModal('btn-voice-modal', 'voice-modal');

// ── Logout ────────────────────────────────────────────────────────────

document.getElementById('btn-logout').addEventListener('click', showLogin);

// ── Auth expired listener ─────────────────────────────────────────────

window.addEventListener('auth:expired', showLogin);

// ── Toast ─────────────────────────────────────────────────────────────

export function showToast(message, type = 'info') {
  const container = document.getElementById('toast-container');
  const toast = document.createElement('div');
  toast.className = `toast toast-${type}`;
  toast.textContent = message;
  container.appendChild(toast);
  setTimeout(() => toast.remove(), 3000);
}

// ── Init ──────────────────────────────────────────────────────────────

initLogin();
initDisplayModal();
initVoiceModal();
initPersonalityPanel();
initChatPanel();
