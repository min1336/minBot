"""Tests for app.pipeline.voice_clone — factory and provider types."""

import pytest
from unittest.mock import patch, MagicMock


from app.pipeline.voice_clone import VoiceCloneProvider, get_voice_clone_provider


class TestGetVoiceCloneProvider:
    def test_elevenlabs_returns_correct_type(self):
        """'elevenlabs' provider name returns an ElevenLabsProvider instance."""
        # Patch Settings so ElevenLabsProvider.__init__ doesn't need a real API key
        with patch("app.config.Settings") as MockSettings:
            MockSettings.return_value = MagicMock(
                elevenlabs_api_key="fake-key",
                elevenlabs_voice_id="fake-id",
                xtts_server_url="http://localhost:8001",
                cosyvoice_server_url="http://localhost:8002",
            )
            from app.config import get_settings
            get_settings.cache_clear()
            get_settings.cache_clear()

            with patch("app.config.get_settings", return_value=MockSettings.return_value):
                from app.pipeline.providers.elevenlabs_provider import ElevenLabsProvider
                provider = get_voice_clone_provider("elevenlabs")
                assert isinstance(provider, ElevenLabsProvider)

    def test_xtts_returns_correct_type(self):
        """'xtts' provider name returns an XTTSProvider instance."""
        mock_settings = MagicMock(
            xtts_server_url="http://localhost:8001",
            cosyvoice_server_url="http://localhost:8002",
            elevenlabs_api_key="",
        )
        with patch("app.config.get_settings", return_value=mock_settings):
            from app.pipeline.providers.xtts_provider import XTTSProvider
            provider = get_voice_clone_provider("xtts")
            assert isinstance(provider, XTTSProvider)

    def test_cosyvoice_returns_correct_type(self):
        """'cosyvoice' provider name returns a CosyVoiceProvider instance."""
        mock_settings = MagicMock(
            cosyvoice_server_url="http://localhost:8002",
            xtts_server_url="http://localhost:8001",
            elevenlabs_api_key="",
        )
        with patch("app.config.get_settings", return_value=mock_settings):
            from app.pipeline.providers.cosyvoice_provider import CosyVoiceProvider
            provider = get_voice_clone_provider("cosyvoice")
            assert isinstance(provider, CosyVoiceProvider)

    def test_invalid_provider_raises_value_error(self):
        """An unrecognized provider name raises ValueError with a helpful message."""
        with pytest.raises(ValueError) as exc_info:
            get_voice_clone_provider("nonexistent_provider")
        assert "nonexistent_provider" in str(exc_info.value)

    def test_invalid_provider_error_lists_supported(self):
        """The ValueError message mentions the supported provider names."""
        with pytest.raises(ValueError) as exc_info:
            get_voice_clone_provider("bad")
        message = str(exc_info.value)
        assert "elevenlabs" in message
        assert "xtts" in message
        assert "cosyvoice" in message

    def test_case_insensitive_elevenlabs(self):
        """Provider name lookup is case-insensitive."""
        mock_settings = MagicMock(
            elevenlabs_api_key="fake",
            xtts_server_url="http://localhost:8001",
            cosyvoice_server_url="http://localhost:8002",
        )
        with patch("app.config.get_settings", return_value=mock_settings):
            from app.pipeline.providers.elevenlabs_provider import ElevenLabsProvider
            provider = get_voice_clone_provider("ElevenLabs")
            assert isinstance(provider, ElevenLabsProvider)

    def test_case_insensitive_xtts(self):
        mock_settings = MagicMock(
            xtts_server_url="http://localhost:8001",
            cosyvoice_server_url="http://localhost:8002",
            elevenlabs_api_key="",
        )
        with patch("app.config.get_settings", return_value=mock_settings):
            from app.pipeline.providers.xtts_provider import XTTSProvider
            provider = get_voice_clone_provider("XTTS")
            assert isinstance(provider, XTTSProvider)

    def test_whitespace_trimmed_in_provider_name(self):
        """Leading/trailing whitespace in the provider name is stripped."""
        mock_settings = MagicMock(
            xtts_server_url="http://localhost:8001",
            cosyvoice_server_url="http://localhost:8002",
            elevenlabs_api_key="",
        )
        with patch("app.config.get_settings", return_value=mock_settings):
            from app.pipeline.providers.xtts_provider import XTTSProvider
            provider = get_voice_clone_provider("  xtts  ")
            assert isinstance(provider, XTTSProvider)

    def test_all_providers_are_subclasses_of_base(self):
        """Every returned provider is a subclass of VoiceCloneProvider (ABC)."""
        mock_settings = MagicMock(
            elevenlabs_api_key="fake",
            xtts_server_url="http://localhost:8001",
            cosyvoice_server_url="http://localhost:8002",
        )
        with patch("app.config.get_settings", return_value=mock_settings):
            for name in ("elevenlabs", "xtts", "cosyvoice"):
                provider = get_voice_clone_provider(name)
                assert isinstance(provider, VoiceCloneProvider), (
                    f"{name} provider is not a VoiceCloneProvider subclass"
                )

    def test_empty_string_raises_value_error(self):
        with pytest.raises(ValueError):
            get_voice_clone_provider("")

    def test_numeric_string_raises_value_error(self):
        with pytest.raises(ValueError):
            get_voice_clone_provider("123")


class TestVoiceCloneProviderABC:
    def test_cannot_instantiate_abstract_base(self):
        """VoiceCloneProvider is abstract and cannot be instantiated directly."""
        with pytest.raises(TypeError):
            VoiceCloneProvider()  # type: ignore[abstract]

    def test_abstract_methods_defined(self):
        """VoiceCloneProvider declares the four expected abstract methods."""
        abstract_methods = getattr(VoiceCloneProvider, "__abstractmethods__", set())
        expected = {"clone_voice", "synthesize", "list_voices", "delete_voice"}
        assert expected == abstract_methods
