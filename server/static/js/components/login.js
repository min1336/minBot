/**
 * Login screen — face canvas animation + password auth.
 */

import { login } from '../api.js';
import { FaceRenderer } from '../face-renderer.js';
import { showMain } from '../app.js';

let face = null;

export function initLogin() {
  const canvas = document.getElementById('login-face');
  const form = document.getElementById('login-form');
  const input = document.getElementById('login-password');
  const error = document.getElementById('login-error');

  face = new FaceRenderer(canvas, 120);
  face.setEmotion('idle');
  face.start();

  form.addEventListener('submit', async (e) => {
    e.preventDefault();
    error.classList.add('hidden');
    error.textContent = '';

    const password = input.value.trim();
    if (!password) return;

    try {
      face.setEmotion('thinking');
      await login(password);
      face.setEmotion('happy');
      setTimeout(() => {
        face.stop();
        showMain();
      }, 600);
    } catch (err) {
      face.setEmotion('sad');
      error.textContent = err.message || '로그인 실패';
      error.classList.remove('hidden');
      input.classList.add('shake');
      setTimeout(() => input.classList.remove('shake'), 400);
      setTimeout(() => face.setEmotion('idle'), 1500);
    }
  });
}
