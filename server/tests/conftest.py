"""Shared pytest fixtures for minBot server tests."""

import pytest
import pytest_asyncio
from unittest.mock import patch, MagicMock

import httpx
from fastapi import FastAPI
from fastapi.testclient import TestClient


# ---------------------------------------------------------------------------
# Settings override
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
    "admin_password": "test_admin_password",
    "jwt_secret_key": "test-jwt-secret-key-for-testing-only",
    "jwt_algorithm": "HS256",
    "jwt_expire_minutes": 60,
    "bot_name": "테스트봇",
    "default_personality": "friendly",
}


@pytest.fixture(autouse=True)
def clear_settings_cache():
    """Clear the lru_cache on get_settings() before each test."""
    from app.config import get_settings
    get_settings.cache_clear()
    yield
    get_settings.cache_clear()


@pytest.fixture
def mock_settings(clear_settings_cache):
    """Patch Settings so no real .env file or env vars are required."""
    from app.config import Settings
    instance = Settings(**MOCK_SETTINGS)
    with patch("app.config.get_settings", return_value=instance):
        yield instance


# ---------------------------------------------------------------------------
# Lightweight FastAPI app for API tests (avoids importing app.main)
# ---------------------------------------------------------------------------

@pytest.fixture
def app(mock_settings):
    """Lightweight FastAPI app with only the API router mounted.

    Avoids importing app.main which pulls in numpy, torch, deepgram etc.
    """
    from app.api.routes import router as api_router

    test_app = FastAPI()
    test_app.include_router(api_router, prefix="/api")

    @test_app.get("/")
    async def health_check():
        from app.config import get_settings
        settings = get_settings()
        return {"status": "ok", "bot_name": settings.bot_name}

    return test_app


@pytest.fixture
def client(app):
    """Synchronous HTTPX test client."""
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
    response = client.post("/api/auth/token", json={"password": "test_admin_password"})
    assert response.status_code == 200, f"Auth failed: {response.json()}"
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
    return b"\x00\x00" * 480


@pytest.fixture
def mock_wav_bytes():
    """Minimal valid WAV header + silent PCM payload."""
    import struct

    num_samples = 16000
    data_size = num_samples * 2
    header = struct.pack(
        "<4sI4s4sIHHIIHH4sI",
        b"RIFF",
        36 + data_size,
        b"WAVE",
        b"fmt ",
        16, 1, 1, 16000, 32000, 2, 16,
        b"data",
        data_size,
    )
    return header + b"\x00" * data_size
