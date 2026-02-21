"""
ElevenLabs streaming TTS module for minBot.

Uses ElevenLabs Flash v2.5 for low-latency voice synthesis with streaming output.
Supports barge-in cancellation and sentence-boundary detection for early TTS start.
"""

import asyncio
import logging
import re
from typing import AsyncIterator

from elevenlabs import AsyncElevenLabs, VoiceSettings
from elevenlabs.core import ApiError

from app.config import get_settings

logger = logging.getLogger(__name__)

# Sentence-ending punctuation pattern (Korean and Latin)
_SENTENCE_BOUNDARY = re.compile(r"(?<=[.!?。！？])\s+")


def split_sentences(text: str) -> list[str]:
    """Split text on sentence boundaries (.!?) to enable early TTS start.

    Returns a list of non-empty sentence strings. Preserves punctuation at
    the end of each sentence.

    Examples:
        >>> split_sentences("Hello world. How are you? Fine!")
        ['Hello world.', 'How are you?', 'Fine!']
        >>> split_sentences("단일 문장")
        ['단일 문장']
    """
    if not text or not text.strip():
        return []

    parts = _SENTENCE_BOUNDARY.split(text.strip())
    return [p.strip() for p in parts if p.strip()]


class ElevenLabsTTS:
    """Async streaming TTS client wrapping the ElevenLabs SDK.

    Generates audio via ElevenLabs Flash v2.5 with streaming output.
    Supports per-request cancellation for barge-in (user interruption).

    Usage:
        tts = ElevenLabsTTS(api_key="...", voice_id="...")
        async for chunk in tts.synthesize_stream("Hello world"):
            send_to_device(chunk)
        tts.cancel()  # stop mid-stream if user interrupts
    """

    MODEL_ID = "eleven_flash_v2_5"
    # mp3_44100_128 is universally supported; switch to opus_48000_32 if the
    # ESP32 decoder is updated to handle Opus over WebSocket.
    OUTPUT_FORMAT = "mp3_44100_128"

    def __init__(self, api_key: str, voice_id: str) -> None:
        self._api_key = api_key
        self._voice_id = voice_id
        self._client = AsyncElevenLabs(api_key=api_key)
        self._cancelled: bool = False

    # ------------------------------------------------------------------
    # Public API
    # ------------------------------------------------------------------

    def cancel(self) -> None:
        """Signal the current stream to stop iteration immediately.

        Call this when the user interrupts (barge-in). The ongoing
        ``synthesize_stream`` or ``synthesize_sentence`` generator will
        raise ``StopAsyncIteration`` on the next chunk check.
        """
        self._cancelled = True
        logger.debug("TTS cancel requested")

    async def synthesize_stream(self, text: str) -> AsyncIterator[bytes]:
        """Stream audio chunks for the full text.

        Audio is yielded as raw bytes in the configured output format
        (default: mp3_44100_128). Iteration stops immediately if
        ``cancel()`` has been called.

        Args:
            text: The text to synthesise.

        Yields:
            Raw audio bytes suitable for streaming to the ESP32.

        Raises:
            RuntimeError: On unrecoverable ElevenLabs API errors.
        """
        self._cancelled = False
        async for chunk in self._stream(text):
            if self._cancelled:
                logger.debug("TTS stream cancelled mid-flight")
                return
            yield chunk

    async def synthesize_sentence(self, sentence: str) -> AsyncIterator[bytes]:
        """Stream audio for a single sentence.

        Functionally equivalent to ``synthesize_stream`` but signals intent
        (one complete sentence).  Useful when the caller has already split
        text via ``split_sentences`` and wants to pipeline TTS with LLM
        generation.

        Args:
            sentence: A single sentence string.

        Yields:
            Raw audio bytes.
        """
        async for chunk in self.synthesize_stream(sentence):
            yield chunk

    # ------------------------------------------------------------------
    # Internal helpers
    # ------------------------------------------------------------------

    async def _stream(self, text: str) -> AsyncIterator[bytes]:
        """Low-level streaming call to the ElevenLabs SDK.

        Wraps rate-limit and API errors with logging and re-raises as
        ``RuntimeError`` so callers have a single exception type to handle.
        """
        if not text or not text.strip():
            logger.warning("TTS received empty text; skipping synthesis")
            return

        voice_settings = VoiceSettings(
            stability=0.5,
            similarity_boost=0.75,
            style=0.0,
            use_speaker_boost=True,
        )

        try:
            logger.debug(
                "Starting TTS stream | model=%s format=%s chars=%d",
                self.MODEL_ID,
                self.OUTPUT_FORMAT,
                len(text),
            )
            async for chunk in await self._client.text_to_speech.convert(
                voice_id=self._voice_id,
                text=text,
                model_id=self.MODEL_ID,
                voice_settings=voice_settings,
                output_format=self.OUTPUT_FORMAT,
            ):
                if chunk:
                    yield chunk

        except ApiError as exc:
            status = getattr(exc, "status_code", None)
            if status == 429:
                logger.error(
                    "ElevenLabs rate limit hit (429). "
                    "Back off and retry or reduce request frequency."
                )
                raise RuntimeError("ElevenLabs TTS rate limit exceeded") from exc
            elif status == 401:
                logger.error("ElevenLabs authentication failed (401). Check API key.")
                raise RuntimeError("ElevenLabs TTS authentication error") from exc
            else:
                logger.error("ElevenLabs API error: %s", exc)
                raise RuntimeError(f"ElevenLabs TTS error: {exc}") from exc
        except asyncio.CancelledError:
            logger.debug("TTS asyncio task cancelled")
            raise
        except Exception as exc:
            logger.exception("Unexpected error during TTS synthesis: %s", exc)
            raise RuntimeError(f"TTS synthesis failed: {exc}") from exc


def create_tts() -> ElevenLabsTTS:
    """Factory that reads credentials from app settings.

    Returns:
        A ready-to-use ``ElevenLabsTTS`` instance.
    """
    settings = get_settings()
    return ElevenLabsTTS(
        api_key=settings.elevenlabs_api_key,
        voice_id=settings.elevenlabs_voice_id,
    )
