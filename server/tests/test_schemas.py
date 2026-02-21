"""Tests for app.models.schemas — Pydantic models and enums."""

import pytest

from app.models.schemas import (
    Emotion,
    PersonalityConfig,
    RobotStatus,
    TokenPayload,
    VoiceProviderChange,
    VoiceSample,
    WSMessage,
    WSMessageType,
)


class TestEmotion:
    def test_all_values_present(self):
        """Emotion enum exposes exactly the expected set of values."""
        expected = {
            "idle", "listening", "thinking", "speaking",
            "happy", "sad", "surprised", "sleeping", "tilted",
        }
        actual = {e.value for e in Emotion}
        assert actual == expected

    def test_string_subclass(self):
        """Emotion is a str enum — members compare equal to plain strings."""
        assert Emotion.HAPPY == "happy"
        assert Emotion.SAD == "sad"
        assert Emotion.IDLE == "idle"

    def test_member_access_by_value(self):
        """Emotion members are accessible via Emotion('value')."""
        assert Emotion("happy") is Emotion.HAPPY
        assert Emotion("sleeping") is Emotion.SLEEPING

    def test_invalid_value_raises(self):
        with pytest.raises(ValueError):
            Emotion("dancing")


class TestWSMessageType:
    def test_all_values_present(self):
        expected = {"audio", "text", "emotion", "barge_in", "cancel_playback", "status"}
        assert {m.value for m in WSMessageType} == expected


class TestWSMessage:
    def test_minimal_message(self):
        """WSMessage requires only 'type'; data and emotion default to None."""
        msg = WSMessage(type=WSMessageType.TEXT)
        assert msg.type == WSMessageType.TEXT
        assert msg.data is None
        assert msg.emotion is None

    def test_with_data_string(self):
        msg = WSMessage(type=WSMessageType.STATUS, data="connected")
        assert msg.data == "connected"

    def test_with_dict_data(self):
        msg = WSMessage(type=WSMessageType.STATUS, data={"key": "value"})
        assert msg.data == {"key": "value"}

    def test_with_emotion(self):
        msg = WSMessage(type=WSMessageType.EMOTION, emotion=Emotion.HAPPY)
        assert msg.emotion == Emotion.HAPPY

    def test_serialization_roundtrip(self):
        """model_dump / model_validate roundtrip preserves values."""
        msg = WSMessage(type=WSMessageType.AUDIO, data=b"\x00\x01", emotion=Emotion.SPEAKING)
        dumped = msg.model_dump()
        restored = WSMessage.model_validate(dumped)
        assert restored.type == msg.type
        assert restored.emotion == msg.emotion


class TestPersonalityConfig:
    def test_defaults(self):
        """PersonalityConfig defaults match documented values."""
        config = PersonalityConfig()
        assert config.bot_name == "미니"
        assert config.speech_patterns == []
        assert config.sentence_endings == []
        assert config.favorite_expressions == []
        assert config.personality_traits == ["친근한", "장난스러운", "다정한"]

    def test_custom_values(self):
        config = PersonalityConfig(
            bot_name="봇봇",
            speech_patterns=["~요", "~다"],
            sentence_endings=["요", "다"],
            favorite_expressions=["대박", "헐"],
            personality_traits=["활발한"],
        )
        assert config.bot_name == "봇봇"
        assert "대박" in config.favorite_expressions
        assert config.personality_traits == ["활발한"]

    def test_serialization(self):
        config = PersonalityConfig(bot_name="미니")
        data = config.model_dump()
        assert data["bot_name"] == "미니"
        assert isinstance(data["speech_patterns"], list)


class TestRobotStatus:
    def test_defaults(self):
        """RobotStatus defaults represent a freshly started, idle robot."""
        status = RobotStatus()
        assert status.battery_percent == 100
        assert status.is_connected is True
        assert status.current_emotion == Emotion.IDLE
        assert status.wifi_rssi == -50
        assert status.uptime_seconds == 0

    def test_custom_values(self):
        status = RobotStatus(
            battery_percent=42,
            is_connected=False,
            current_emotion=Emotion.HAPPY,
            wifi_rssi=-70,
            uptime_seconds=3600,
        )
        assert status.battery_percent == 42
        assert status.current_emotion == Emotion.HAPPY
        assert status.uptime_seconds == 3600

    def test_serialization(self):
        status = RobotStatus(current_emotion=Emotion.SLEEPING)
        data = status.model_dump()
        assert data["current_emotion"] == "sleeping"


class TestVoiceSample:
    def test_required_fields(self):
        sample = VoiceSample(filename="test.wav", duration_seconds=3.5)
        assert sample.filename == "test.wav"
        assert sample.duration_seconds == 3.5
        assert sample.sample_rate == 16000  # default

    def test_custom_sample_rate(self):
        sample = VoiceSample(filename="hq.wav", duration_seconds=5.0, sample_rate=44100)
        assert sample.sample_rate == 44100


class TestVoiceProviderChange:
    def test_valid_provider(self):
        vpc = VoiceProviderChange(provider="xtts")
        assert vpc.provider == "xtts"

    def test_serialization(self):
        vpc = VoiceProviderChange(provider="cosyvoice")
        assert vpc.model_dump() == {"provider": "cosyvoice"}


class TestTokenPayload:
    def test_fields(self):
        payload = TokenPayload(sub="mobile_app", exp=9999999999)
        assert payload.sub == "mobile_app"
        assert payload.exp == 9999999999
