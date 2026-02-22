/**
 * Digital display modal — large face canvas with emotion controls.
 */

import { FaceRenderer } from '../face-renderer.js';

let face = null;

export function initDisplayModal() {
  const canvas = document.getElementById('display-face');
  face = new FaceRenderer(canvas, 240);
  face.start();

  // Emotion buttons
  const btns = document.querySelectorAll('#emotion-buttons .emotion-btn');
  btns.forEach(btn => {
    btn.addEventListener('click', () => {
      btns.forEach(b => b.classList.remove('active'));
      btn.classList.add('active');
      face.setEmotion(btn.dataset.emotion);
    });
  });

  // Audio level slider
  const slider = document.getElementById('audio-level-slider');
  const sliderValue = document.getElementById('audio-level-value');
  slider.addEventListener('input', () => {
    const val = parseInt(slider.value);
    sliderValue.textContent = val;
    face.setAudioLevel(val);
  });

  // Canvas size buttons
  const sizeBtns = document.querySelectorAll('[data-size]');
  sizeBtns.forEach(btn => {
    btn.addEventListener('click', () => {
      sizeBtns.forEach(b => b.classList.remove('active'));
      btn.classList.add('active');
      const size = parseInt(btn.dataset.size);
      face.setSize(size);
    });
  });

  // Grid overlay toggle
  const gridCheckbox = document.getElementById('grid-overlay');
  gridCheckbox.addEventListener('change', () => {
    face.showGrid = gridCheckbox.checked;
  });

  // FPS counter update
  setInterval(() => {
    const el = document.getElementById('display-fps');
    if (el) el.textContent = `${face.fps} FPS`;
  }, 500);
}
