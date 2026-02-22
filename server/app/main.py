"""
minBot FastAPI server — main application entry point.

Wires together the full AI pipeline:
  PCM audio (WebSocket) → VAD → STT → LLM → TTS → audio back to client

Pipeline timing targets:
  STT  ~90-150 ms  (Deepgram Nova-3 streaming)
  LLM  ~50-200 ms  (Groq Llama 4 Scout)
  TTS  ~75-135 ms  (ElevenLabs Flash v2.5)
  Total goal: < 500 ms end-to-end
"""

from __future__ import annotations

import asyncio
import json
import logging
import struct
from contextlib import asynccontextmanager
from dataclasses import dataclass, field
from typing import Any

from pathlib import Path

import numpy as np
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles

from app.config import get_settings
from app.models.schemas import Emotion, WSMessageType
from app.pipeline.stt import DeepgramSTT
from app.pipeline.llm import GroqLLM, extract_emotion
from app.pipeline.tts import ElevenLabsTTS, split_sentences
from app.personality.prompt_builder import PromptBuilder
from app.personality.speech_learner import SpeechLearner

# ---------------------------------------------------------------------------
# Logging
# ---------------------------------------------------------------------------

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
)
logger = logging.getLogger(__name__)

# ---------------------------------------------------------------------------
# Application-wide singletons (initialised in lifespan)
# ---------------------------------------------------------------------------

_prompt_builder: PromptBuilder | None = None
_speech_learner: SpeechLearner | None = None

# ---------------------------------------------------------------------------
# VAD helpers
# ---------------------------------------------------------------------------

# Silero VAD is loaded lazily to avoid import-time GPU/CPU overhead.
_silero_model: Any | None = None
_silero_utils: Any | None = None

# Audio constants (must match ESP32 firmware)
_SAMPLE_RATE = 16_000        # Hz
_FRAME_MS = 30               # ms per VAD frame
_FRAME_SAMPLES = _SAMPLE_RATE * _FRAME_MS // 1000   # 480 samples
_FRAME_BYTES = _FRAME_SAMPLES * 2                    # 16-bit PCM → 2 bytes/sample

# VAD thresholds
_VAD_THRESHOLD = 0.5         # Silero speech probability threshold
_SPEECH_HANGOVER_FRAMES = 8  # frames of silence before utterance is declared done
_MIN_SPEECH_FRAMES = 3       # minimum frames to count as speech (avoids noise blips)
_MAX_VAD_BUFFER_BYTES = _SAMPLE_RATE * 2 * 5  # 5 seconds of 16kHz 16-bit PCM


def _load_silero_vad() -> bool:
    """Attempt to load Silero VAD. Returns True on success."""
    global _silero_model, _silero_utils
    try:
        import torch  # type: ignore
        model, utils = torch.hub.load(
            repo_or_dir="snakers4/silero-vad",
            model="silero_vad",
            force_reload=False,
            onnx=False,
        )
        _silero_model = model
        _silero_utils = utils
        logger.info("Silero VAD loaded successfully")
        return True
    except Exception as exc:
        logger.warning("Silero VAD unavailable (%s); using energy-based VAD fallback", exc)
        return False


def _energy_vad(pcm_bytes: bytes, threshold_rms: float = 300.0) -> float:
    """Simple RMS energy VAD. Returns 1.0 if speech is likely, else 0.0."""
    if len(pcm_bytes) < 2:
        return 0.0
    samples = np.frombuffer(pcm_bytes, dtype=np.int16).astype(np.float32)
    rms = float(np.sqrt(np.mean(samples ** 2)))
    return 1.0 if rms >= threshold_rms else 0.0


def _vad_probability(pcm_bytes: bytes) -> float:
    """Return speech probability [0.0, 1.0] for one audio frame."""
    if _silero_model is not None and _silero_utils is not None:
        try:
            import torch  # type: ignore
            samples = np.frombuffer(pcm_bytes, dtype=np.int16).astype(np.float32) / 32768.0
            tensor = torch.from_numpy(samples)
            prob = float(_silero_model(tensor, _SAMPLE_RATE).item())
            return prob
        except Exception as exc:
            logger.debug("Silero VAD inference error: %s; falling back to energy", exc)
    return _energy_vad(pcm_bytes)


# ---------------------------------------------------------------------------
# Per-connection state
# ---------------------------------------------------------------------------

@dataclass
class ConnectionState:
    """Mutable state scoped to a single WebSocket connection."""

    # VAD / utterance tracking
    vad_buffer: bytes = b""              # leftover PCM bytes not yet forming a full frame
    speech_frames: int = 0              # consecutive frames classified as speech
    silence_frames: int = 0             # consecutive frames of silence after speech
    is_speaking: bool = False           # True while the user is actively speaking

    # Pipeline components (per-connection instances)
    stt: DeepgramSTT | None = None
    llm: GroqLLM | None = None
    tts: ElevenLabsTTS | None = None

    # Pipeline control
    pipeline_task: asyncio.Task | None = None   # running LLM→TTS task
    barge_in_event: asyncio.Event = field(default_factory=asyncio.Event)

    # Display / emotion state
    current_emotion: Emotion = Emotion.IDLE

    def reset_vad(self) -> None:
        self.vad_buffer = b""
        self.speech_frames = 0
        self.silence_frames = 0
        self.is_speaking = False


# ---------------------------------------------------------------------------
# Lifespan (startup / shutdown)
# ---------------------------------------------------------------------------

@asynccontextmanager
async def lifespan(app: FastAPI):
    """Initialize shared resources on startup; clean up on shutdown."""
    global _prompt_builder, _speech_learner

    settings = get_settings()

    # Load VAD model (non-blocking attempt)
    _load_silero_vad()

    # Initialize personality components
    _prompt_builder = PromptBuilder(bot_name=settings.bot_name)
    _speech_learner = SpeechLearner(data_dir="data/conversations")

    # Load previously saved speech patterns from disk
    try:
        await _speech_learner.load_patterns("data/patterns.json")
        patterns = _speech_learner.get_top_patterns()
        _prompt_builder.set_speech_patterns(patterns)
        logger.info("Speech patterns loaded and applied to PromptBuilder")
    except Exception as exc:
        logger.warning("Could not load speech patterns on startup: %s", exc)

    logger.info(
        "minBot server started | bot=%s host=%s port=%d",
        settings.bot_name,
        settings.server_host,
        settings.server_port,
    )

    yield

    # Shutdown: persist current learned patterns
    if _speech_learner is not None:
        try:
            await _speech_learner.save_patterns("data/patterns.json")
            logger.info("Speech patterns saved on shutdown")
        except Exception as exc:
            logger.warning("Could not save speech patterns on shutdown: %s", exc)

    logger.info("minBot server shut down cleanly")


# ---------------------------------------------------------------------------
# FastAPI app
# ---------------------------------------------------------------------------

app = FastAPI(title="minBot Server", version="1.0.0", lifespan=lifespan)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=False,
    allow_methods=["*"],
    allow_headers=["*"],
)

# Include REST API router — imported here to avoid circular imports.
# The routes module is created separately; if it doesn't exist yet this
# import will raise at startup, which is the correct behaviour.
try:
    from app.api.routes import router as api_router
    app.include_router(api_router, prefix="/api")
    logger.info("API router mounted at /api")
except ImportError:
    logger.warning("app.api.routes not found — REST API endpoints will be unavailable")


# ---------------------------------------------------------------------------
# Static files (developer dashboard UI)
# ---------------------------------------------------------------------------

_static_dir = Path(__file__).parent.parent / "static"
if _static_dir.exists():
    app.mount("/", StaticFiles(directory=str(_static_dir), html=True), name="static")
    logger.info("Static files mounted from %s", _static_dir)


# ---------------------------------------------------------------------------
# WebSocket helpers
# ---------------------------------------------------------------------------

async def _send_json(ws: WebSocket, payload: dict) -> None:
    """Send a JSON control message to the client (swallows disconnect errors)."""
    try:
        await ws.send_text(json.dumps(payload, ensure_ascii=False))
    except Exception:
        pass


async def _send_bytes(ws: WebSocket, data: bytes) -> None:
    """Send raw binary audio to the client (swallows disconnect errors)."""
    try:
        await ws.send_bytes(data)
    except Exception:
        pass


async def _set_emotion(ws: WebSocket, state: ConnectionState, emotion: Emotion) -> None:
    """Update the displayed emotion and notify the client."""
    if state.current_emotion != emotion:
        state.current_emotion = emotion
        await _send_json(ws, {"type": WSMessageType.EMOTION, "emotion": emotion.value})


# ---------------------------------------------------------------------------
# Pipeline: LLM → TTS → send audio
# ---------------------------------------------------------------------------

async def _run_pipeline(
    ws: WebSocket,
    state: ConnectionState,
    transcript: str,
) -> None:
    """Run LLM generation → TTS synthesis → stream audio back.

    Designed to run as an asyncio Task so it can be cancelled on barge-in.
    """
    if not transcript.strip():
        logger.debug("Empty transcript; skipping pipeline")
        return

    assert state.llm is not None
    assert state.tts is not None

    settings = get_settings()

    # Build system prompt incorporating learned speech patterns
    system_prompt = ""
    if _prompt_builder is not None:
        try:
            system_prompt = _prompt_builder.build_system_prompt()
        except Exception as exc:
            logger.warning("PromptBuilder failed: %s", exc)

    await _set_emotion(ws, state, Emotion.THINKING)
    logger.info("Pipeline start | transcript=%r", transcript[:80])

    sentence_buffer = ""
    full_response_parts: list[str] = []

    try:
        async for token in state.llm.generate(transcript, system_prompt=system_prompt):
            # Check barge-in between every token
            if state.barge_in_event.is_set():
                logger.info("Barge-in detected during LLM generation — aborting")
                return

            sentence_buffer += token
            full_response_parts.append(token)

            # Try to split on sentence boundaries and TTS each complete sentence
            sentences = split_sentences(sentence_buffer)
            if len(sentences) > 1:
                # All but the last element are complete sentences
                for sentence in sentences[:-1]:
                    clean, emotion_str = extract_emotion(sentence)
                    if clean.strip():
                        try:
                            emotion = Emotion(emotion_str)
                        except ValueError:
                            emotion = Emotion.SPEAKING
                        await _set_emotion(ws, state, emotion)

                        logger.debug("TTS sentence: %r", clean[:60])
                        try:
                            async for audio_chunk in state.tts.synthesize_sentence(clean):
                                if state.barge_in_event.is_set():
                                    return
                                await _send_bytes(ws, audio_chunk)
                        except RuntimeError as exc:
                            logger.error("TTS error: %s", exc)
                            await _send_json(
                                ws,
                                {"type": WSMessageType.STATUS, "data": f"TTS error: {exc}"},
                            )
                            return

                # Keep only the last (potentially incomplete) sentence in the buffer
                sentence_buffer = sentences[-1]

        # Synthesize any remaining text in the buffer after LLM finishes
        if sentence_buffer.strip() and not state.barge_in_event.is_set():
            clean, emotion_str = extract_emotion(sentence_buffer)
            if clean.strip():
                try:
                    emotion = Emotion(emotion_str)
                except ValueError:
                    emotion = Emotion.SPEAKING
                await _set_emotion(ws, state, emotion)
                try:
                    async for audio_chunk in state.tts.synthesize_sentence(clean):
                        if state.barge_in_event.is_set():
                            break
                        await _send_bytes(ws, audio_chunk)
                except RuntimeError as exc:
                    logger.error("TTS error (tail): %s", exc)

        # Learn from this exchange
        if _speech_learner is not None and full_response_parts:
            full_text = "".join(full_response_parts)
            try:
                await _speech_learner.learn_from_text(transcript)
                # Refresh prompt builder with updated patterns
                if _prompt_builder is not None:
                    _prompt_builder.set_speech_patterns(_speech_learner.get_top_patterns())
            except Exception as exc:
                logger.debug("SpeechLearner update error: %s", exc)

    except asyncio.CancelledError:
        logger.info("Pipeline task cancelled (barge-in)")
        state.llm.cancel()
        state.tts.cancel()
        raise
    except Exception as exc:
        logger.exception("Unexpected pipeline error: %s", exc)
        await _send_json(ws, {"type": WSMessageType.STATUS, "data": f"Pipeline error: {exc}"})
    finally:
        if not state.barge_in_event.is_set():
            await _set_emotion(ws, state, Emotion.IDLE)


# ---------------------------------------------------------------------------
# VAD processing: consume incoming PCM bytes frame by frame
# ---------------------------------------------------------------------------

async def _process_audio_frame(
    ws: WebSocket,
    state: ConnectionState,
    frame: bytes,
) -> str | None:
    """Process one 30 ms PCM frame through VAD.

    Returns the final transcript string when an utterance boundary is detected,
    otherwise returns None.
    """
    prob = _vad_probability(frame)
    is_speech = prob >= _VAD_THRESHOLD

    if is_speech:
        state.silence_frames = 0
        state.speech_frames += 1

        if not state.is_speaking and state.speech_frames >= _MIN_SPEECH_FRAMES:
            # Transition: silence → speaking
            state.is_speaking = True
            logger.debug("VAD: speech start detected (prob=%.2f)", prob)
            await _set_emotion(ws, state, Emotion.LISTENING)

            # Open STT stream
            if state.stt is None:
                settings = get_settings()
                state.stt = DeepgramSTT(api_key=settings.deepgram_api_key)
            try:
                await state.stt.start_stream()
            except Exception as exc:
                logger.error("STT stream start failed: %s", exc)
                state.is_speaking = False
                return None

        # Forward audio to STT
        if state.is_speaking and state.stt is not None:
            await state.stt.send_audio(frame)

    else:
        # Silence frame
        if state.is_speaking:
            state.silence_frames += 1

            # Still forward a little audio during hangover for accurate endpointing
            if state.stt is not None and state.silence_frames <= _SPEECH_HANGOVER_FRAMES // 2:
                await state.stt.send_audio(frame)

            if state.silence_frames >= _SPEECH_HANGOVER_FRAMES:
                # Transition: speaking → silence  (utterance complete)
                logger.debug("VAD: speech end detected after %d hangover frames", state.silence_frames)
                state.is_speaking = False

                transcript = ""
                if state.stt is not None:
                    transcript = await state.stt.stop_stream()
                    state.stt = None

                state.speech_frames = 0
                state.silence_frames = 0
                return transcript.strip() if transcript else None

    return None


# ---------------------------------------------------------------------------
# WebSocket endpoint
# ---------------------------------------------------------------------------

@app.websocket("/audio")
async def websocket_audio(ws: WebSocket):
    """Full-duplex audio WebSocket endpoint.

    Binary frames: raw PCM 16-bit 16 kHz mono audio (any chunk size).
    Text frames:   JSON control messages, e.g. {"type": "barge_in"}.

    Audio output to client:
      - Binary frames: MP3 audio (ElevenLabs Flash v2.5 output).
      - Text frames:   JSON emotion/status messages.
    """
    await ws.accept()
    settings = get_settings()
    logger.info("WebSocket connected: %s", ws.client)

    state = ConnectionState(
        llm=GroqLLM(api_key=settings.groq_api_key),
        tts=ElevenLabsTTS(
            api_key=settings.elevenlabs_api_key,
            voice_id=settings.elevenlabs_voice_id,
        ),
    )

    # Notify client we are ready
    await _send_json(ws, {"type": WSMessageType.STATUS, "data": "connected"})
    await _set_emotion(ws, state, Emotion.IDLE)

    try:
        while True:
            message = await ws.receive()

            # ----------------------------------------------------------------
            # Binary frame — PCM audio
            # ----------------------------------------------------------------
            if "bytes" in message and message["bytes"] is not None:
                audio_data: bytes = message["bytes"]

                # Append incoming bytes with overflow protection
                if len(state.vad_buffer) + len(audio_data) > _MAX_VAD_BUFFER_BYTES:
                    logger.warning("VAD buffer overflow; dropping audio and resetting")
                    state.reset_vad()
                    continue
                state.vad_buffer += audio_data

                # Consume complete 30 ms frames
                while len(state.vad_buffer) >= _FRAME_BYTES:
                    frame = state.vad_buffer[:_FRAME_BYTES]
                    state.vad_buffer = state.vad_buffer[_FRAME_BYTES:]

                    transcript = await _process_audio_frame(ws, state, frame)

                    if transcript:
                        logger.info("Utterance complete | transcript=%r", transcript[:100])

                        # Cancel any ongoing pipeline before starting a new one
                        if state.pipeline_task and not state.pipeline_task.done():
                            state.pipeline_task.cancel()
                            try:
                                await state.pipeline_task
                            except (asyncio.CancelledError, Exception):
                                pass

                        state.barge_in_event.clear()
                        state.pipeline_task = asyncio.create_task(
                            _run_pipeline(ws, state, transcript),
                            name="pipeline",
                        )

            # ----------------------------------------------------------------
            # Text frame — JSON control message
            # ----------------------------------------------------------------
            elif "text" in message and message["text"] is not None:
                try:
                    payload: dict = json.loads(message["text"])
                except json.JSONDecodeError:
                    logger.warning("Received non-JSON text frame; ignoring")
                    continue

                msg_type = payload.get("type", "")

                if msg_type == WSMessageType.BARGE_IN:
                    logger.info("Barge-in received from client")

                    # Signal running pipeline to stop
                    state.barge_in_event.set()
                    if state.llm is not None:
                        state.llm.cancel()
                    if state.tts is not None:
                        state.tts.cancel()

                    # Cancel the pipeline task
                    if state.pipeline_task and not state.pipeline_task.done():
                        state.pipeline_task.cancel()
                        try:
                            await state.pipeline_task
                        except (asyncio.CancelledError, Exception):
                            pass

                    # Also abort any active STT stream
                    if state.stt is not None:
                        state.stt.cancel()
                        try:
                            await state.stt.stop_stream()
                        except Exception:
                            pass
                        state.stt = None

                    state.reset_vad()

                    # Inform client to stop playing buffered audio
                    await _send_json(ws, {"type": WSMessageType.CANCEL_PLAYBACK})
                    await _set_emotion(ws, state, Emotion.IDLE)

                else:
                    logger.debug("Unknown control message type: %r", msg_type)

    except WebSocketDisconnect:
        logger.info("WebSocket disconnected: %s", ws.client)
    except Exception as exc:
        logger.exception("WebSocket handler error: %s", exc)
    finally:
        # Clean up per-connection resources
        if state.pipeline_task and not state.pipeline_task.done():
            state.pipeline_task.cancel()
            try:
                await state.pipeline_task
            except (asyncio.CancelledError, Exception):
                pass

        if state.stt is not None:
            try:
                state.stt.cancel()
                await state.stt.stop_stream()
            except Exception:
                pass

        logger.info("WebSocket cleanup complete: %s", ws.client)


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    import uvicorn

    settings = get_settings()
    uvicorn.run(
        "app.main:app",
        host=settings.server_host,
        port=settings.server_port,
        reload=True,
    )
