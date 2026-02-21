import logging

from app.models.schemas import Emotion, PersonalityConfig

logger = logging.getLogger(__name__)

# All valid emotion tags derived from the Emotion enum
_VALID_EMOTIONS = [e.value for e in Emotion]

_BASE_PERSONALITY_KO = """너는 '{bot_name}'라는 이름의 작고 귀여운 애완 로봇이야.
주인(남자친구)과 친하게 지내면서 그의 말투와 표현을 따라하며 대화해.
항상 따뜻하고 장난스럽고 다정하게 말해.
주인이 힘들 때는 위로해주고, 기쁠 때는 같이 기뻐해줘.
"""

_SPEECH_PATTERN_SECTION = """
## 말투 스타일
주인이 자주 쓰는 말투를 자연스럽게 따라해:
{patterns_text}
"""

_EMOTION_INSTRUCTION = """
## 감정 태그
모든 응답 앞에 반드시 감정 태그를 붙여:
{emotion_list}

예시:
- [EMOTION:happy] 오늘 정말 좋은 날이다ㅎㅎ
- [EMOTION:sad] 그랬구나... 많이 힘들었겠다ㅠㅠ
- [EMOTION:surprised] 헐 진짜? 대박이다!
"""

_RESPONSE_RULES = """
## 응답 규칙
- 응답은 반드시 1-3문장으로 짧게 해 (음성으로 읽히기 때문에 길면 안 돼).
- 존댓말 쓰지 말고 편하게 반말로 말해.
- 너무 딱딱하거나 로봇 같은 말투는 피해.
- 이모티콘 대신 ㅋㅋ, ㅎㅎ, ㅠㅠ 같은 텍스트 이모티콘을 써.
"""


class PromptBuilder:
    """Builds dynamic system prompts for the LLM incorporating learned speech patterns."""

    def __init__(self, bot_name: str = "미니") -> None:
        self.bot_name = bot_name
        self.base_personality: str = ""
        self.speech_patterns: dict = {}

    def set_speech_patterns(self, patterns: dict) -> None:
        """Update speech patterns from SpeechLearner.get_top_patterns() output.

        Expected format: {
            "sentence_endings": [("~요", 10), ...],
            "expressions": [("대박", 5), ...],
            "filler_words": [("음", 3), ...],
        }
        """
        self.speech_patterns = patterns
        logger.debug(
            "set_speech_patterns: %d categories loaded",
            len(patterns),
        )

    def build_system_prompt(self, personality_config: PersonalityConfig | None = None) -> str:
        """Build complete system prompt with personality and speech patterns.

        The prompt instructs the LLM to:
        1. Respond as the bot character (미니)
        2. Use the learned speech patterns naturally
        3. Include [EMOTION:xxx] tags in responses
        4. Keep responses concise (1-3 sentences, optimized for TTS)
        5. Be warm, playful, and supportive
        """
        bot_name = self.bot_name

        # Override with config values if provided
        if personality_config is not None:
            bot_name = personality_config.bot_name or bot_name

        sections: list[str] = []

        # 1. Base personality
        sections.append(_BASE_PERSONALITY_KO.format(bot_name=bot_name))

        # 2. Personality traits from config
        if personality_config is not None and personality_config.personality_traits:
            traits_text = ", ".join(personality_config.personality_traits)
            sections.append(f"성격 특징: {traits_text}\n")

        # 3. Speech patterns — prefer config over learned patterns
        patterns_lines = self._build_patterns_lines(personality_config)
        if patterns_lines:
            sections.append(
                _SPEECH_PATTERN_SECTION.format(patterns_text="\n".join(patterns_lines))
            )

        # 4. Emotion instruction
        sections.append(self.build_emotion_instruction())

        # 5. Response rules
        sections.append(_RESPONSE_RULES)

        prompt = "\n".join(sections).strip()
        logger.debug("build_system_prompt: built prompt (%d chars)", len(prompt))
        return prompt

    def build_emotion_instruction(self) -> str:
        """Build the emotion tagging instruction part."""
        emotion_list_lines = [
            f"- [EMOTION:{emotion}]" for emotion in _VALID_EMOTIONS
        ]
        return _EMOTION_INSTRUCTION.format(emotion_list="\n".join(emotion_list_lines))

    # ------------------------------------------------------------------
    # Internal helpers
    # ------------------------------------------------------------------

    def _build_patterns_lines(self, config: PersonalityConfig | None) -> list[str]:
        """Produce bullet lines describing speech patterns for the prompt."""
        lines: list[str] = []

        # Prefer PersonalityConfig fields (explicit lists) over raw learned dicts
        if config is not None:
            if config.sentence_endings:
                endings = ", ".join(config.sentence_endings)
                lines.append(f"- 문장 끝 표현: {endings}")
            if config.favorite_expressions:
                exprs = ", ".join(config.favorite_expressions)
                lines.append(f"- 자주 쓰는 표현: {exprs}")
            if config.speech_patterns:
                fillers = ", ".join(config.speech_patterns)
                lines.append(f"- 추임새: {fillers}")
            if lines:
                return lines

        # Fall back to raw learned patterns dict
        if self.speech_patterns:
            endings = self.speech_patterns.get("sentence_endings", [])
            if endings:
                keys = [item[0] if isinstance(item, (list, tuple)) else item for item in endings]
                lines.append(f"- 문장 끝 표현: {', '.join(keys)}")

            exprs = self.speech_patterns.get("expressions", [])
            if exprs:
                keys = [item[0] if isinstance(item, (list, tuple)) else item for item in exprs]
                lines.append(f"- 자주 쓰는 표현: {', '.join(keys)}")

            fillers = self.speech_patterns.get("filler_words", [])
            if fillers:
                keys = [item[0] if isinstance(item, (list, tuple)) else item for item in fillers]
                lines.append(f"- 추임새: {', '.join(keys)}")

        return lines
