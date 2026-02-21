"""Unit tests for app.api.auth — token creation and verification."""

import time

import pytest
from unittest.mock import MagicMock
from jose import jwt

from app.api.auth import create_access_token, verify_token


# ---------------------------------------------------------------------------
# create_access_token
# ---------------------------------------------------------------------------

class TestCreateAccessToken:
    def test_returns_string(self, mock_settings):
        token = create_access_token(subject="test_user")
        assert isinstance(token, str)

    def test_token_contains_subject_claim(self, mock_settings):
        token = create_access_token(subject="mobile_app")
        payload = jwt.decode(
            token,
            mock_settings.jwt_secret_key,
            algorithms=[mock_settings.jwt_algorithm],
        )
        assert payload["sub"] == "mobile_app"

    def test_token_contains_exp_claim(self, mock_settings):
        token = create_access_token(subject="user")
        payload = jwt.decode(
            token,
            mock_settings.jwt_secret_key,
            algorithms=[mock_settings.jwt_algorithm],
        )
        assert "exp" in payload

    def test_token_expiry_is_in_the_future(self, mock_settings):
        token = create_access_token(subject="user")
        payload = jwt.decode(
            token,
            mock_settings.jwt_secret_key,
            algorithms=[mock_settings.jwt_algorithm],
        )
        assert payload["exp"] > time.time()

    def test_different_subjects_produce_different_tokens(self, mock_settings):
        t1 = create_access_token(subject="alice")
        t2 = create_access_token(subject="bob")
        assert t1 != t2


# ---------------------------------------------------------------------------
# verify_token
# ---------------------------------------------------------------------------

class TestVerifyToken:
    def test_valid_token_returns_subject(self, mock_settings):
        token = create_access_token(subject="mobile_app")
        creds = MagicMock()
        creds.credentials = token
        result = verify_token(credentials=creds)
        assert result == "mobile_app"

    def test_expired_token_raises_401(self, mock_settings):
        from datetime import datetime, timezone
        from fastapi import HTTPException

        expired_payload = {"sub": "user", "exp": datetime(2020, 1, 1, tzinfo=timezone.utc)}
        token = jwt.encode(
            expired_payload,
            mock_settings.jwt_secret_key,
            algorithm=mock_settings.jwt_algorithm,
        )
        creds = MagicMock()
        creds.credentials = token
        with pytest.raises(HTTPException) as exc_info:
            verify_token(credentials=creds)
        assert exc_info.value.status_code == 401

    def test_tampered_token_raises_401(self, mock_settings):
        from fastapi import HTTPException

        token = create_access_token(subject="user")
        tampered = token[:-5] + "XXXXX"
        creds = MagicMock()
        creds.credentials = tampered
        with pytest.raises(HTTPException) as exc_info:
            verify_token(credentials=creds)
        assert exc_info.value.status_code == 401

    def test_token_without_sub_raises_401(self, mock_settings):
        from fastapi import HTTPException

        payload = {"exp": time.time() + 3600}
        token = jwt.encode(
            payload,
            mock_settings.jwt_secret_key,
            algorithm=mock_settings.jwt_algorithm,
        )
        creds = MagicMock()
        creds.credentials = token
        with pytest.raises(HTTPException) as exc_info:
            verify_token(credentials=creds)
        assert exc_info.value.status_code == 401

    def test_wrong_secret_raises_401(self, mock_settings):
        from fastapi import HTTPException

        payload = {"sub": "user", "exp": time.time() + 3600}
        token = jwt.encode(payload, "wrong-secret", algorithm="HS256")
        creds = MagicMock()
        creds.credentials = token
        with pytest.raises(HTTPException) as exc_info:
            verify_token(credentials=creds)
        assert exc_info.value.status_code == 401

    def test_garbage_string_raises_401(self, mock_settings):
        from fastapi import HTTPException

        creds = MagicMock()
        creds.credentials = "not-a-jwt-at-all"
        with pytest.raises(HTTPException) as exc_info:
            verify_token(credentials=creds)
        assert exc_info.value.status_code == 401
