from abc import ABC, abstractmethod
from typing import AsyncIterator
import logging

logger = logging.getLogger(__name__)


class VoiceCloneProvider(ABC):
    @abstractmethod
    async def clone_voice(self, audio_samples: list[bytes], name: str = "cloned") -> str:
        """Clone a voice from audio samples. Returns voice_id."""
        ...

    @abstractmethod
    async def synthesize(self, text: str, voice_id: str) -> AsyncIterator[bytes]:
        """Synthesize speech with cloned voice. Yields audio chunks."""
        ...

    @abstractmethod
    async def list_voices(self) -> list[dict]:
        """List available cloned voices."""
        ...

    @abstractmethod
    async def delete_voice(self, voice_id: str) -> bool:
        """Delete a cloned voice."""
        ...


def get_voice_clone_provider(provider_name: str) -> VoiceCloneProvider:
    """Factory function to get provider by name."""
    name = provider_name.lower().strip()

    if name == "elevenlabs":
        from app.pipeline.providers.elevenlabs_provider import ElevenLabsProvider
        logger.info("Using ElevenLabs voice clone provider")
        return ElevenLabsProvider()

    if name == "xtts":
        from app.pipeline.providers.xtts_provider import XTTSProvider
        logger.info("Using XTTS v2 voice clone provider")
        return XTTSProvider()

    if name == "cosyvoice":
        from app.pipeline.providers.cosyvoice_provider import CosyVoiceProvider
        logger.info("Using CosyVoice 2 voice clone provider")
        return CosyVoiceProvider()

    raise ValueError(
        f"Unknown voice clone provider: '{provider_name}'. "
        "Supported providers: elevenlabs, xtts, cosyvoice"
    )
