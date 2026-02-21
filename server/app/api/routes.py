import time
from pathlib import Path

from fastapi import APIRouter, Depends, HTTPException, Query, UploadFile, status

from app.api.auth import create_access_token, verify_token
from app.config import get_settings
from app.models.schemas import (
    PersonalityConfig,
    RobotStatus,
    VoiceProviderChange,
)

router = APIRouter()

# In-memory state for prototype
_personality_config = PersonalityConfig()
_start_time = time.time()
_conversation_log: list[dict] = []

VALID_PROVIDERS = {"elevenlabs", "xtts", "cosyvoice"}

# ---------------------------------------------------------------------------
# Auth
# ---------------------------------------------------------------------------

@router.post("/auth/token")
async def get_token(body: dict):
    """Issue JWT for hardcoded prototype password."""
    if body.get("password") != "minbot_secret":
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Invalid password",
        )
    token = create_access_token(subject="mobile_app")
    return {"access_token": token, "token_type": "bearer"}


# ---------------------------------------------------------------------------
# Voice Samples
# ---------------------------------------------------------------------------

@router.post("/voice-samples", status_code=status.HTTP_201_CREATED)
async def upload_voice_samples(
    files: list[UploadFile],
    _subject: str = Depends(verify_token),
):
    """Accept multipart audio files, save to data/voice_samples/, return count and total duration."""
    settings = get_settings()
    save_dir = Path("data/voice_samples")
    save_dir.mkdir(parents=True, exist_ok=True)

    uploaded = 0
    total_bytes = 0

    for file in files:
        content = await file.read()
        dest = save_dir / file.filename
        dest.write_bytes(content)
        uploaded += 1
        total_bytes += len(content)

    # Approximate duration: assume 16-bit PCM 16kHz mono -> 32000 bytes/sec
    total_duration = total_bytes / 32000.0

    return {"uploaded": uploaded, "total_duration": round(total_duration, 2)}


# ---------------------------------------------------------------------------
# Personality
# ---------------------------------------------------------------------------

@router.get("/personality", response_model=PersonalityConfig)
async def get_personality(_subject: str = Depends(verify_token)):
    """Return current personality configuration."""
    return _personality_config


@router.put("/personality", response_model=PersonalityConfig)
async def update_personality(
    config: PersonalityConfig,
    _subject: str = Depends(verify_token),
):
    """Replace personality configuration."""
    global _personality_config
    _personality_config = config
    return _personality_config


# ---------------------------------------------------------------------------
# Status
# ---------------------------------------------------------------------------

@router.get("/status", response_model=RobotStatus)
async def get_status(_subject: str = Depends(verify_token)):
    """Return current robot status."""
    uptime = int(time.time() - _start_time)
    return RobotStatus(uptime_seconds=uptime)


# ---------------------------------------------------------------------------
# Voice Provider
# ---------------------------------------------------------------------------

@router.post("/voice-provider")
async def change_voice_provider(
    body: VoiceProviderChange,
    _subject: str = Depends(verify_token),
):
    """Switch active voice clone provider."""
    if body.provider not in VALID_PROVIDERS:
        raise HTTPException(
            status_code=status.HTTP_422_UNPROCESSABLE_ENTITY,
            detail=f"provider must be one of {sorted(VALID_PROVIDERS)}",
        )
    settings = get_settings()
    settings.voice_clone_provider = body.provider
    return {"provider": body.provider, "status": "switched"}


# ---------------------------------------------------------------------------
# Conversations
# ---------------------------------------------------------------------------

@router.get("/conversations")
async def get_conversations(
    limit: int = Query(default=20, ge=1, le=100),
    _subject: str = Depends(verify_token),
):
    """Return recent conversation log entries."""
    return _conversation_log[-limit:]
