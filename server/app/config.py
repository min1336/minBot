from pydantic_settings import BaseSettings
from functools import lru_cache


class Settings(BaseSettings):
    # Deepgram STT
    deepgram_api_key: str = ""

    # Groq LLM
    groq_api_key: str = ""

    # ElevenLabs TTS
    elevenlabs_api_key: str = ""
    elevenlabs_voice_id: str = ""

    # Voice Clone Provider
    voice_clone_provider: str = "elevenlabs"  # elevenlabs | xtts | cosyvoice

    # XTTS v2 self-hosted
    xtts_server_url: str = "http://localhost:8001"

    # CosyVoice 2 self-hosted
    cosyvoice_server_url: str = "http://localhost:8002"

    # Server
    server_host: str = "0.0.0.0"
    server_port: int = 8000

    # JWT
    jwt_secret_key: str = "change_this_to_a_random_secret"
    jwt_algorithm: str = "HS256"
    jwt_expire_minutes: int = 1440

    # Personality
    bot_name: str = "미니"
    default_personality: str = "friendly"

    model_config = {"env_file": ".env", "env_file_encoding": "utf-8"}


@lru_cache
def get_settings() -> Settings:
    return Settings()
