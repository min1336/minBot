"""Tests for REST API endpoints — auth, status, personality, conversations."""

import pytest

from app.api.auth import create_access_token


# ---------------------------------------------------------------------------
# Health check
# ---------------------------------------------------------------------------

class TestHealthCheck:
    def test_health_check_returns_ok(self, client):
        response = client.get("/")
        assert response.status_code == 200
        data = response.json()
        assert data["status"] == "ok"

    def test_health_check_returns_bot_name(self, client):
        response = client.get("/")
        assert "bot_name" in response.json()

    def test_health_check_no_auth_required(self, client):
        """Health endpoint must be publicly accessible."""
        response = client.get("/")
        assert response.status_code == 200


# ---------------------------------------------------------------------------
# Auth — POST /api/auth/token
# ---------------------------------------------------------------------------

class TestAuthToken:
    def test_correct_password_returns_token(self, client):
        response = client.post("/api/auth/token", json={"password": "minbot_secret"})
        assert response.status_code == 200
        body = response.json()
        assert "access_token" in body
        assert body["token_type"] == "bearer"

    def test_wrong_password_returns_401(self, client):
        response = client.post("/api/auth/token", json={"password": "wrong_password"})
        assert response.status_code == 401

    def test_empty_body_returns_401(self, client):
        response = client.post("/api/auth/token", json={})
        assert response.status_code == 401

    def test_missing_password_field_returns_401(self, client):
        response = client.post("/api/auth/token", json={"user": "admin"})
        assert response.status_code == 401

    def test_token_is_string(self, client):
        response = client.post("/api/auth/token", json={"password": "minbot_secret"})
        token = response.json()["access_token"]
        assert isinstance(token, str)
        assert len(token) > 10


# ---------------------------------------------------------------------------
# Protected endpoints — unauthenticated access
# ---------------------------------------------------------------------------

class TestUnauthenticatedAccess:
    PROTECTED_ROUTES = [
        ("GET", "/api/status"),
        ("GET", "/api/personality"),
        ("GET", "/api/conversations"),
    ]

    @pytest.mark.parametrize("method,path", PROTECTED_ROUTES)
    def test_protected_route_without_auth_returns_403_or_401(self, client, method, path):
        """Protected routes must reject requests that carry no Authorization header."""
        response = client.request(method, path)
        assert response.status_code in (401, 403), (
            f"{method} {path} returned {response.status_code} — expected 401 or 403"
        )

    def test_invalid_token_returns_401(self, client):
        headers = {"Authorization": "Bearer not.a.real.token"}
        response = client.get("/api/status", headers=headers)
        assert response.status_code == 401

    def test_malformed_auth_header_returns_403(self, client):
        """Missing 'Bearer ' prefix triggers HTTPBearer rejection (403)."""
        headers = {"Authorization": "Token abc123"}
        response = client.get("/api/status", headers=headers)
        assert response.status_code == 403


# ---------------------------------------------------------------------------
# GET /api/status
# ---------------------------------------------------------------------------

class TestStatusEndpoint:
    def test_returns_200_with_auth(self, client, auth_headers):
        response = client.get("/api/status", headers=auth_headers)
        assert response.status_code == 200

    def test_response_contains_required_fields(self, client, auth_headers):
        response = client.get("/api/status", headers=auth_headers)
        data = response.json()
        assert "battery_percent" in data
        assert "is_connected" in data
        assert "current_emotion" in data
        assert "wifi_rssi" in data
        assert "uptime_seconds" in data

    def test_battery_percent_default(self, client, auth_headers):
        response = client.get("/api/status", headers=auth_headers)
        assert response.json()["battery_percent"] == 100

    def test_uptime_is_non_negative_integer(self, client, auth_headers):
        response = client.get("/api/status", headers=auth_headers)
        uptime = response.json()["uptime_seconds"]
        assert isinstance(uptime, int)
        assert uptime >= 0

    def test_current_emotion_is_valid(self, client, auth_headers):
        from app.models.schemas import Emotion
        response = client.get("/api/status", headers=auth_headers)
        emotion_value = response.json()["current_emotion"]
        valid_values = {e.value for e in Emotion}
        assert emotion_value in valid_values


# ---------------------------------------------------------------------------
# GET /api/personality
# ---------------------------------------------------------------------------

class TestPersonalityEndpoint:
    def test_returns_200_with_auth(self, client, auth_headers):
        response = client.get("/api/personality", headers=auth_headers)
        assert response.status_code == 200

    def test_response_contains_bot_name(self, client, auth_headers):
        response = client.get("/api/personality", headers=auth_headers)
        data = response.json()
        assert "bot_name" in data

    def test_response_contains_personality_traits(self, client, auth_headers):
        response = client.get("/api/personality", headers=auth_headers)
        data = response.json()
        assert "personality_traits" in data
        assert isinstance(data["personality_traits"], list)

    def test_response_contains_speech_fields(self, client, auth_headers):
        response = client.get("/api/personality", headers=auth_headers)
        data = response.json()
        for field in ("speech_patterns", "sentence_endings", "favorite_expressions"):
            assert field in data, f"Missing field: {field}"


# ---------------------------------------------------------------------------
# PUT /api/personality
# ---------------------------------------------------------------------------

class TestUpdatePersonality:
    def test_update_personality_returns_200(self, client, auth_headers):
        payload = {
            "bot_name": "봇봇",
            "speech_patterns": [],
            "sentence_endings": ["~요", "~다"],
            "favorite_expressions": ["대박"],
            "personality_traits": ["활발한"],
        }
        response = client.put("/api/personality", json=payload, headers=auth_headers)
        assert response.status_code == 200

    def test_updated_personality_is_reflected_in_get(self, client, auth_headers):
        """PUT then GET returns the updated config."""
        payload = {
            "bot_name": "업데이트봇",
            "speech_patterns": [],
            "sentence_endings": [],
            "favorite_expressions": [],
            "personality_traits": ["조용한"],
        }
        client.put("/api/personality", json=payload, headers=auth_headers)
        response = client.get("/api/personality", headers=auth_headers)
        assert response.json()["bot_name"] == "업데이트봇"


# ---------------------------------------------------------------------------
# GET /api/conversations
# ---------------------------------------------------------------------------

class TestConversationsEndpoint:
    def test_returns_200_with_auth(self, client, auth_headers):
        response = client.get("/api/conversations", headers=auth_headers)
        assert response.status_code == 200

    def test_returns_list(self, client, auth_headers):
        response = client.get("/api/conversations", headers=auth_headers)
        assert isinstance(response.json(), list)

    def test_limit_parameter_accepted(self, client, auth_headers):
        response = client.get("/api/conversations?limit=5", headers=auth_headers)
        assert response.status_code == 200

    def test_limit_out_of_range_returns_422(self, client, auth_headers):
        """limit must be between 1 and 100 (FastAPI Query validation)."""
        response = client.get("/api/conversations?limit=0", headers=auth_headers)
        assert response.status_code == 422
