"""Tests for app.pipeline.tts — split_sentences() and ElevenLabsTTS helpers."""

import pytest

from app.pipeline.tts import split_sentences, ElevenLabsTTS


class TestSplitSentences:
    def test_splits_on_period(self):
        result = split_sentences("Hello world. How are you.")
        assert result == ["Hello world.", "How are you."]

    def test_splits_on_exclamation(self):
        result = split_sentences("Hello! World!")
        assert result == ["Hello!", "World!"]

    def test_splits_on_question_mark(self):
        result = split_sentences("How are you? I am fine.")
        assert result == ["How are you?", "I am fine."]

    def test_splits_mixed_punctuation(self):
        result = split_sentences("Hello world. How are you? Fine!")
        assert result == ["Hello world.", "How are you?", "Fine!"]

    def test_korean_text_single_sentence(self):
        """Korean text with no terminal punctuation returns the whole text."""
        result = split_sentences("안녕하세요")
        assert result == ["안녕하세요"]

    def test_korean_text_with_period(self):
        result = split_sentences("안녕하세요. 오늘 날씨가 좋네요.")
        assert result == ["안녕하세요.", "오늘 날씨가 좋네요."]

    def test_korean_text_with_exclamation(self):
        result = split_sentences("대박! 진짜야!")
        assert result == ["대박!", "진짜야!"]

    def test_korean_text_mixed(self):
        result = split_sentences("안녕! 오늘 어때? 나는 좋아.")
        assert result == ["안녕!", "오늘 어때?", "나는 좋아."]

    def test_empty_string_returns_empty_list(self):
        result = split_sentences("")
        assert result == []

    def test_whitespace_only_returns_empty_list(self):
        result = split_sentences("   ")
        assert result == []

    def test_text_without_punctuation_returns_whole_text(self):
        """Text with no sentence-ending punctuation is returned as a single-element list."""
        result = split_sentences("no punctuation here")
        assert result == ["no punctuation here"]

    def test_leading_trailing_whitespace_stripped(self):
        result = split_sentences("  Hello. World.  ")
        assert result[0] == "Hello."
        assert result[-1] == "World."

    def test_no_empty_strings_in_result(self):
        """Result never contains empty strings even with multiple spaces between sentences."""
        result = split_sentences("Hello.   World.")
        assert all(s for s in result)

    def test_single_sentence_with_period(self):
        result = split_sentences("단일 문장.")
        assert result == ["단일 문장."]

    def test_returns_list_type(self):
        result = split_sentences("Test.")
        assert isinstance(result, list)

    def test_preserves_punctuation_at_end(self):
        """Each sentence in the result ends with its original punctuation mark."""
        result = split_sentences("First. Second! Third?")
        assert result[0].endswith(".")
        assert result[1].endswith("!")
        assert result[2].endswith("?")


class TestElevenLabsTTSCancelFlag:
    """Unit tests for barge-in cancel flag (no network calls)."""

    def _make_tts(self) -> ElevenLabsTTS:
        return ElevenLabsTTS(api_key="fake-key", voice_id="fake-voice-id")

    def test_cancel_sets_flag(self):
        tts = self._make_tts()
        assert tts._cancelled is False
        tts.cancel()
        assert tts._cancelled is True

    def test_synthesize_stream_resets_flag(self):
        """synthesize_stream() resets _cancelled to False at the start of each call.

        We only test the flag reset here — actual HTTP is not invoked because
        we don't iterate the returned async generator.
        """
        tts = self._make_tts()
        tts._cancelled = True
        # Calling synthesize_stream() returns an async generator; the flag
        # is reset lazily when the generator is first advanced. Wrap and check.
        import asyncio

        async def _check():
            gen = tts.synthesize_stream("hello")
            # The generator hasn't started yet; _cancelled is still True from setup.
            # Prime it with a try/anext (will fail on network), but flag resets first.
            try:
                await gen.__anext__()
            except Exception:
                pass
            # After the first yield attempt, _cancelled should have been reset to False.
            assert tts._cancelled is False

        asyncio.run(_check())

    def test_model_id_constant(self):
        assert ElevenLabsTTS.MODEL_ID == "eleven_flash_v2_5"

    def test_output_format_constant(self):
        assert ElevenLabsTTS.OUTPUT_FORMAT == "mp3_44100_128"
