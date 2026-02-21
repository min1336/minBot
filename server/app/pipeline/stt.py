import asyncio
import logging
from typing import Callable, Optional

from deepgram import (
    DeepgramClient,
    DeepgramClientOptions,
    LiveTranscriptionEvents,
    LiveOptions,
)

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

        self._client: Optional[DeepgramClient] = None
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

        options = DeepgramClientOptions(api_key=self._api_key)
        self._client = DeepgramClient("", options)

        try:
            self._connection = self._client.listen.asynclive.v("1")

            self._connection.on(LiveTranscriptionEvents.Transcript, self._on_transcript)
            self._connection.on(LiveTranscriptionEvents.Error, self._on_error)
            self._connection.on(LiveTranscriptionEvents.Close, self._on_close)

            live_options = LiveOptions(
                model="nova-3",
                language="ko",
                smart_format=True,
                interim_results=True,
                endpointing=300,
                encoding="linear16",
                sample_rate=16000,
                channels=1,
            )

            started = await self._connection.start(live_options)
            if not started:
                raise RuntimeError("Deepgram connection failed to start")

            self._connected = True
            logger.info("Deepgram STT stream started (nova-3, ko, 16kHz)")

        except Exception as exc:
            logger.error("Failed to start Deepgram stream: %s", exc)
            self._connected = False
            raise

    async def send_audio(self, audio_chunk: bytes) -> None:
        """Send a raw PCM 16bit 16kHz audio chunk to Deepgram.

        Args:
            audio_chunk: Raw PCM bytes (16-bit little-endian, 16 kHz, mono).
        """
        if self._cancelled or not self._connected or self._connection is None:
            return
        try:
            await self._connection.send(audio_chunk)
        except Exception as exc:
            logger.error("Error sending audio to Deepgram: %s", exc)

    async def stop_stream(self) -> str:
        """Flush pending audio, close the Deepgram connection, and return the final transcript.

        Returns:
            The accumulated final transcript text.
        """
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

        # Combine any leftover interim parts with confirmed finals
        combined = " ".join(filter(None, [self._final_transcript] + self._transcript_parts)).strip()
        self._final_transcript = combined
        return self._final_transcript

    def cancel(self) -> None:
        """Immediately cancel the current stream (barge-in support).

        The connection will be closed on the next send attempt or when
        stop_stream() is called. For an instant teardown, schedule
        stop_stream() as a task after calling cancel().
        """
        logger.info("Deepgram STT stream cancelled (barge-in)")
        self._cancelled = True
        self._transcript_parts = []

    # ------------------------------------------------------------------
    # Internal event handlers
    # ------------------------------------------------------------------

    async def _on_transcript(self, _client, result, **_kwargs) -> None:
        """Handle incoming transcript events from Deepgram."""
        try:
            sentence = result.channel.alternatives[0].transcript
        except (AttributeError, IndexError):
            return

        if not sentence:
            return

        is_final: bool = result.is_final

        if is_final:
            # Append to confirmed transcript and clear interim buffer
            self._final_transcript = " ".join(
                filter(None, [self._final_transcript, sentence])
            ).strip()
            self._transcript_parts = []
            logger.debug("Final transcript segment: %r", sentence)
        else:
            # Keep the latest interim result for this utterance
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

    async def _on_error(self, _client, error, **_kwargs) -> None:
        """Handle Deepgram WebSocket errors."""
        logger.error("Deepgram STT error: %s", error)

    async def _on_close(self, _client, close, **_kwargs) -> None:
        """Handle Deepgram WebSocket close events."""
        self._connected = False
        logger.info("Deepgram STT connection closed: %s", close)
