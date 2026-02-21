"""Shared pytest fixtures for minBot server tests."""

import pytest
import pytest_asyncio
from unittest.mock import patch

import httpx
from fastapi.testclient import TestClient


# ---------------------------------------------------------------------------
# Settings override — must be applied before the app imports get_settings()
# ---------------------------------------------------------------------------

MOCK_SETTINGS = {
    "deepgram_api_key": "test-deepgram-key",
    "groq_api_key": "test-groq-key",
    "elevenlabs_api_key": "test-elevenlabs-key",
    "elevenlabs_voice_id": "test-voice-id",
    "voice_clone_provider": "elevenlabs",
    "xtts_server_url": "http://localhost:8001",
    "cosyvoice_server_url": "http://localhost:8002",
    "server_host": "0.0.0.0",
    "server_port": 8000,
    "jwt_secret_key": "test-jwt-secret-key-for-testing-only",
    "jwt_algorithm": "HS256",
    "jwt_expire_minutes": 60,
    "bot_name": "테스트봇",
    "default_personality": "friendly",
}


@pytest.fixture(autouse=True)
def clear_settings_cache():
    """Clear the lru_cache on get_settings() before each test to allow env var mocking."""
    from app.config import get_settings
    get_settings.cache_clear()
    yield
    get_settings.cache_clear()


@pytest.fixture
def mock_settings(clear_settings_cache):
    """Patch Settings so no real .env file or env vars are required."""
    with patch("app.config.Settings") as MockSettings:
        from app.config import Settings
        instance = Settings(**MOCK_SETTINGS)
        MockSettings.return_value = instance
        # Also patch wherever get_settings is imported directly
        with patch("app.config.get_settings", return_value=instance):
            yield instance


# ---------------------------------------------------------------------------
# FastAPI app fixture (synchronous TestClient)
# ---------------------------------------------------------------------------

@pytest.fixture
def app():
    """Return the FastAPI application with lifespan disabled for testing."""
    # Patch heavy lifespan side-effects (Silero VAD load, file I/O)
    with (
        patch("app.main._load_silero_vad", return_value=False),
        patch("app.personality.speech_learner.SpeechLearner.load_patterns"),
        patch("app.personality.speech_learner.SpeechLearner.save_patterns"),
    ):
        from app.main import app as _app
        yield _app


@pytest.fixture
def client(app):
    """Synchronous HTTPX test client via FastAPI TestClient."""
    with TestClient(app, raise_server_exceptions=True) as c:
        yield c


# ---------------------------------------------------------------------------
# Async client fixture
# ---------------------------------------------------------------------------

@pytest_asyncio.fixture
async def async_client(app):
    """Async HTTPX client using ASGITransport."""
    transport = httpx.ASGITransport(app=app)
    async with httpx.AsyncClient(transport=transport, base_url="http://test") as ac:
        yield ac


# ---------------------------------------------------------------------------
# Auth helpers
# ---------------------------------------------------------------------------

@pytest.fixture
def auth_token(client):
    """Obtain a valid JWT by posting the correct password."""
    response = client.post("/api/auth/token", json={"password": "minbot_secret"})
    assert response.status_code == 200
    return response.json()["access_token"]


@pytest.fixture
def auth_headers(auth_token):
    """Authorization header dict ready for use with the test client."""
    return {"Authorization": f"Bearer {auth_token}"}


# ---------------------------------------------------------------------------
# Mock audio data
# ---------------------------------------------------------------------------

@pytest.fixture
def mock_pcm_audio():
    """Minimal 30 ms silent PCM 16-bit 16 kHz mono frame (960 bytes)."""
    return b"\x00\x00" * 480  # 480 samples × 2 bytes = 960 bytes


@pytest.fixture
def mock_wav_bytes():
    """Minimal valid WAV header + silent PCM payload (useful for voice clone tests)."""
    import struct

    num_samples = 16000  # 1 second at 16 kHz
    data_size = num_samples * 2  # 16-bit
    header = struct.pack(
        "<4sI4s4sIHHIIHH4sI",
        b"RIFF",
        36 + data_size,   # chunk size
        b"WAVE",
        b"fmt ",
        16,               # subchunk1 size (PCM)
        1,                # audio format (PCM)
        1,                # num channels (mono)
        16000,            # sample rate
        32000,            # byte rate
        2,                # block align
        16,               # bits per sample
        b"data",
        data_size,
    )
    return header + b"\x00" * data_size
