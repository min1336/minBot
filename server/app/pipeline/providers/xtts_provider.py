import logging
import io
from typing import AsyncIterator

import httpx

from app.config import get_settings
from app.pipeline.voice_clone import VoiceCloneProvider

logger = logging.getLogger(__name__)

_CLONE_ENDPOINT = "/clone_speaker"
_SYNTHESIZE_ENDPOINT = "/tts_stream"
_LIST_ENDPOINT = "/speakers"
_DELETE_ENDPOINT = "/delete_speaker"


class XTTSProvider(VoiceCloneProvider):
    """XTTS v2 self-hosted voice clone provider.

    Expects an XTTS server running at `xtts_server_url` that exposes:
      POST /clone_speaker  – uploads reference audio, returns {"speaker_id": ...}
      POST /tts_stream     – streams synthesized audio chunks
      GET  /speakers       – lists available speaker ids
      DELETE /delete_speaker/{speaker_id} – removes a speaker
    """

    def __init__(self) -> None:
        settings = get_settings()
        self._base_url = settings.xtts_server_url.rstrip("/")
        logger.info("XTTSProvider base_url=%s", self._base_url)

    async def clone_voice(self, audio_samples: list[bytes], name: str = "cloned") -> str:
        """Upload reference audio to XTTS server for zero-shot cloning.

        XTTS v2 requires at least ~3 seconds of clean audio for Korean.
        Returns the speaker_id assigned by the server.
        """
        if not audio_samples:
            raise ValueError("At least one audio sample is required for XTTS voice cloning")

        # Concatenate samples into a single WAV payload if multiple provided.
        reference_audio = audio_samples[0] if len(audio_samples) == 1 else b"".join(audio_samples)

        logger.info(
            "Uploading reference audio to XTTS server for speaker '%s' (%d bytes)",
            name,
            len(reference_audio),
        )

        async with httpx.AsyncClient(timeout=60.0) as client:
            try:
                response = await client.post(
                    f"{self._base_url}{_CLONE_ENDPOINT}",
                    files={"wav_file": (f"{name}.wav", io.BytesIO(reference_audio), "audio/wav")},
                    data={"speaker_name": name},
                )
                response.raise_for_status()
                data = response.json()
                speaker_id: str = data.get("speaker_id") or data.get("name") or name
                logger.info("XTTS clone success: speaker_id=%s", speaker_id)
                return speaker_id
            except httpx.HTTPStatusError as exc:
                logger.error("XTTS clone_voice HTTP error %s: %s", exc.response.status_code, exc.response.text)
                raise
            except Exception as exc:
                logger.error("XTTS clone_voice failed: %s", exc)
                raise

    async def synthesize(self, text: str, voice_id: str) -> AsyncIterator[bytes]:
        """Stream synthesized audio from XTTS server using the cloned speaker."""
        logger.info("XTTS synthesize: speaker_id=%s, text_len=%d", voice_id, len(text))

        payload = {
            "text": text,
            "speaker_name": voice_id,
            "language": "ko",
            "add_wav_header": False,
            "stream_chunk_size": 100,
        }

        try:
            async with httpx.AsyncClient(timeout=120.0) as client:
                async with client.stream("POST", f"{self._base_url}{_SYNTHESIZE_ENDPOINT}", json=payload) as response:
                    response.raise_for_status()
                    async for chunk in response.aiter_bytes(chunk_size=4096):
                        if chunk:
                            yield chunk
        except httpx.HTTPStatusError as exc:
            logger.error("XTTS synthesize HTTP error %s: %s", exc.response.status_code, exc.response.text)
            raise
        except Exception as exc:
            logger.error("XTTS synthesize failed: %s", exc)
            raise

    async def list_voices(self) -> list[dict]:
        """List available speakers on the XTTS server."""
        async with httpx.AsyncClient(timeout=10.0) as client:
            try:
                response = await client.get(f"{self._base_url}{_LIST_ENDPOINT}")
                response.raise_for_status()
                data = response.json()
                # Normalize to list[dict] regardless of server response shape.
                if isinstance(data, list):
                    voices = [
                        {"voice_id": item if isinstance(item, str) else item.get("speaker_id", str(item)), "name": item if isinstance(item, str) else item.get("name", str(item))}
                        for item in data
                    ]
                elif isinstance(data, dict):
                    voices = [{"voice_id": k, "name": k} for k in data.keys()]
                else:
                    voices = []
                logger.info("XTTS listed %d speakers", len(voices))
                return voices
            except Exception as exc:
                logger.error("XTTS list_voices failed: %s", exc)
                raise

    async def delete_voice(self, voice_id: str) -> bool:
        """Delete a speaker from the XTTS server."""
        async with httpx.AsyncClient(timeout=10.0) as client:
            try:
                response = await client.delete(f"{self._base_url}{_DELETE_ENDPOINT}/{voice_id}")
                response.raise_for_status()
                logger.info("XTTS deleted speaker: voice_id=%s", voice_id)
                return True
            except httpx.HTTPStatusError as exc:
                logger.error(
                    "XTTS delete_voice HTTP error %s for voice_id=%s: %s",
                    exc.response.status_code,
                    voice_id,
                    exc.response.text,
                )
                return False
            except Exception as exc:
                logger.error("XTTS delete_voice failed for voice_id=%s: %s", voice_id, exc)
                return False
