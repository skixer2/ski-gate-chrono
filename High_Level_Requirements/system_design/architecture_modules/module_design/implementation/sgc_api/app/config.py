"""Application configuration.

Single source of truth for runtime settings. Everything comes from the
environment (12-factor); there are no secrets in code.

Remote-DB architecture reference: remote_db_architecture.md §3, §5, §11.
"""

from functools import lru_cache

from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    """Environment-driven settings (prefix SGC_API_)."""

    model_config = SettingsConfigDict(env_prefix="SGC_API_", extra="ignore")

    # --- Security -----------------------------------------------------------
    # JWT signing secret. MUST come from the environment in any real deployment.
    # The default exists only so unit tests / smoke runs work out of the box.
    secret_key: str = "dev-insecure-change-me"
    # Weak/default secrets abort startup (review hardening 2026-10-26:
    # PyJWT warns below 32 bytes for HS256, RFC 7518 §3.2). Tests/smoke
    # set a proper 32+ byte secret via conftest/smoke.sh.
    allow_insecure_secret: bool = False
    jwt_algorithm: str = "HS256"
    # Token lifetimes per §5: access 60 min, refresh 30 days.
    access_token_minutes: int = 60
    refresh_token_days: int = 30

    # --- Database -----------------------------------------------------------
    # P0 = SQLite (WAL). Flip to PostgreSQL at P2 = change this URL + alembic
    # upgrade (§2 guardrails). The SQLite file lives on a volume in Docker.
    database_url: str = "sqlite:///./data/sgc.db"

    # --- Bootstrap info (I07) ----------------------------------------------
    # Mirrored verbatim by GET /bootstrap-info. One indirection so the API can
    # move host/domain without an app release (§3).
    api_base: str = "http://127.0.0.1:8000"
    min_app_version: str = "1.65"

    # --- Run upload caps ----------------------------------------------------
    # §4: "413 if a run exceeds sane row caps (reject ⇒ queue item stays
    # retryable, never silently truncated)". 600 s @ 10 Hz = 6000 baro rows,
    # so 10 000 leaves headroom; gate counts are well under 1000.
    max_timestamps_per_run: int = 1000
    max_barometric_rows_per_run: int = 10_000

    # --- Retention (§5, JP 2026-10-26) --------------------------------------
    # Cloud run data older than this is purged nightly (reqs v6.3).
    retention_days: int = 365


def _validate_secret(settings: Settings) -> None:
    """Fail fast on weak/default JWT secrets (unless explicitly allowed)."""
    weak = (settings.secret_key == "dev-insecure-change-me"
            or len(settings.secret_key.encode("utf-8")) < 32)
    if weak and not settings.allow_insecure_secret:
        raise ValueError(
            "SGC_API_SECRET_KEY must be set to >=32 random bytes "
            "(HS256, RFC 7518 3.2). Set SGC_API_ALLOW_INSECURE_SECRET=1 "
            "only for throwaway local experiments.")


@lru_cache
def get_settings() -> Settings:
    """Cached settings accessor (import-safe; env is read once per process)."""
    settings = Settings()
    _validate_secret(settings)
    return settings
