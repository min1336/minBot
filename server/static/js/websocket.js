/**
 * WebSocket audio client for minBot.
 *
 * Handles:
 *  - Microphone capture → PCM 16-bit 16kHz → WS binary
 *  - WS binary (MP3) → AudioContext → speaker playback
 *  - JSON control messages (emotion, status, cancel_playback, barge_in)
 */

export class AudioWebSocket {
  constructor() {
    this.ws = null;
    this.audioCtx = null;
    this._micStream = null;
    this._scriptNode = null;
    this._source = null;
    this._playQueue = [];
    this._playing = false;
    this.recording = false;
    this.connected = false;

    // Audio level (RMS) for face mouth sync
    this.audioLevel = 0;

    // Callbacks
    this.onEmotion = null;     // (emotion: string) => void
    this.onStatus = null;      // (data: string) => void
    this.onTranscript = null;  // (text: string) => void
    this.onConnected = null;
    this.onDisconnected = null;
    this.onAudioLevel = null;  // (level: number 0-255) => void
  }

  // ── Connection ──────────────────────────────────────────────────

  connect(token) {
    if (this.ws) this.disconnect();

    const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
    const url = `${proto}//${location.host}/audio`;
    this.ws = new WebSocket(url);
    this.ws.binaryType = 'arraybuffer';

    this.ws.onopen = () => {
      this.connected = true;
      this.onConnected?.();
    };

    this.ws.onclose = () => {
      this.connected = false;
      this.recording = false;
      this.onDisconnected?.();
    };

    this.ws.onerror = () => {
      this.connected = false;
    };

    this.ws.onmessage = (evt) => {
      if (evt.data instanceof ArrayBuffer) {
        this._handleAudioData(evt.data);
      } else {
        this._handleJsonMessage(evt.data);
      }
    };
  }

  disconnect() {
    this.stopMic();
    if (this.ws) {
      this.ws.close();
      this.ws = null;
    }
    this.connected = false;
    this._playQueue = [];
  }

  // ── JSON messages ───────────────────────────────────────────────

  _handleJsonMessage(text) {
    try {
      const msg = JSON.parse(text);
      switch (msg.type) {
        case 'emotion':
          this.onEmotion?.(msg.emotion);
          break;
        case 'status':
          this.onStatus?.(msg.data);
          break;
        case 'cancel_playback':
          this._clearPlayback();
          break;
      }
    } catch { /* ignore non-JSON */ }
  }

  sendBargeIn() {
    if (this.ws?.readyState === WebSocket.OPEN) {
      this.ws.send(JSON.stringify({ type: 'barge_in' }));
    }
    this._clearPlayback();
  }

  // ── Microphone capture ──────────────────────────────────────────

  async startMic() {
    if (this.recording) return;

    this.audioCtx = this.audioCtx || new AudioContext({ sampleRate: 16000 });
    if (this.audioCtx.state === 'suspended') await this.audioCtx.resume();

    this._micStream = await navigator.mediaDevices.getUserMedia({
      audio: { sampleRate: 16000, channelCount: 1, echoCancellation: true },
    });

    this._source = this.audioCtx.createMediaStreamSource(this._micStream);
    this._scriptNode = this.audioCtx.createScriptProcessor(512, 1, 1);

    this._scriptNode.onaudioprocess = (e) => {
      if (!this.recording || !this.ws || this.ws.readyState !== WebSocket.OPEN) return;
      const float32 = e.inputBuffer.getChannelData(0);

      // Compute RMS for visual level
      let sum = 0;
      for (let i = 0; i < float32.length; i++) sum += float32[i] * float32[i];
      const rms = Math.sqrt(sum / float32.length);
      this.audioLevel = Math.min(255, Math.round(rms * 255 * 8));
      this.onAudioLevel?.(this.audioLevel);

      // Float32 → Int16 PCM
      const int16 = new Int16Array(float32.length);
      for (let i = 0; i < float32.length; i++) {
        const s = Math.max(-1, Math.min(1, float32[i]));
        int16[i] = s < 0 ? s * 0x8000 : s * 0x7FFF;
      }
      this.ws.send(int16.buffer);
    };

    this._source.connect(this._scriptNode);
    this._scriptNode.connect(this.audioCtx.destination);
    this.recording = true;
  }

  stopMic() {
    this.recording = false;
    this.audioLevel = 0;
    if (this._scriptNode) {
      this._scriptNode.disconnect();
      this._scriptNode = null;
    }
    if (this._source) {
      this._source.disconnect();
      this._source = null;
    }
    if (this._micStream) {
      this._micStream.getTracks().forEach(t => t.stop());
      this._micStream = null;
    }
  }

  // ── Audio playback (MP3 chunks from server) ─────────────────────

  _handleAudioData(buffer) {
    this._playQueue.push(buffer);
    if (!this._playing) this._playNext();
  }

  async _playNext() {
    if (this._playQueue.length === 0) {
      this._playing = false;
      this.onAudioLevel?.(0);
      return;
    }
    this._playing = true;
    const buffer = this._playQueue.shift();

    try {
      this.audioCtx = this.audioCtx || new AudioContext({ sampleRate: 16000 });
      const audioBuffer = await this.audioCtx.decodeAudioData(buffer.slice(0));
      const source = this.audioCtx.createBufferSource();
      source.buffer = audioBuffer;
      source.connect(this.audioCtx.destination);
      source.onended = () => this._playNext();
      source.start();

      // Estimate audio level from playback buffer for mouth sync
      const data = audioBuffer.getChannelData(0);
      let sum = 0;
      const step = Math.max(1, Math.floor(data.length / 100));
      for (let i = 0; i < data.length; i += step) sum += data[i] * data[i];
      const rms = Math.sqrt(sum / (data.length / step));
      const level = Math.min(255, Math.round(rms * 255 * 6));
      this.onAudioLevel?.(level);
    } catch {
      this._playNext();
    }
  }

  _clearPlayback() {
    this._playQueue = [];
    this._playing = false;
    this.onAudioLevel?.(0);
  }
}
