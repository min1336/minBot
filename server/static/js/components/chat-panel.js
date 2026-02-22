/**
 * Chat panel — full pipeline test with mic/text input and face display.
 */

import { FaceRenderer } from '../face-renderer.js';
import { AudioWebSocket } from '../websocket.js';
import { showToast } from '../app.js';

let face = null;
let audioWs = null;

export function initChatPanel() {
  const canvas = document.getElementById('chat-face');
  face = new FaceRenderer(canvas, 120);
  face.start();

  audioWs = new AudioWebSocket();

  // ── Callbacks ─────────────────────────────────────────────────

  audioWs.onEmotion = (emotion) => {
    face.setEmotion(emotion);
  };

  audioWs.onStatus = (data) => {
    if (data === 'connected') {
      showToast('WebSocket 연결됨', 'success');
    }
  };

  audioWs.onAudioLevel = (level) => {
    face.setAudioLevel(level);
  };

  audioWs.onDisconnected = () => {
    face.setEmotion('idle');
    updateMicButton(false);
  };

  // ── Mic button ────────────────────────────────────────────────

  const micBtn = document.getElementById('btn-mic');
  const micIcon = document.getElementById('mic-icon');

  micBtn.addEventListener('click', async () => {
    if (!audioWs.connected) {
      audioWs.connect();
      // Wait for connection
      await new Promise(r => setTimeout(r, 500));
    }

    if (audioWs.recording) {
      audioWs.stopMic();
      updateMicButton(false);
    } else {
      try {
        await audioWs.startMic();
        updateMicButton(true);
      } catch (err) {
        showToast('마이크 접근 권한이 필요합니다', 'error');
      }
    }
  });

  function updateMicButton(recording) {
    micBtn.classList.toggle('recording', recording);
    micIcon.textContent = recording ? 'ON' : 'MIC';
  }

  // ── Stop button (barge-in) ────────────────────────────────────

  const stopBtn = document.getElementById('btn-stop');
  stopBtn.addEventListener('click', () => {
    audioWs.sendBargeIn();
    face.setEmotion('idle');
    face.setAudioLevel(0);
  });

  // ── Text input ────────────────────────────────────────────────

  const textInput = document.getElementById('chat-text-input');
  const history = document.getElementById('chat-history');

  textInput.addEventListener('keydown', async (e) => {
    if (e.key !== 'Enter' || e.isComposing || !textInput.value.trim()) return;
    e.preventDefault();

    const text = textInput.value.trim();
    textInput.value = '';

    // Add user bubble
    addBubble(text, 'user');

    // Ensure WebSocket is connected before sending
    if (!audioWs.connected) {
      audioWs.connect();
      // Wait for connection (up to 3s)
      for (let i = 0; i < 30; i++) {
        if (audioWs.ws?.readyState === WebSocket.OPEN) break;
        await new Promise(r => setTimeout(r, 100));
      }
    }

    // Send text message via WebSocket
    if (audioWs.ws?.readyState === WebSocket.OPEN) {
      audioWs.ws.send(JSON.stringify({ type: 'text', data: text }));
      face.setEmotion('thinking');
    } else {
      addBubble('WebSocket 연결 실패. 다시 시도해주세요.', 'bot');
    }
  });

  // Listen for emotion changes to parse [EMOTION:xxx] from chat
  const origOnStatus = audioWs.onStatus;
  audioWs.onStatus = (data) => {
    origOnStatus?.(data);

    if (typeof data === 'string' && data !== 'connected') {
      // Check for emotion tags
      const emotionMatch = data.match(/\[EMOTION:(\w+)\]/);
      const cleanText = data.replace(/\[EMOTION:\w+\]/g, '').trim();
      const emotion = emotionMatch ? emotionMatch[1] : null;

      if (cleanText) {
        addBubble(cleanText, 'bot', emotion);
      }
    }
  };

  function addBubble(text, type, emotion = null) {
    const bubble = document.createElement('div');
    bubble.className = `chat-bubble ${type}`;
    bubble.textContent = text;
    if (emotion) {
      const tag = document.createElement('span');
      tag.className = 'emotion-tag';
      tag.textContent = emotion;
      bubble.appendChild(tag);
    }
    history.appendChild(bubble);
    history.scrollTop = history.scrollHeight;
  }
}

export function destroyChatPanel() {
  if (audioWs) {
    audioWs.disconnect();
  }
}
