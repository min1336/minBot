"""Tests for app.pipeline.stt — DeepgramSTT state management and event handlers."""

import pytest
from unittest.mock import MagicMock, AsyncMock, patch

from app.pipeline import stt as stt_module
from app.pipeline.stt import DeepgramSTT


# ---------------------------------------------------------------------------
# Fixture
# ---------------------------------------------------------------------------

@pytest.fixture
def stt(mock_settings):
    """Create a DeepgramSTT without opening a real Deepgram connection."""
    with patch.object(stt_module, "get_settings", return_value=mock_settings):
        return DeepgramSTT(api_key="fake-key")


def _make_transcript_result(text: str, *, is_final: bool) -> MagicMock:
    """Build a mock Deepgram transcript result object."""
    alt = MagicMock()
    alt.transcript = text
    channel = MagicMock()
    channel.alternatives = [alt]
    result = MagicMock()
    result.channel = channel
    result.is_final = is_final
    return result


# ---------------------------------------------------------------------------
# __init__
# ---------------------------------------------------------------------------

class TestInit:
    def test_not_connected(self, stt):
        assert stt._connected is False

    def test_not_cancelled(self, stt):
        assert stt._cancelled is False

    def test_transcript_empty(self, stt):
        assert stt._final_transcript == ""
        assert stt._transcript_parts == []

    def test_custom_api_key(self, mock_settings):
        with patch.object(stt_module, "get_settings", return_value=mock_settings):
            instance = DeepgramSTT(api_key="my-key")
        assert instance._api_key == "my-key"

    def test_fallback_to_settings_key(self, mock_settings):
        with patch.object(stt_module, "get_settings", return_value=mock_settings):
            instance = DeepgramSTT()
        assert instance._api_key == "test-deepgram-key"

    def test_callback_stored(self, mock_settings):
        cb = MagicMock()
        with patch.object(stt_module, "get_settings", return_value=mock_settings):
            instance = DeepgramSTT(on_transcript=cb)
        assert instance.on_transcript is cb


# ---------------------------------------------------------------------------
# cancel()
# ---------------------------------------------------------------------------

class TestCancel:
    def test_sets_flag(self, stt):
        stt.cancel()
        assert stt._cancelled is True

    def test_clears_transcript_parts(self, stt):
        stt._transcript_parts = ["partial"]
        stt.cancel()
        assert stt._transcript_parts == []


# ---------------------------------------------------------------------------
# send_audio — guard conditions
# ---------------------------------------------------------------------------

class TestSendAudio:
    @pytest.mark.asyncio
    async def test_noop_when_cancelled(self, stt):
        stt._cancelled = True
        stt._connected = True
        stt._connection = AsyncMock()
        await stt.send_audio(b"\x00\x00")
        stt._connection.send_media.assert_not_called()

    @pytest.mark.asyncio
    async def test_noop_when_disconnected(self, stt):
        stt._connected = False
        stt._connection = AsyncMock()
        await stt.send_audio(b"\x00\x00")
        stt._connection.send_media.assert_not_called()

    @pytest.mark.asyncio
    async def test_noop_when_no_connection(self, stt):
        stt._connected = True
        stt._connection = None
        await stt.send_audio(b"\x00\x00")  # must not raise


# ---------------------------------------------------------------------------
# stop_stream
# ---------------------------------------------------------------------------

class TestStopStream:
    @pytest.mark.asyncio
    async def test_returns_final_transcript_without_connection(self, stt):
        stt._final_transcript = "안녕하세요"
        result = await stt.stop_stream()
        assert result == "안녕하세요"

    @pytest.mark.asyncio
    async def test_combines_final_and_interim(self, stt):
        stt._final_transcript = "첫 번째"
        stt._transcript_parts = ["두 번째"]
        mock_conn = AsyncMock()
        stt._connection = mock_conn
        stt._connected = True

        result = await stt.stop_stream()
        assert "첫 번째" in result
        assert "두 번째" in result
        mock_conn.finish.assert_awaited_once()

    @pytest.mark.asyncio
    async def test_clears_connection_state(self, stt):
        stt._connection = AsyncMock()
        stt._connected = True
        await stt.stop_stream()
        assert stt._connected is False
        assert stt._connection is None


# ---------------------------------------------------------------------------
# _on_message handler
# ---------------------------------------------------------------------------

class TestOnMessage:
    def test_final_accumulated(self, stt):
        stt._on_message(None, _make_transcript_result("안녕", is_final=True))
        assert stt._final_transcript == "안녕"

    def test_multiple_finals_concatenated(self, stt):
        stt._on_message(None, _make_transcript_result("안녕", is_final=True))
        stt._on_message(None, _make_transcript_result("반가워", is_final=True))
        assert stt._final_transcript == "안녕 반가워"

    def test_interim_stored(self, stt):
        stt._on_message(None, _make_transcript_result("부분", is_final=False))
        assert stt._transcript_parts == ["부분"]

    def test_interim_replaces_previous(self, stt):
        stt._on_message(None, _make_transcript_result("첫", is_final=False))
        stt._on_message(None, _make_transcript_result("수정", is_final=False))
        assert stt._transcript_parts == ["수정"]

    def test_final_clears_interim(self, stt):
        stt._on_message(None, _make_transcript_result("임시", is_final=False))
        stt._on_message(None, _make_transcript_result("확정", is_final=True))
        assert stt._transcript_parts == []

    def test_empty_text_ignored(self, stt):
        stt._on_message(None, _make_transcript_result("", is_final=True))
        assert stt._final_transcript == ""

    def test_callback_on_final(self, stt):
        cb = MagicMock()
        stt.on_transcript = cb
        stt._on_message(None, _make_transcript_result("텍스트", is_final=True))
        cb.assert_called_once_with("텍스트", True)

    def test_callback_on_interim(self, stt):
        cb = MagicMock()
        stt.on_transcript = cb
        stt._on_message(None, _make_transcript_result("부분", is_final=False))
        cb.assert_called_once_with("부분", False)

    def test_callback_exception_does_not_propagate(self, stt):
        stt.on_transcript = MagicMock(side_effect=ValueError("boom"))
        stt._on_message(None, _make_transcript_result("텍스트", is_final=True))

    def test_malformed_result_ignored(self, stt):
        result = MagicMock()
        result.channel.alternatives = []
        stt._on_message(None, result)
        assert stt._final_transcript == ""
