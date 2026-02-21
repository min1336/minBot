"""
Groq LLM module for minBot.

Uses Groq Python SDK with Llama 4 Scout streaming.
Supports barge-in via cancel(), emotion tag extraction, and
conversation history management (last 10 turns).
"""

import asyncio
import logging
import re
from typing import AsyncIterator

from groq import AsyncGroq

from app.config import get_settings

logger = logging.getLogger(__name__)

# Supported emotions mapped from LLM tags
SUPPORTED_EMOTIONS = frozenset(
    {"idle", "happy", "sad", "surprised", "thinking", "speaking"}
)

_EMOTION_RE = re.compile(r"\[EMOTION:([a-z]+)\]", re.IGNORECASE)

MAX_HISTORY_TURNS = 10  # number of user/assistant pairs to keep


def extract_emotion(text: str) -> tuple[str, str]:
    """Return (clean_text, emotion) by stripping the first [EMOTION:xxx] tag.

    If no tag is present or the emotion is unsupported, emotion defaults to
    'speaking' (the neutral speaking state).
    """
    match = _EMOTION_RE.search(text)
    if match:
        emotion_candidate = match.group(1).lower()
        emotion = emotion_candidate if emotion_candidate in SUPPORTED_EMOTIONS else "speaking"
        clean_text = _EMOTION_RE.sub("", text).strip()
    else:
        emotion = "speaking"
        clean_text = text.strip()
    return clean_text, emotion


_DEFAULT_SYSTEM_PROMPT = (
    "당신은 '미니'라는 이름의 소형 애완 로봇입니다. "
    "귀엽고 따뜻하며 사용자를 진심으로 아끼는 성격입니다. "
    "짧고 자연스러운 한국어로 대답하세요 (1-3문장). "
    "대답 앞에 현재 감정을 [EMOTION:emotion_name] 형태로 반드시 표시하세요. "
    "사용 가능한 감정: idle, happy, sad, surprised, thinking, speaking. "
    "예시: [EMOTION:happy] 안녕하세요! 오늘 기분이 어때요?"
)


class GroqLLM:
    """Streaming LLM client backed by Groq (Llama 4 Scout)."""

    def __init__(
        self,
        api_key: str,
        model: str = "meta-llama/llama-4-scout-17b-16e-instruct",
    ) -> None:
        self._client = AsyncGroq(api_key=api_key)
        self._model = model
        self._cancel_event = asyncio.Event()
        # Conversation history stores {'role': ..., 'content': ...} dicts
        self._history: list[dict] = []

    # ------------------------------------------------------------------
    # Public API
    # ------------------------------------------------------------------

    async def generate(
        self,
        user_message: str,
        system_prompt: str = "",
        conversation_history: list[dict] | None = None,
    ) -> AsyncIterator[str]:
        """Stream token chunks for *user_message*.

        Yields raw text chunks (including any [EMOTION:xxx] tags) as they
        arrive from the Groq API.  The caller is responsible for calling
        extract_emotion() on the accumulated text when needed.

        If *conversation_history* is provided it is used instead of the
        internal history buffer (useful for stateless callers).
        """
        self._cancel_event.clear()

        sys_prompt = system_prompt.strip() if system_prompt else _DEFAULT_SYSTEM_PROMPT

        # Build message list
        history = conversation_history if conversation_history is not None else self._history
        messages: list[dict] = (
            [{"role": "system", "content": sys_prompt}]
            + history
            + [{"role": "user", "content": user_message}]
        )

        try:
            stream = await self._client.chat.completions.create(
                model=self._model,
                messages=messages,
                max_tokens=150,
                temperature=0.7,
                stream=True,
            )

            collected_content: list[str] = []

            async for chunk in stream:
                if self._cancel_event.is_set():
                    logger.info("GroqLLM: generation cancelled (barge-in)")
                    break

                delta = chunk.choices[0].delta.content
                if delta:
                    collected_content.append(delta)
                    yield delta

            # Update internal history only when using the internal buffer
            if conversation_history is None:
                full_response = "".join(collected_content)
                self._update_history(user_message, full_response)

        except Exception as exc:
            logger.error("GroqLLM: API error during generate: %s", exc, exc_info=True)
            raise

    def cancel(self) -> None:
        """Signal the ongoing generation to stop (barge-in support)."""
        self._cancel_event.set()
        logger.debug("GroqLLM: cancel() called")

    def clear_history(self) -> None:
        """Reset the internal conversation history."""
        self._history.clear()

    @property
    def history(self) -> list[dict]:
        """Read-only view of the current conversation history."""
        return list(self._history)

    # ------------------------------------------------------------------
    # Internal helpers
    # ------------------------------------------------------------------

    def _update_history(self, user_message: str, assistant_response: str) -> None:
        """Append a turn and trim to MAX_HISTORY_TURNS pairs."""
        self._history.append({"role": "user", "content": user_message})
        self._history.append({"role": "assistant", "content": assistant_response})

        # Each turn = 2 messages (user + assistant); keep last N turns
        max_messages = MAX_HISTORY_TURNS * 2
        if len(self._history) > max_messages:
            self._history = self._history[-max_messages:]


def create_llm() -> GroqLLM:
    """Factory that reads api_key from app settings."""
    settings = get_settings()
    return GroqLLM(api_key=settings.groq_api_key)
