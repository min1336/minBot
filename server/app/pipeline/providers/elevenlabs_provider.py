import logging
import io
from typing import AsyncIterator

from app.config import get_settings
from app.pipeline.voice_clone import VoiceCloneProvider

logger = logging.getLogger(__name__)


class ElevenLabsProvider(VoiceCloneProvider):
    """ElevenLabs Instant Voice Cloning provider using Flash v2.5."""

    def __init__(self) -> None:
        settings = get_settings()
        self._api_key = settings.elevenlabs_api_key
        if not self._api_key:
            logger.warning("ElevenLabs API key is not set")

    def _get_client(self):
        from elevenlabs.client import ElevenLabs
        return ElevenLabs(api_key=self._api_key)

    async def clone_voice(self, audio_samples: list[bytes], name: str = "cloned") -> str:
        """Upload audio samples and create an Instant Voice Clone. Returns voice_id."""
        import asyncio

        if not audio_samples:
            raise ValueError("At least one audio sample is required for voice cloning")

        logger.info("Cloning voice '%s' with %d sample(s) via ElevenLabs IVC", name, len(audio_samples))

        samples_snapshot = list(audio_samples)

        def _do_clone() -> str:
            client = self._get_client()
            voice = client.clone(
                name=name,
                files=[io.BytesIO(s) for s in samples_snapshot],
            )
            return voice.voice_id

        try:
            voice_id = await asyncio.get_running_loop().run_in_executor(None, _do_clone)
            logger.info("Voice cloned successfully: voice_id=%s", voice_id)
            return voice_id
        except Exception as exc:
            logger.error("ElevenLabs clone_voice failed: %s", exc)
            raise

    async def synthesize(self, text: str, voice_id: str) -> AsyncIterator[bytes]:
        """Stream synthesized audio using the cloned voice (Flash v2.5)."""
        import asyncio

        logger.info("Synthesizing with ElevenLabs Flash v2.5, voice_id=%s, text_len=%d", voice_id, len(text))

        def _stream_chunks():
            client = self._get_client()
            return client.generate(
                text=text,
                voice=voice_id,
                model="eleven_flash_v2_5",
                stream=True,
                output_format="pcm_16000",
            )

        try:
            loop = asyncio.get_running_loop()
            stream = await loop.run_in_executor(None, _stream_chunks)
            for chunk in stream:
                if chunk:
                    yield chunk
        except Exception as exc:
            logger.error("ElevenLabs synthesize failed: %s", exc)
            raise

    async def list_voices(self) -> list[dict]:
        """List all available cloned voices."""
        import asyncio

        def _list():
            client = self._get_client()
            response = client.voices.get_all()
            return [
                {
                    "voice_id": v.voice_id,
                    "name": v.name,
                    "category": getattr(v, "category", None),
                    "description": getattr(v, "description", None),
                }
                for v in response.voices
            ]

        try:
            voices = await asyncio.get_running_loop().run_in_executor(None, _list)
            logger.info("Listed %d ElevenLabs voices", len(voices))
            return voices
        except Exception as exc:
            logger.error("ElevenLabs list_voices failed: %s", exc)
            raise

    async def delete_voice(self, voice_id: str) -> bool:
        """Delete a cloned voice by voice_id."""
        import asyncio

        def _delete():
            client = self._get_client()
            client.voices.delete(voice_id)
            return True

        try:
            result = await asyncio.get_running_loop().run_in_executor(None, _delete)
            logger.info("Deleted ElevenLabs voice: voice_id=%s", voice_id)
            return result
        except Exception as exc:
            logger.error("ElevenLabs delete_voice failed for voice_id=%s: %s", voice_id, exc)
            return False
