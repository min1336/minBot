import json
import logging
import re
from collections import defaultdict
from pathlib import Path

import aiofiles

from app.models.schemas import PersonalityConfig

logger = logging.getLogger(__name__)

# Korean sentence ending patterns (문장 종결어미)
_ENDING_PATTERNS = [
    r"[ㄱ-ㅎ가-힣]+요[.!?~]*$",      # ~요
    r"ㅋ{2,}[.!?~]*$",                # ㅋㅋ / ㅋㅋㅋ
    r"ㅎ{2,}[.!?~]*$",                # ㅎㅎ / ㅎㅎㅎ
    r"[ㄱ-ㅎ가-힣]+임[.!?~]*$",       # ~임
    r"[ㄱ-ㅎ가-힣]+함[.!?~]*$",       # ~함
    r"[ㄱ-ㅎ가-힣]+인데[.!?~]*$",     # ~인데
    r"[ㄱ-ㅎ가-힣]+지[.!?~]*$",       # ~지
    r"[ㄱ-ㅎ가-힣]+잖아[.!?~]*$",     # ~잖아
    r"[ㄱ-ㅎ가-힣]+거든[.!?~]*$",     # ~거든
    r"[ㄱ-ㅎ가-힣]+어[.!?~]*$",       # ~어
    r"[ㄱ-ㅎ가-힣]+아[.!?~]*$",       # ~아
    r"[ㄱ-ㅎ가-힣]+다[.!?~]*$",       # ~다
    r"[ㄱ-ㅎ가-힣]+ㅠ+[.!?~]*$",     # ~ㅠ / ~ㅠㅠ
    r"[ㄱ-ㅎ가-힣]+ㅜ+[.!?~]*$",     # ~ㅜ / ~ㅜㅜ
    r"[ㄱ-ㅎ가-힣]+ㅠㅠ+[.!?~]*$",   # ~ㅠㅠ
]

# Frequent expression patterns (자주 쓰는 표현)
_EXPRESSION_PATTERNS = [
    r"아\s*진짜",
    r"대박",
    r"어쩌구",
    r"진짜로",
    r"완전",
    r"너무",
    r"엄청",
    r"헐+",
    r"오\s*마이",
    r"레알",
    r"ㄹㅇ",
    r"개\s*[ㄱ-ㅎ가-힣]+",
    r"존\s*[ㄱ-ㅎ가-힣]+",
    r"미쳤",
    r"말도\s*안\s*돼",
    r"어떡해",
    r"왜\s*이래",
]

# Filler word patterns (추임새)
_FILLER_PATTERNS = [
    r"^음+[,\s]",
    r"^어+[,\s]",
    r"^그+[,\s]",
    r"^뭐+[,\s]",
    r"^아+[,\s]",
    r"^이+[,\s]",
    r"^저+[,\s]",
    r"^그니까",
    r"^있잖아",
    r"^근데",
]


def _extract_ending(sentence: str) -> str | None:
    """Return the matched ending key from a sentence, or None."""
    sentence = sentence.strip()
    for pattern in _ENDING_PATTERNS:
        m = re.search(pattern, sentence)
        if m:
            return m.group(0).rstrip(".!?~").strip()
    return None


def _extract_expressions(text: str) -> list[str]:
    """Return all expression matches found in text."""
    found = []
    for pattern in _EXPRESSION_PATTERNS:
        matches = re.findall(pattern, text)
        found.extend(matches)
    return found


def _extract_fillers(text: str) -> list[str]:
    """Return filler words found at the start of sentences."""
    found = []
    sentences = re.split(r"[.!?~\n]+", text)
    for sentence in sentences:
        sentence = sentence.strip()
        if not sentence:
            continue
        for pattern in _FILLER_PATTERNS:
            m = re.match(pattern, sentence)
            if m:
                found.append(m.group(0).rstrip(" ,").strip())
    return found


class SpeechLearner:
    """Analyzes conversation logs to extract Korean speech patterns (말투 패턴)."""

    def __init__(self, data_dir: str = "data/conversations") -> None:
        self.data_dir = data_dir
        self.patterns: dict[str, dict[str, int]] = {
            "sentence_endings": defaultdict(int),
            "expressions": defaultdict(int),
            "filler_words": defaultdict(int),
        }

    async def analyze_conversation(self, messages: list[dict]) -> dict:
        """Analyze a conversation to extract speech patterns.

        Each message: {"role": "user"|"assistant", "content": "text"}
        Returns extracted patterns as a summary dict.
        """
        summary: dict[str, list[str]] = {
            "sentence_endings": [],
            "expressions": [],
            "filler_words": [],
        }

        for message in messages:
            role = message.get("role", "")
            content = message.get("content", "")
            if not content or role not in ("user", "assistant"):
                continue

            await self.learn_from_text(content)

            # Collect what was found in this message for the summary
            sentences = re.split(r"[.!?~\n]+", content)
            for sentence in sentences:
                ending = _extract_ending(sentence)
                if ending:
                    summary["sentence_endings"].append(ending)

            summary["expressions"].extend(_extract_expressions(content))
            summary["filler_words"].extend(_extract_fillers(content))

        logger.debug(
            "analyze_conversation: processed %d messages, "
            "endings=%d expressions=%d fillers=%d",
            len(messages),
            len(summary["sentence_endings"]),
            len(summary["expressions"]),
            len(summary["filler_words"]),
        )
        return summary

    async def learn_from_text(self, text: str) -> None:
        """Learn patterns from a single text input."""
        if not text:
            return

        # Sentence endings
        sentences = re.split(r"[.!?~\n]+", text)
        for sentence in sentences:
            ending = _extract_ending(sentence)
            if ending:
                self.patterns["sentence_endings"][ending] += 1

        # Frequent expressions
        for expr in _extract_expressions(text):
            key = expr.strip()
            if key:
                self.patterns["expressions"][key] += 1

        # Filler words
        for filler in _extract_fillers(text):
            if filler:
                self.patterns["filler_words"][filler] += 1

    def get_top_patterns(self, n: int = 10) -> dict:
        """Get the most frequent patterns (top N per category)."""
        result: dict[str, list[tuple[str, int]]] = {}
        for category, freq_map in self.patterns.items():
            sorted_items = sorted(freq_map.items(), key=lambda x: x[1], reverse=True)
            result[category] = sorted_items[:n]
        return result

    async def save_patterns(self, filepath: str = "data/patterns.json") -> None:
        """Persist learned patterns to disk."""
        path = Path(filepath)
        try:
            path.parent.mkdir(parents=True, exist_ok=True)
            serializable = {
                category: dict(freq_map)
                for category, freq_map in self.patterns.items()
            }
            async with aiofiles.open(path, "w", encoding="utf-8") as f:
                await f.write(json.dumps(serializable, ensure_ascii=False, indent=2))
            logger.info("Saved speech patterns to %s", filepath)
        except OSError as e:
            logger.error("Failed to save patterns to %s: %s", filepath, e)

    async def load_patterns(self, filepath: str = "data/patterns.json") -> None:
        """Load previously learned patterns."""
        path = Path(filepath)
        if not path.exists():
            logger.warning("Patterns file not found: %s", filepath)
            return
        try:
            async with aiofiles.open(path, "r", encoding="utf-8") as f:
                raw = await f.read()
            data = json.loads(raw)
            for category in ("sentence_endings", "expressions", "filler_words"):
                if category in data and isinstance(data[category], dict):
                    for key, count in data[category].items():
                        self.patterns[category][key] += int(count)
            logger.info("Loaded speech patterns from %s", filepath)
        except (OSError, json.JSONDecodeError, ValueError) as e:
            logger.error("Failed to load patterns from %s: %s", filepath, e)

    def to_personality_config(self, bot_name: str = "미니", top_n: int = 5) -> PersonalityConfig:
        """Convert current patterns into a PersonalityConfig instance."""
        top = self.get_top_patterns(n=top_n)
        sentence_endings = [item[0] for item in top.get("sentence_endings", [])]
        expressions = [item[0] for item in top.get("expressions", [])]
        fillers = [item[0] for item in top.get("filler_words", [])]
        return PersonalityConfig(
            bot_name=bot_name,
            speech_patterns=fillers,
            sentence_endings=sentence_endings,
            favorite_expressions=expressions,
        )
