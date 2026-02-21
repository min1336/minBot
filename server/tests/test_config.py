"""Tests for app.config — Settings loading and get_settings() caching."""

import os
from unittest.mock import patch

import pytest

from app.config import Settings, get_settings


class TestSettings:
    def test_defaults(self):
        """Settings have safe defaults for all fields."""
        s = Settings()
        assert s.deepgram_api_key == ""
        assert s.groq_api_key == ""
        assert s.elevenlabs_api_key == ""
        assert s.elevenlabs_voice_id == ""
        assert s.voice_clone_provider == "elevenlabs"
        assert s.server_host == "0.0.0.0"
        assert s.server_port == 8000
        assert s.jwt_algorithm == "HS256"
        assert s.jwt_expire_minutes == 1440
        assert s.bot_name == "미니"
        assert s.default_personality == "friendly"

    def test_loads_from_env_vars(self):
        """Settings reads values from environment variables."""
        env = {
            "DEEPGRAM_API_KEY": "dg-key-123",
            "GROQ_API_KEY": "groq-key-456",
            "ELEVENLABS_API_KEY": "el-key-789",
            "ELEVENLABS_VOICE_ID": "voice-abc",
            "BOT_NAME": "미봇",
            "SERVER_PORT": "9000",
            "JWT_EXPIRE_MINUTES": "30",
        }
        with patch.dict(os.environ, env, clear=False):
            s = Settings()

        assert s.deepgram_api_key == "dg-key-123"
        assert s.groq_api_key == "groq-key-456"
        assert s.elevenlabs_api_key == "el-key-789"
        assert s.elevenlabs_voice_id == "voice-abc"
        assert s.bot_name == "미봇"
        assert s.server_port == 9000
        assert s.jwt_expire_minutes == 30

    def test_voice_clone_provider_options(self):
        """voice_clone_provider accepts all three valid values."""
        for provider in ("elevenlabs", "xtts", "cosyvoice"):
            with patch.dict(os.environ, {"VOICE_CLONE_PROVIDER": provider}):
                s = Settings()
                assert s.voice_clone_provider == provider

    def test_xtts_and_cosyvoice_urls(self):
        """Self-hosted server URLs are configurable via env vars."""
        env = {
            "XTTS_SERVER_URL": "http://xtts.local:8001",
            "COSYVOICE_SERVER_URL": "http://cosy.local:8002",
        }
        with patch.dict(os.environ, env):
            s = Settings()
        assert s.xtts_server_url == "http://xtts.local:8001"
        assert s.cosyvoice_server_url == "http://cosy.local:8002"


class TestGetSettings:
    def test_returns_settings_instance(self, clear_settings_cache):
        """get_settings() returns a Settings object."""
        result = get_settings()
        assert isinstance(result, Settings)

    def test_is_cached(self, clear_settings_cache):
        """Calling get_settings() twice returns the same object (lru_cache)."""
        first = get_settings()
        second = get_settings()
        assert first is second

    def test_cache_cleared_by_fixture(self, clear_settings_cache):
        """After cache clear, a new instance is returned on next call."""
        first = get_settings()
        get_settings.cache_clear()
        second = get_settings()
        # Both are valid Settings; they are distinct objects after clearing
        assert isinstance(second, Settings)
        # They happen to have the same values but are not the same object
        assert first is not second
