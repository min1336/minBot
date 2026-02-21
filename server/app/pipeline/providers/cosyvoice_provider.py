import logging
import io
from typing import AsyncIterator

import httpx

from app.config import get_settings
from app.pipeline.voice_clone import VoiceCloneProvider

logger = logging.getLogger(__name__)

_CLONE_ENDPOINT = "/clone"
_SYNTHESIZE_ENDPOINT = "/inference_cross_lingual"
_LIST_ENDPOINT = "/speakers"
_DELETE_ENDPOINT = "/delete_speaker"


class CosyVoiceProvider(VoiceCloneProvider):
    """CosyVoice 2 (Alibaba) self-hosted voice clone provider.

    Uses zero-shot cross-lingual synthesis, which supports Korean without
    a separate training step – just ~3-10 seconds of reference audio.

    Expected server endpoints:
      POST /clone                       – register reference audio, returns {"speaker_id": ...}
      POST /inference_cross_lingual     – stream synthesized audio
      GET  /speakers                    – list registered speaker ids
      DELETE /delete_speaker/{id}       – remove a speaker
    """

    def __init__(self) -> None:
        settings = get_settings()
        self._base_url = settings.cosyvoice_server_url.rstrip("/")
        logger.info("CosyVoiceProvider base_url=%s", self._base_url)

    async def clone_voice(self, audio_samples: list[bytes], name: str = "cloned") -> str:
        """Register reference audio for zero-shot cloning on the CosyVoice 2 server.

        CosyVoice 2 supports cross-lingual zero-shot synthesis, meaning
        a Korean voice can be cloned from a Korean reference utterance
        without any fine-tuning. Returns the speaker_id.
        """
        if not audio_samples:
            raise ValueError("At least one audio sample is required for CosyVoice cloning")

        reference_audio = audio_samples[0] if len(audio_samples) == 1 else b"".join(audio_samples)

        logger.info(
            "Uploading reference audio to CosyVoice server for speaker '%s' (%d bytes)",
            name,
            len(reference_audio),
        )

        async with httpx.AsyncClient(timeout=60.0) as client:
            try:
                response = await client.post(
                    f"{self._base_url}{_CLONE_ENDPOINT}",
                    files={
                        "reference_audio": (f"{name}.wav", io.BytesIO(reference_audio), "audio/wav"),
                    },
                    data={"speaker_id": name},
                )
                response.raise_for_status()
                data = response.json()
                speaker_id: str = data.get("speaker_id") or name
                logger.info("CosyVoice clone success: speaker_id=%s", speaker_id)
                return speaker_id
            except httpx.HTTPStatusError as exc:
                logger.error(
                    "CosyVoice clone_voice HTTP error %s: %s",
                    exc.response.status_code,
                    exc.response.text,
                )
                raise
            except Exception as exc:
                logger.error("CosyVoice clone_voice failed: %s", exc)
                raise

    async def synthesize(self, text: str, voice_id: str) -> AsyncIterator[bytes]:
        """Stream cross-lingual synthesized audio from CosyVoice 2 server.

        Uses the registered speaker's voice characteristics for Korean output.
        """
        logger.info("CosyVoice synthesize: speaker_id=%s, text_len=%d", voice_id, len(text))

        payload = {
            "tts_text": text,
            "speaker_id": voice_id,
            "language": "ko",
            "speed": 1.0,
            "stream": True,
        }

        try:
            async with httpx.AsyncClient(timeout=120.0) as client:
                async with client.stream(
                    "POST",
                    f"{self._base_url}{_SYNTHESIZE_ENDPOINT}",
                    json=payload,
                ) as response:
                    response.raise_for_status()
                    async for chunk in response.aiter_bytes(chunk_size=4096):
                        if chunk:
                            yield chunk
        except httpx.HTTPStatusError as exc:
            logger.error(
                "CosyVoice synthesize HTTP error %s: %s",
                exc.response.status_code,
                exc.response.text,
            )
            raise
        except Exception as exc:
            logger.error("CosyVoice synthesize failed: %s", exc)
            raise

    async def list_voices(self) -> list[dict]:
        """List registered speakers on the CosyVoice server."""
        async with httpx.AsyncClient(timeout=10.0) as client:
            try:
                response = await client.get(f"{self._base_url}{_LIST_ENDPOINT}")
                response.raise_for_status()
                data = response.json()
                if isinstance(data, list):
                    voices = [
                        {
                            "voice_id": item if isinstance(item, str) else item.get("speaker_id", str(item)),
                            "name": item if isinstance(item, str) else item.get("name", str(item)),
                        }
                        for item in data
                    ]
                elif isinstance(data, dict):
                    voices = [{"voice_id": k, "name": k} for k in data.keys()]
                else:
                    voices = []
                logger.info("CosyVoice listed %d speakers", len(voices))
                return voices
            except Exception as exc:
                logger.error("CosyVoice list_voices failed: %s", exc)
                raise

    async def delete_voice(self, voice_id: str) -> bool:
        """Delete a registered speaker from the CosyVoice server."""
        async with httpx.AsyncClient(timeout=10.0) as client:
            try:
                response = await client.delete(f"{self._base_url}{_DELETE_ENDPOINT}/{voice_id}")
                response.raise_for_status()
                logger.info("CosyVoice deleted speaker: voice_id=%s", voice_id)
                return True
            except httpx.HTTPStatusError as exc:
                logger.error(
                    "CosyVoice delete_voice HTTP error %s for voice_id=%s: %s",
                    exc.response.status_code,
                    voice_id,
                    exc.response.text,
                )
                return False
            except Exception as exc:
                logger.error("CosyVoice delete_voice failed for voice_id=%s: %s", voice_id, exc)
                return False
