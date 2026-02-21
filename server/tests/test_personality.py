"""Tests for app.personality — PromptBuilder and SpeechLearner."""

import pytest
import pytest_asyncio

from app.models.schemas import Emotion, PersonalityConfig
from app.personality.prompt_builder import PromptBuilder
from app.personality.speech_learner import SpeechLearner


# ---------------------------------------------------------------------------
# PromptBuilder
# ---------------------------------------------------------------------------

class TestPromptBuilder:
    def test_build_system_prompt_contains_bot_name(self):
        """build_system_prompt() includes the configured bot name."""
        builder = PromptBuilder(bot_name="미니")
        prompt = builder.build_system_prompt()
        assert "미니" in prompt

    def test_build_system_prompt_uses_personality_config_name(self):
        """When PersonalityConfig is passed, its bot_name overrides the builder default."""
        builder = PromptBuilder(bot_name="기본봇")
        config = PersonalityConfig(bot_name="커스텀봇")
        prompt = builder.build_system_prompt(personality_config=config)
        assert "커스텀봇" in prompt

    def test_build_system_prompt_without_config(self):
        """build_system_prompt() works without a PersonalityConfig argument."""
        builder = PromptBuilder(bot_name="미니")
        prompt = builder.build_system_prompt()
        assert isinstance(prompt, str)
        assert len(prompt) > 0

    def test_build_system_prompt_includes_emotion_instruction(self):
        """The generated prompt always includes emotion tagging instructions."""
        builder = PromptBuilder(bot_name="미니")
        prompt = builder.build_system_prompt()
        assert "EMOTION" in prompt

    def test_build_system_prompt_includes_personality_traits(self):
        """Custom personality_traits appear in the prompt."""
        builder = PromptBuilder(bot_name="미니")
        config = PersonalityConfig(personality_traits=["용감한", "호기심 많은"])
        prompt = builder.build_system_prompt(personality_config=config)
        assert "용감한" in prompt
        assert "호기심 많은" in prompt

    def test_build_emotion_instruction_lists_all_emotions(self):
        """build_emotion_instruction() includes every Emotion enum value."""
        builder = PromptBuilder()
        instruction = builder.build_emotion_instruction()
        for emotion in Emotion:
            assert emotion.value in instruction, f"Missing emotion: {emotion.value}"

    def test_build_emotion_instruction_format(self):
        """Emotion instruction contains [EMOTION:xxx] style lines."""
        builder = PromptBuilder()
        instruction = builder.build_emotion_instruction()
        assert "[EMOTION:" in instruction

    def test_set_speech_patterns_integrates_into_prompt(self):
        """set_speech_patterns() data appears in the system prompt when no config overrides."""
        builder = PromptBuilder(bot_name="미니")
        patterns = {
            "sentence_endings": [("~요", 10), ("~다", 5)],
            "expressions": [("대박", 8)],
            "filler_words": [("음", 3)],
        }
        builder.set_speech_patterns(patterns)
        prompt = builder.build_system_prompt()
        # Patterns section should be injected
        assert "요" in prompt or "대박" in prompt

    def test_set_speech_patterns_config_takes_precedence(self):
        """PersonalityConfig.sentence_endings overrides learned patterns from set_speech_patterns()."""
        builder = PromptBuilder(bot_name="미니")
        builder.set_speech_patterns({
            "sentence_endings": [("~요", 99)],
        })
        config = PersonalityConfig(sentence_endings=["~지", "~잖아"])
        prompt = builder.build_system_prompt(personality_config=config)
        assert "지" in prompt or "잖아" in prompt

    def test_set_speech_patterns_stores_patterns(self):
        """set_speech_patterns() stores the given dict on the builder."""
        builder = PromptBuilder()
        patterns = {"sentence_endings": [("~요", 1)]}
        builder.set_speech_patterns(patterns)
        assert builder.speech_patterns == patterns

    def test_empty_speech_patterns_produces_valid_prompt(self):
        """An empty patterns dict does not break prompt generation."""
        builder = PromptBuilder(bot_name="미니")
        builder.set_speech_patterns({})
        prompt = builder.build_system_prompt()
        assert len(prompt) > 0

    def test_default_bot_name(self):
        """PromptBuilder defaults to '미니' when no bot_name supplied."""
        builder = PromptBuilder()
        assert builder.bot_name == "미니"


# ---------------------------------------------------------------------------
# SpeechLearner
# ---------------------------------------------------------------------------

class TestSpeechLearner:
    @pytest.mark.asyncio
    async def test_learn_from_text_extracts_korean_endings(self):
        """learn_from_text() populates sentence_endings for Korean ~요 / ~다 / etc."""
        learner = SpeechLearner()
        await learner.learn_from_text("오늘 날씨가 좋아요. 기분도 좋다.")
        endings = dict(learner.patterns["sentence_endings"])
        # At least one ending should have been detected
        assert len(endings) > 0

    @pytest.mark.asyncio
    async def test_learn_from_text_empty_string_no_error(self):
        """learn_from_text() handles empty string gracefully."""
        learner = SpeechLearner()
        await learner.learn_from_text("")
        assert learner.patterns["sentence_endings"] == {}

    @pytest.mark.asyncio
    async def test_learn_from_text_accumulates_counts(self):
        """Repeated patterns increment their counter."""
        learner = SpeechLearner()
        await learner.learn_from_text("오늘 좋아요. 내일도 좋아요.")
        endings = dict(learner.patterns["sentence_endings"])
        # '좋아요' ending should appear at least twice (one per sentence)
        total_count = sum(endings.values())
        assert total_count >= 2

    @pytest.mark.asyncio
    async def test_learn_from_text_expressions(self):
        """Known frequent expressions (e.g. '대박') are counted."""
        learner = SpeechLearner()
        await learner.learn_from_text("대박이다! 진짜 대박.")
        exprs = dict(learner.patterns["expressions"])
        assert any("대박" in k for k in exprs)

    def test_get_top_patterns_returns_top_n(self):
        """get_top_patterns(n) returns at most n items per category."""
        learner = SpeechLearner()
        # Manually populate more than n entries
        for i in range(20):
            learner.patterns["sentence_endings"][f"ending_{i}"] = i
        top = learner.get_top_patterns(n=5)
        assert len(top["sentence_endings"]) == 5

    def test_get_top_patterns_sorted_descending(self):
        """Results are sorted by frequency, highest first."""
        learner = SpeechLearner()
        learner.patterns["sentence_endings"]["rare"] = 1
        learner.patterns["sentence_endings"]["common"] = 10
        learner.patterns["sentence_endings"]["medium"] = 5
        top = learner.get_top_patterns(n=3)
        counts = [count for _, count in top["sentence_endings"]]
        assert counts == sorted(counts, reverse=True)

    def test_get_top_patterns_empty_learner(self):
        """A freshly created SpeechLearner returns empty lists for all categories."""
        learner = SpeechLearner()
        top = learner.get_top_patterns(n=10)
        assert top["sentence_endings"] == []
        assert top["expressions"] == []
        assert top["filler_words"] == []

    def test_get_top_patterns_returns_tuples(self):
        """Each item in a category is a (key, count) tuple."""
        learner = SpeechLearner()
        learner.patterns["expressions"]["헐"] = 3
        top = learner.get_top_patterns(n=1)
        assert top["expressions"][0] == ("헐", 3)

    def test_get_top_patterns_n_larger_than_entries(self):
        """Requesting more than available entries returns all of them."""
        learner = SpeechLearner()
        learner.patterns["filler_words"]["음"] = 2
        top = learner.get_top_patterns(n=100)
        assert len(top["filler_words"]) == 1

    @pytest.mark.asyncio
    async def test_analyze_conversation_processes_all_messages(self):
        """analyze_conversation() processes both user and assistant messages."""
        learner = SpeechLearner()
        messages = [
            {"role": "user", "content": "오늘 기분 좋아요."},
            {"role": "assistant", "content": "저도 기분이 좋아요!"},
        ]
        summary = await learner.analyze_conversation(messages)
        assert "sentence_endings" in summary
        assert "expressions" in summary
        assert "filler_words" in summary

    @pytest.mark.asyncio
    async def test_analyze_conversation_ignores_unknown_roles(self):
        """Messages with roles other than user/assistant are silently skipped."""
        learner = SpeechLearner()
        messages = [
            {"role": "system", "content": "일부 내용"},
            {"role": "user", "content": "안녕요."},
        ]
        # Should not raise
        await learner.analyze_conversation(messages)

    def test_to_personality_config(self):
        """to_personality_config() returns a PersonalityConfig populated from top patterns."""
        learner = SpeechLearner()
        learner.patterns["sentence_endings"]["요"] = 5
        learner.patterns["expressions"]["대박"] = 3
        config = learner.to_personality_config(bot_name="미니", top_n=5)
        assert isinstance(config, PersonalityConfig)
        assert config.bot_name == "미니"
        assert "요" in config.sentence_endings
