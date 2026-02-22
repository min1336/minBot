/**
 * Conversations panel — recent conversation log viewer.
 */

import { getConversations } from '../api.js';
import { showToast } from '../app.js';

export function initConversationsPanel() {
  const limitSelect = document.getElementById('conv-limit');
  const refreshBtn = document.getElementById('btn-refresh-conv');
  const listEl = document.getElementById('conversations-list');

  refreshBtn.addEventListener('click', () => load(listEl, limitSelect));
  limitSelect.addEventListener('change', () => load(listEl, limitSelect));

  // Data loaded on tab activation (not here — no token yet)
}

async function load(listEl, limitSelect) {
  const limit = parseInt(limitSelect.value);
  try {
    const convs = await getConversations(limit);
    renderList(listEl, convs);
  } catch (err) {
    showToast(err.message, 'error');
  }
}

function renderList(container, conversations) {
  while (container.firstChild) container.removeChild(container.firstChild);

  if (!conversations || conversations.length === 0) {
    const empty = document.createElement('p');
    empty.className = 'empty-state';
    empty.textContent = '아직 대화 기록이 없습니다.';
    container.appendChild(empty);
    return;
  }

  conversations.forEach(conv => {
    const item = document.createElement('div');
    item.className = 'conv-item';

    const time = document.createElement('div');
    time.className = 'conv-time';
    time.textContent = conv.timestamp
      ? new Date(conv.timestamp).toLocaleString('ko-KR')
      : '';

    const user = document.createElement('div');
    user.className = 'conv-user';
    user.textContent = conv.user_message || '';

    const bot = document.createElement('div');
    bot.className = 'conv-bot';
    bot.textContent = conv.bot_response || '';

    if (conv.emotion) {
      const tag = document.createElement('span');
      tag.className = 'emotion-tag';
      tag.textContent = conv.emotion;
      bot.appendChild(tag);
    }

    item.appendChild(time);
    item.appendChild(user);
    item.appendChild(bot);
    container.appendChild(item);
  });
}
