from typing import Dict, List, Optional, Union

from pydantic import BaseModel
from enum import Enum


class Emotion(str, Enum):
    IDLE = "idle"
    LISTENING = "listening"
    THINKING = "thinking"
    SPEAKING = "speaking"
    HAPPY = "happy"
    SAD = "sad"
    SURPRISED = "surprised"
    SLEEPING = "sleeping"
    TILTED = "tilted"


class WSMessageType(str, Enum):
    AUDIO = "audio"
    TEXT = "text"
    EMOTION = "emotion"
    BARGE_IN = "barge_in"
    CANCEL_PLAYBACK = "cancel_playback"
    STATUS = "status"


class WSMessage(BaseModel):
    type: WSMessageType
    data: Optional[Union[str, bytes, Dict]] = None
    emotion: Optional[Emotion] = None


class PersonalityConfig(BaseModel):
    bot_name: str = "미니"
    speech_patterns: List[str] = []
    sentence_endings: List[str] = []
    favorite_expressions: List[str] = []
    personality_traits: List[str] = ["친근한", "장난스러운", "다정한"]


class VoiceSample(BaseModel):
    filename: str
    duration_seconds: float
    sample_rate: int = 16000


class VoiceProviderChange(BaseModel):
    provider: str  # elevenlabs | xtts | cosyvoice


class RobotStatus(BaseModel):
    battery_percent: int = 100
    is_connected: bool = True
    current_emotion: Emotion = Emotion.IDLE
    wifi_rssi: int = -50
    uptime_seconds: int = 0


class TokenPayload(BaseModel):
    sub: str
    exp: int
