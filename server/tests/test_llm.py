"""Tests for app.pipeline.llm — extract_emotion() and GroqLLM helpers."""

import pytest

from app.pipeline.llm import (
    SUPPORTED_EMOTIONS,
    extract_emotion,
    MAX_HISTORY_TURNS,
    GroqLLM,
)


class TestExtractEmotion:
    def test_extracts_happy_tag(self):
        """[EMOTION:happy] is stripped and emotion 'happy' is returned."""
        clean, emotion = extract_emotion("[EMOTION:happy] 안녕하세요!")
        assert emotion == "happy"
        assert clean == "안녕하세요!"

    def test_extracts_sad_tag(self):
        clean, emotion = extract_emotion("[EMOTION:sad] 그랬구나...")
        assert emotion == "sad"
        assert clean == "그랬구나..."

    def test_extracts_surprised_tag(self):
        clean, emotion = extract_emotion("[EMOTION:surprised] 헐 진짜?")
        assert emotion == "surprised"
        assert clean == "헐 진짜?"

    def test_case_insensitive_tag(self):
        """Tag matching is case-insensitive."""
        clean, emotion = extract_emotion("[EMOTION:HAPPY] text")
        assert emotion == "happy"
        assert "EMOTION" not in clean

    def test_returns_clean_text_without_tag(self):
        """The returned clean_text must not contain the [EMOTION:...] tag."""
        clean, _ = extract_emotion("[EMOTION:thinking] 잠깐 생각해볼게.")
        assert "[EMOTION:" not in clean
        assert "잠깐 생각해볼게." in clean

    def test_missing_tag_defaults_to_speaking(self):
        """When no tag is present, emotion defaults to 'speaking'."""
        clean, emotion = extract_emotion("그냥 일반 텍스트야.")
        assert emotion == "speaking"
        assert clean == "그냥 일반 텍스트야."

    def test_empty_string_defaults_to_speaking(self):
        clean, emotion = extract_emotion("")
        assert emotion == "speaking"
        assert clean == ""

    def test_whitespace_only_defaults_to_speaking(self):
        clean, emotion = extract_emotion("   ")
        assert emotion == "speaking"
        assert clean == ""

    def test_unknown_emotion_defaults_to_speaking(self):
        """Unrecognized emotion labels (not in SUPPORTED_EMOTIONS) map to 'speaking'."""
        clean, emotion = extract_emotion("[EMOTION:dancing] 춤추자!")
        assert emotion == "speaking"
        assert clean == "춤추자!"

    def test_all_supported_emotions_are_preserved(self):
        """Every emotion in SUPPORTED_EMOTIONS is returned as-is when valid."""
        for supported in SUPPORTED_EMOTIONS:
            _, emotion = extract_emotion(f"[EMOTION:{supported}] text")
            assert emotion == supported

    def test_multiple_tags_only_first_removed(self):
        """When multiple tags exist, only the first is extracted; the rest are also removed."""
        text = "[EMOTION:happy] 첫 번째. [EMOTION:sad] 두 번째."
        clean, emotion = extract_emotion(text)
        # First emotion wins
        assert emotion == "happy"
        # All tags are stripped by the global sub
        assert "[EMOTION:" not in clean

    def test_tag_with_surrounding_whitespace(self):
        """Extra whitespace around the tag does not affect extraction."""
        clean, emotion = extract_emotion("  [EMOTION:idle]  안녕  ")
        assert emotion == "idle"
        assert clean == "안녕"

    def test_text_only_tag_returns_empty_clean(self):
        """A message that is only the tag produces an empty clean_text."""
        clean, emotion = extract_emotion("[EMOTION:happy]")
        assert emotion == "happy"
        assert clean == ""


class TestGroqLLMHistory:
    """Unit tests for GroqLLM history management (no network calls)."""

    def _make_llm(self) -> GroqLLM:
        return GroqLLM(api_key="fake-key-for-unit-tests")

    def test_initial_history_empty(self):
        llm = self._make_llm()
        assert llm.history == []

    def test_clear_history(self):
        llm = self._make_llm()
        # Manually inject history entries
        llm._history = [
            {"role": "user", "content": "hello"},
            {"role": "assistant", "content": "hi"},
        ]
        llm.clear_history()
        assert llm.history == []

    def test_update_history_appends_pair(self):
        llm = self._make_llm()
        llm._update_history("질문이야", "대답이야")
        assert len(llm.history) == 2
        assert llm.history[0] == {"role": "user", "content": "질문이야"}
        assert llm.history[1] == {"role": "assistant", "content": "대답이야"}

    def test_history_trimmed_to_max_turns(self):
        """History is trimmed to MAX_HISTORY_TURNS pairs (= MAX_HISTORY_TURNS * 2 messages)."""
        llm = self._make_llm()
        # Add one more than the maximum
        for i in range(MAX_HISTORY_TURNS + 1):
            llm._update_history(f"user {i}", f"assistant {i}")

        assert len(llm.history) == MAX_HISTORY_TURNS * 2
        # The oldest entry should have been evicted (user 0 / assistant 0)
        assert llm.history[0]["content"] == "user 1"

    def test_history_property_returns_copy(self):
        """Mutating the returned history list does not affect internal state."""
        llm = self._make_llm()
        llm._update_history("q", "a")
        h = llm.history
        h.clear()
        assert len(llm.history) == 2  # internal state unchanged

    def test_cancel_sets_event(self):
        """cancel() sets the internal cancel event."""
        llm = self._make_llm()
        assert not llm._cancel_event.is_set()
        llm.cancel()
        assert llm._cancel_event.is_set()
