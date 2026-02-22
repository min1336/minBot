/**
 * Pixel-art face renderer — faithful JS port of firmware sprites.h + emotions.cpp.
 *
 * 60x60 logical grid, each logical pixel = SCALE hardware pixels.
 * Circular clipping, 9 emotions, particle system, blink animation.
 */

// ── Colour palette (matching firmware RGB888 values) ──────────────────

const COL = {
  BG:    '#081030',
  PUPIL: '#101020',
  WHITE: '#FFFFFF',
  BLUSH: '#FF6080',
  HEART: '#FF1010',
  TEAR:  '#4080FF',
  STAR:  '#FFE000',
  ZZZ:   '#A0A0A0',
};

const CHAR_MAP = {
  '#': COL.PUPIL,
  W:   COL.WHITE,
  H:   COL.HEART,
  T:   COL.TEAR,
  Y:   COL.STAR,
  Z:   COL.ZZZ,
  B:   COL.BLUSH,
};

// ── Sprite data (from sprites.h) ─────────────────────────────────────

function sprite(w, rows) {
  return { w, h: rows.length, data: rows };
}

const SPR = {
  eye_idle: sprite(12, [
    '..########..',
    '.##########.',
    '############',
    '############',
    '####WW######',
    '####WW######',
    '.##########.',
    '..########..',
  ]),
  eye_listen: sprite(12, [
    '..########..',
    '.##########.',
    '############',
    '############',
    '####WW######',
    '####WW######',
    '############',
    '############',
    '.##########.',
    '..########..',
  ]),
  eye_think: sprite(12, [
    '..########..',
    '.##########.',
    '############',
    '########WW##',
    '########WW##',
    '############',
    '.##########.',
    '..########..',
  ]),
  eye_speak: sprite(12, [
    '.##########.',
    '############',
    '####WW######',
    '####WW######',
    '############',
    '.##########.',
  ]),
  eye_happy: sprite(12, [
    '..########..',
    '.##########.',
    '############',
    '...#####....',
    '............',
  ]),
  eye_sad: sprite(12, [
    '############',
    '############',
    '.##########.',
    '..########..',
    '..###WW###..',
    '.####WW####.',
    '.##########.',
    '..########..',
  ]),
  eye_surprised: sprite(12, [
    '...######...',
    '..########..',
    '.##########.',
    '############',
    '####WW######',
    '####WW######',
    '############',
    '############',
    '.##########.',
    '..########..',
    '...######...',
    '............',
  ]),
  eye_sleep: sprite(12, [
    '...######...',
    '.##########.',
    '############',
  ]),
  eye_tilt: sprite(8, [
    '##....##',
    '###..#..',
    '.####...',
    '..####..',
    '..####..',
    '.####...',
    '###..#..',
    '##....##',
  ]),
  eye_blink_half: sprite(12, [
    '############',
    '.##########.',
    '..########..',
    '............',
  ]),
  eye_blink_closed: sprite(12, [
    '.##########.',
    '############',
  ]),
  mouth_closed: sprite(16, [
    '....########....',
    '....########....',
  ]),
  mouth_half: sprite(16, [
    '...##########...',
    '..############..',
    '.###........###.',
    '..############..',
    '...##########...',
  ]),
  mouth_open: sprite(16, [
    '..############..',
    '.##############.',
    '####.........###',
    '###...........##',
    '###...........##',
    '####.........###',
    '.##############.',
    '..############..',
  ]),
  mouth_smile: sprite(16, [
    '##............##',
    '.##..........##.',
    '..##........##..',
    '...###....###...',
    '.....######.....',
  ]),
  mouth_frown: sprite(16, [
    '.....######.....',
    '...###....###...',
    '..##........##..',
    '.##..........##.',
    '##............##',
  ]),
  heart: sprite(8, [
    '.HH..HH.',
    'HHHHHHHH',
    'HHHHHHHH',
    '.HHHHHH.',
    '..HHHH..',
    '...HH...',
    '........',
  ]),
  tear: sprite(4, [
    '.T..',
    '.T..',
    'TTT.',
    'TTT.',
    'TTT.',
    'TTT.',
    '.TT.',
    '....',
  ]),
  star: sprite(8, [
    '...YY...',
    '...YY...',
    'Y..YY..Y',
    'YYYYYYYY',
    'YYYYYYYY',
    'Y..YY..Y',
    '...YY...',
    '...YY...',
  ]),
  zzz: sprite(12, [
    'ZZZZZ.......',
    '...ZZ.......',
    '..ZZ.ZZZZ...',
    '.ZZ....ZZ...',
    'ZZ....ZZZZZZ',
    '............',
  ]),
  dots: sprite(14, [
    '..............',
    'ZZ.ZZ.ZZ......',
    'ZZ.ZZ.ZZ......',
  ]),
  exclaim: sprite(3, [
    'WWW',
    'WWW',
    'WWW',
    'WWW',
    'WWW',
    'WWW',
    '...',
    '...',
    'WWW',
    'WWW',
  ]),
  blush: sprite(10, [
    '.B.B.B.B..',
    'B.B.B.B.B.',
    '.B.B.B.B..',
    '..........',
  ]),
};

// ── Logical positions (from emotions.cpp) ─────────────────────────────

const EYE_L_X = 7;
const EYE_R_X = 41;
const EYE_Y   = 15;
const MOUTH_X = 22;
const MOUTH_Y = 38;

// ── FaceRenderer class ───────────────────────────────────────────────

export class FaceRenderer {
  constructor(canvas, size = 240) {
    this.canvas = canvas;
    this.ctx = canvas.getContext('2d');
    this.setSize(size);

    // State
    this.emotion = 'idle';
    this.prevEmotion = null;
    this.audioLevel = 0;
    this.showGrid = false;

    // Blink
    this._blinkAcc = 0;
    this._blinkInterval = 2800 + Math.random() * 600;
    this._blinking = false;
    this._blinkFrame = 0;
    this._blinkFrameAcc = 0;

    // Thinking rotation
    this._thinkStep = 0;
    this._thinkAcc = 0;

    // Speaking mouth stage
    this._mouthStage = 0;

    // Transition
    this._transitioning = false;
    this._transProg = 0;

    // Particles
    this._particles = [];
    this._particleSpawnAcc = 0;

    // FPS tracking
    this._frameCount = 0;
    this._fpsAcc = 0;
    this.fps = 0;

    // Animation
    this._running = false;
    this._lastTime = 0;
  }

  setSize(size) {
    this.size = size;
    this.canvas.width = size;
    this.canvas.height = size;
    this.scale = size / 60;
  }

  setEmotion(emotion) {
    if (emotion === this.emotion) return;
    this.prevEmotion = this.emotion;
    this.emotion = emotion;
    this._transitioning = true;
    this._transProg = 0;

    // Reset emotion-specific state
    this._thinkStep = 0;
    this._thinkAcc = 0;
    this._mouthStage = 0;

    // Spawn particles
    this._particles = [];
    this._particleSpawnAcc = 0;
    this._spawnEmotionParticles(emotion);
  }

  setAudioLevel(level) {
    this.audioLevel = Math.max(0, Math.min(255, level));
  }

  start() {
    if (this._running) return;
    this._running = true;
    this._lastTime = performance.now();
    this._tick();
  }

  stop() {
    this._running = false;
  }

  // ── Main render loop ──────────────────────────────────────────────

  _tick() {
    if (!this._running) return;
    const now = performance.now();
    const dt = now - this._lastTime;
    this._lastTime = now;

    this._update(dt);
    this._render();

    // FPS
    this._frameCount++;
    this._fpsAcc += dt;
    if (this._fpsAcc >= 1000) {
      this.fps = this._frameCount;
      this._frameCount = 0;
      this._fpsAcc -= 1000;
    }

    requestAnimationFrame(() => this._tick());
  }

  _update(dt) {
    // Transition progress
    if (this._transitioning) {
      this._transProg += dt / 66; // 2-frame crossfade @ ~33ms/frame
      if (this._transProg >= 1) {
        this._transitioning = false;
        this._transProg = 1;
      }
    }

    // Emotion-specific updates
    switch (this.emotion) {
      case 'idle':
        this._updateBlink(dt);
        break;
      case 'thinking':
        this._thinkAcc += dt;
        if (this._thinkAcc >= 200) {
          this._thinkAcc = 0;
          this._thinkStep = (this._thinkStep + 1) % 8;
        }
        break;
      case 'speaking':
        this._updateMouthStage();
        break;
      case 'sleeping':
        this._particleSpawnAcc += dt;
        if (this._particleSpawnAcc >= 3000) {
          this._particleSpawnAcc = 0;
          this._particles.push({
            spr: SPR.zzz, x: 38, y: 10, vx: 1, vy: -2,
            life: 2500, maxLife: 2500,
          });
        }
        break;
    }

    // Update particles
    this._updateParticles(dt);
  }

  _updateBlink(dt) {
    this._blinkAcc += dt;
    if (!this._blinking && this._blinkAcc >= this._blinkInterval) {
      this._blinking = true;
      this._blinkFrame = 0;
      this._blinkFrameAcc = 0;
      this._blinkAcc = 0;
      this._blinkInterval = 2500 + Math.random() * 1500;
    }
    if (this._blinking) {
      this._blinkFrameAcc += dt;
      if (this._blinkFrameAcc >= 60) {
        this._blinkFrameAcc = 0;
        this._blinkFrame++;
        if (this._blinkFrame > 4) {
          this._blinking = false;
        }
      }
    }
  }

  _updateMouthStage() {
    const lvl = this.audioLevel;
    if (lvl > 180) this._mouthStage = 2;
    else if (lvl > 80) this._mouthStage = 1;
    else this._mouthStage = 0;
  }

  // ── Particle system ───────────────────────────────────────────────

  _spawnEmotionParticles(emotion) {
    switch (emotion) {
      case 'happy':
        this._particles.push(
          { spr: SPR.heart, x: 5,  y: 10, vx: -1, vy: -2, life: 1200, maxLife: 1200 },
          { spr: SPR.heart, x: 50, y: 8,  vx: 1,  vy: -2, life: 1400, maxLife: 1400 },
          { spr: SPR.heart, x: 28, y: 4,  vx: 0,  vy: -3, life: 1000, maxLife: 1000 },
        );
        break;
      case 'sad':
        this._particles.push(
          { spr: SPR.tear, x: 9,  y: 20, vx: 0, vy: 3, life: 1000, maxLife: 1000 },
          { spr: SPR.tear, x: 45, y: 20, vx: 0, vy: 3, life: 1200, maxLife: 1200 },
        );
        break;
      case 'tilted':
        this._particles.push(
          { spr: SPR.star, x: 5,  y: 30, vx: -2, vy: -1, life: 800,  maxLife: 800 },
          { spr: SPR.star, x: 50, y: 28, vx: 2,  vy: -1, life: 900,  maxLife: 900 },
          { spr: SPR.star, x: 28, y: 5,  vx: 0,  vy: -2, life: 700,  maxLife: 700 },
        );
        break;
    }
  }

  _updateParticles(dt) {
    for (let i = this._particles.length - 1; i >= 0; i--) {
      const p = this._particles[i];
      p.life -= dt;
      if (p.life <= 0) {
        this._particles.splice(i, 1);
        continue;
      }
      p.x += p.vx * dt / 100;
      p.y += p.vy * dt / 100;
    }
  }

  // ── Rendering ─────────────────────────────────────────────────────

  _render() {
    const { ctx, size, scale } = this;

    // Clear with BG
    ctx.fillStyle = COL.BG;
    ctx.fillRect(0, 0, size, size);

    // Circular clip
    ctx.save();
    ctx.beginPath();
    ctx.arc(size / 2, size / 2, size / 2, 0, Math.PI * 2);
    ctx.clip();

    // Fill clipped area with BG
    ctx.fillStyle = COL.BG;
    ctx.fillRect(0, 0, size, size);

    // Render emotion
    this._renderEmotion();

    // Render particles
    this._renderParticles();

    // Grid overlay
    if (this.showGrid) this._renderGrid();

    ctx.restore();
  }

  _renderEmotion() {
    const e = this.emotion;

    switch (e) {
      case 'idle':
        this._renderIdle();
        break;
      case 'listening':
        this._drawSprite(SPR.eye_listen, EYE_L_X, EYE_Y - 1);
        this._drawSprite(SPR.eye_listen, EYE_R_X, EYE_Y - 1);
        this._drawSprite(SPR.mouth_closed, MOUTH_X, MOUTH_Y);
        break;
      case 'thinking':
        this._renderThinking();
        break;
      case 'speaking':
        this._renderSpeaking();
        break;
      case 'happy':
        this._drawSprite(SPR.eye_happy, EYE_L_X, EYE_Y);
        this._drawSprite(SPR.eye_happy, EYE_R_X, EYE_Y);
        this._drawSprite(SPR.mouth_smile, MOUTH_X, MOUTH_Y);
        this._drawSprite(SPR.blush, 5, 32, 200);
        this._drawSprite(SPR.blush, 45, 32, 200);
        break;
      case 'sad':
        this._drawSprite(SPR.eye_sad, EYE_L_X, EYE_Y);
        this._drawSprite(SPR.eye_sad, EYE_R_X, EYE_Y);
        this._drawSprite(SPR.mouth_frown, MOUTH_X, MOUTH_Y);
        break;
      case 'surprised':
        this._drawSprite(SPR.eye_surprised, EYE_L_X, EYE_Y - 2);
        this._drawSprite(SPR.eye_surprised, EYE_R_X, EYE_Y - 2);
        this._drawSprite(SPR.mouth_open, MOUTH_X - 2, MOUTH_Y);
        this._drawSprite(SPR.exclaim, 28, 5);
        break;
      case 'sleeping':
        this._drawSprite(SPR.eye_sleep, EYE_L_X, EYE_Y + 3);
        this._drawSprite(SPR.eye_sleep, EYE_R_X, EYE_Y + 3);
        this._drawSprite(SPR.mouth_closed, MOUTH_X, MOUTH_Y, 200);
        break;
      case 'tilted':
        this._drawSprite(SPR.eye_tilt, EYE_L_X + 2, EYE_Y);
        this._drawSprite(SPR.eye_tilt, EYE_R_X + 2, EYE_Y);
        this._drawSprite(SPR.mouth_closed, MOUTH_X, MOUTH_Y, 180);
        break;
    }
  }

  _renderIdle() {
    if (this._blinking) {
      // Blink sequence: 1=half, 2=closed, 3=half, 4+=idle
      const f = this._blinkFrame;
      if (f === 1 || f === 3) {
        this._drawSprite(SPR.eye_blink_half, EYE_L_X, EYE_Y + 2);
        this._drawSprite(SPR.eye_blink_half, EYE_R_X, EYE_Y + 2);
      } else if (f === 2) {
        this._drawSprite(SPR.eye_blink_closed, EYE_L_X, EYE_Y + 3);
        this._drawSprite(SPR.eye_blink_closed, EYE_R_X, EYE_Y + 3);
      } else {
        this._drawSprite(SPR.eye_idle, EYE_L_X, EYE_Y);
        this._drawSprite(SPR.eye_idle, EYE_R_X, EYE_Y);
      }
    } else {
      this._drawSprite(SPR.eye_idle, EYE_L_X, EYE_Y);
      this._drawSprite(SPR.eye_idle, EYE_R_X, EYE_Y);
    }
    this._drawSprite(SPR.mouth_closed, MOUTH_X, MOUTH_Y);
  }

  _renderThinking() {
    // 8-step rotation: offsets cycle through [0,0] [1,-1] [1,1] [0,1] [1,1] [1,-1] [0,0] [0,1]
    const offsets = [
      [0, 0], [1, -1], [1, 1], [0, 1],
      [1, 1], [1, -1], [0, 0], [0, 1],
    ];
    const alphas = [255, 200, 150, 200, 150, 200, 255, 200];
    const [ox, oy] = offsets[this._thinkStep];
    const dotsAlpha = alphas[this._thinkStep];

    this._drawSprite(SPR.eye_think, EYE_L_X + ox, EYE_Y + oy);
    this._drawSprite(SPR.eye_think, EYE_R_X + ox, EYE_Y + oy);
    this._drawSprite(SPR.dots, MOUTH_X + 2, MOUTH_Y + 4, dotsAlpha);
  }

  _renderSpeaking() {
    this._drawSprite(SPR.eye_speak, EYE_L_X, EYE_Y + 1);
    this._drawSprite(SPR.eye_speak, EYE_R_X, EYE_Y + 1);

    switch (this._mouthStage) {
      case 0:
        this._drawSprite(SPR.mouth_closed, MOUTH_X, MOUTH_Y);
        break;
      case 1:
        this._drawSprite(SPR.mouth_half, MOUTH_X, MOUTH_Y);
        break;
      case 2:
        this._drawSprite(SPR.mouth_open, MOUTH_X - 2, MOUTH_Y);
        break;
    }
  }

  _renderParticles() {
    for (const p of this._particles) {
      const alpha = Math.max(0, p.life / p.maxLife);
      this._drawSprite(p.spr, Math.round(p.x), Math.round(p.y), Math.round(alpha * 255));
    }
  }

  // ── Sprite drawing ────────────────────────────────────────────────

  _drawSprite(spr, lx, ly, alpha = 255) {
    const { ctx, scale } = this;
    const prevAlpha = ctx.globalAlpha;
    ctx.globalAlpha = alpha / 255;

    for (let row = 0; row < spr.h; row++) {
      const rowStr = spr.data[row];
      for (let col = 0; col < spr.w; col++) {
        const ch = rowStr[col];
        if (ch === '.') continue;
        const color = CHAR_MAP[ch];
        if (!color) continue;
        ctx.fillStyle = color;
        ctx.fillRect(
          (lx + col) * scale,
          (ly + row) * scale,
          scale,
          scale,
        );
      }
    }

    ctx.globalAlpha = prevAlpha;
  }

  // ── Grid overlay ──────────────────────────────────────────────────

  _renderGrid() {
    const { ctx, size, scale } = this;
    ctx.strokeStyle = 'rgba(255,255,255,0.08)';
    ctx.lineWidth = 0.5;
    for (let i = 0; i <= 60; i++) {
      const pos = i * scale;
      ctx.beginPath(); ctx.moveTo(pos, 0); ctx.lineTo(pos, size); ctx.stroke();
      ctx.beginPath(); ctx.moveTo(0, pos); ctx.lineTo(size, pos); ctx.stroke();
    }
  }
}
