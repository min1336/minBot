"""Deepgram Nova-3 streaming STT module for minBot.

Uses Deepgram Python SDK v5 async API with real-time websocket transcription.
Supports partial transcripts, barge-in cancellation, and callback notification.
"""

from __future__ import annotations

import asyncio
import logging
from typing import Callable, Optional

from deepgram import AsyncDeepgramClient
from deepgram.core.events import EventType

from app.config import get_settings

logger = logging.getLogger(__name__)


class DeepgramSTT:
    """Deepgram Nova-3 real-time streaming STT for PCM 16bit 16kHz audio."""

    def __init__(
        self,
        api_key: Optional[str] = None,
        on_transcript: Optional[Callable[[str, bool], None]] = None,
    ) -> None:
        """
        Args:
            api_key: Deepgram API key. Falls back to settings if not provided.
            on_transcript: Optional callback invoked on each transcript event.
                Signature: on_transcript(text: str, is_final: bool)
        """
        settings = get_settings()
        self._api_key = api_key or settings.deepgram_api_key
        self.on_transcript: Optional[Callable[[str, bool], None]] = on_transcript

        self._client: Optional[AsyncDeepgramClient] = None
        self._connection = None
        self._transcript_parts: list[str] = []
        self._final_transcript: str = ""
        self._cancelled: bool = False
        self._connected: bool = False

    # ------------------------------------------------------------------
    # Public API
    # ------------------------------------------------------------------

    async def start_stream(self) -> None:
        """Open Deepgram WebSocket connection and start a live transcription session."""
        self._cancelled = False
        self._transcript_parts = []
        self._final_transcript = ""

        self._client = AsyncDeepgramClient(api_key=self._api_key)

        try:
            self._connection = self._client.listen.v1.connect(
                model="nova-3",
                language="ko",
                smart_format=True,
                interim_results=True,
                endpointing="300",
                encoding="linear16",
                sample_rate="16000",
                channels="1",
            )

            self._connection.on(EventType.MESSAGE, self._on_message)
            self._connection.on(EventType.ERROR, self._on_error)
            self._connection.on(EventType.CLOSE, self._on_close)

            await self._connection.start_listening()
            self._connected = True
            logger.info("Deepgram STT stream started (nova-3, ko, 16kHz)")

        except Exception as exc:
            logger.error("Failed to start Deepgram stream: %s", exc)
            self._connected = False
            raise

    async def send_audio(self, audio_chunk: bytes) -> None:
        """Send a raw PCM 16bit 16kHz audio chunk to Deepgram."""
        if self._cancelled or not self._connected or self._connection is None:
            return
        try:
            from deepgram.extensions.types.sockets import ListenV1MediaMessage
            await self._connection.send_media(ListenV1MediaMessage(audio_chunk))
        except Exception as exc:
            logger.error("Error sending audio to Deepgram: %s", exc)

    async def stop_stream(self) -> str:
        """Close the Deepgram connection and return the final transcript."""
        if self._connection is None:
            return self._final_transcript

        try:
            await self._connection.finish()
            logger.info("Deepgram STT stream finished")
        except Exception as exc:
            logger.error("Error finishing Deepgram stream: %s", exc)
        finally:
            self._connected = False
            self._connection = None

        combined = " ".join(
            filter(None, [self._final_transcript] + self._transcript_parts)
        ).strip()
        self._final_transcript = combined
        return self._final_transcript

    def cancel(self) -> None:
        """Immediately cancel the current stream (barge-in support)."""
        logger.info("Deepgram STT stream cancelled (barge-in)")
        self._cancelled = True
        self._transcript_parts = []

    # ------------------------------------------------------------------
    # Internal event handlers
    # ------------------------------------------------------------------

    def _on_message(self, _connection, result, **_kwargs) -> None:
        """Handle incoming transcript messages from Deepgram v5 SDK."""
        try:
            sentence = result.channel.alternatives[0].transcript
        except (AttributeError, IndexError, TypeError):
            return

        if not sentence:
            return

        is_final: bool = getattr(result, "is_final", False)

        if is_final:
            self._final_transcript = " ".join(
                filter(None, [self._final_transcript, sentence])
            ).strip()
            self._transcript_parts = []
            logger.debug("Final transcript segment: %r", sentence)
        else:
            if self._transcript_parts:
                self._transcript_parts[-1] = sentence
            else:
                self._transcript_parts.append(sentence)
            logger.debug("Interim transcript: %r", sentence)

        if self.on_transcript is not None:
            try:
                self.on_transcript(sentence, is_final)
            except Exception as exc:
                logger.error("on_transcript callback raised: %s", exc)

    def _on_error(self, _connection, error, **_kwargs) -> None:
        """Handle Deepgram WebSocket errors."""
        logger.error("Deepgram STT error: %s", error)

    def _on_close(self, _connection, close_event, **_kwargs) -> None:
        """Handle Deepgram WebSocket close events."""
        self._connected = False
        logger.info("Deepgram STT connection closed: %s", close_event)


def create_stt() -> DeepgramSTT:
    """Factory that reads api_key from app settings."""
    settings = get_settings()
    return DeepgramSTT(api_key=settings.deepgram_api_key)
