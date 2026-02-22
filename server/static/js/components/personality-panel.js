/**
 * Personality settings — bot name + 4 tag chip editors.
 */

import { getPersonality, updatePersonality } from '../api.js';
import { showToast } from '../app.js';

const TAG_FIELDS = [
  { id: 'tags-personality-traits', key: 'personality_traits' },
  { id: 'tags-speech-patterns', key: 'speech_patterns' },
  { id: 'tags-sentence-endings', key: 'sentence_endings' },
  { id: 'tags-favorite-expressions', key: 'favorite_expressions' },
];

let currentConfig = null;

export function initPersonalityPanel() {
  // Tag input handlers
  document.querySelectorAll('.tag-input').forEach(input => {
    input.addEventListener('keydown', (e) => {
      if (e.key !== 'Enter' || e.isComposing || !input.value.trim()) return;
      e.preventDefault();
      const targetId = input.dataset.target;
      const container = document.getElementById(targetId);
      addTag(container, input.value.trim());
      input.value = '';
    });
  });

  // Save button
  document.getElementById('btn-save-personality').addEventListener('click', savePersonality);

  // Reset button
  document.getElementById('btn-reset-personality').addEventListener('click', loadPersonality);

  // Data loaded on tab activation (not here — no token yet)
}

export async function loadPersonality() {
  try {
    currentConfig = await getPersonality();
    renderConfig(currentConfig);
  } catch (err) {
    showToast(err.message, 'error');
  }
}

function renderConfig(config) {
  document.getElementById('personality-name').value = config.bot_name || '미니';

  TAG_FIELDS.forEach(({ id, key }) => {
    const container = document.getElementById(id);
    clearTags(container);
    (config[key] || []).forEach(val => addTag(container, val));
  });
}

async function savePersonality() {
  const config = {
    bot_name: document.getElementById('personality-name').value.trim() || '미니',
  };

  TAG_FIELDS.forEach(({ id, key }) => {
    config[key] = getTagValues(document.getElementById(id));
  });

  try {
    currentConfig = await updatePersonality(config);
    showToast('성격 설정 저장 완료', 'success');
  } catch (err) {
    showToast(err.message, 'error');
  }
}

// ── Tag chip helpers ──────────────────────────────────────────────

function addTag(container, text) {
  const chip = document.createElement('span');
  chip.className = 'tag-chip';
  chip.dataset.value = text;

  const label = document.createElement('span');
  label.textContent = text;

  const remove = document.createElement('span');
  remove.className = 'tag-remove';
  remove.textContent = '\u00d7';
  remove.addEventListener('click', () => chip.remove());

  chip.appendChild(label);
  chip.appendChild(remove);
  container.appendChild(chip);
}

function clearTags(container) {
  while (container.firstChild) container.removeChild(container.firstChild);
}

function getTagValues(container) {
  return Array.from(container.querySelectorAll('.tag-chip'))
    .map(chip => chip.dataset.value);
}
