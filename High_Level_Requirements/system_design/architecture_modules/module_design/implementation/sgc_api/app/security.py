"""Credential hashing, JWT issue/verify, refresh-token table.

§5 auth model + §10 security:
  * passwords: argon2id (argon2-cffi)
  * access token: JWT, 60 min — stateless, carries sub + user_type
  * refresh token: opaque random string, 30 days, stored HASHED server-side
    in `refresh_tokens`; rotated on every use and revocable (§10
    "refresh-token rotation + revocation list")

The refresh_tokens table is server-side session state — not part of REQ-DB
(like users.password_hash, it is the §5/§11.2-sanctioned credential
machinery). It lives here, next to the code that owns its lifecycle.
"""

import hashlib
import secrets
from datetime import datetime, timedelta, timezone

from argon2 import PasswordHasher
from argon2.exceptions import VerifyMismatchError
from jwt import InvalidTokenError, decode, encode
from sqlalchemy import DateTime, ForeignKey, Integer, Text
from sqlalchemy.orm import Mapped, mapped_column

from app.config import get_settings
from app.db import Base, ensure_utc, to_db_utc

_hasher = PasswordHasher()  # argon2id with library defaults


# --- RefreshToken ORM table --------------------------------------------------


class RefreshToken(Base):
    """refresh_tokens — one row per issued refresh token (hashed)."""

    __tablename__ = "refresh_tokens"

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    user_id: Mapped[int] = mapped_column(
        ForeignKey("users.id", ondelete="CASCADE"), nullable=False
    )
    # sha256 of the opaque token — a DB leak must not yield usable tokens.
    token_hash: Mapped[str] = mapped_column(Text, nullable=False, unique=True)
    expires_at: Mapped[datetime] = mapped_column(DateTime, nullable=False)
    # Soft revoke marker; deletion = hard revoke (GDPR clears the table).
    revoked_at: Mapped[datetime | None] = mapped_column(DateTime, nullable=True)
    created_at: Mapped[datetime] = mapped_column(DateTime, nullable=False)

    def is_valid(self, now: datetime) -> bool:
        # ensure_utc: SQLite reads the stored value back naive.
        return self.revoked_at is None and now < ensure_utc(self.expires_at)


# --- Password hashing (argon2id) ----------------------------------------------


def hash_password(password: str) -> str:
    return _hasher.hash(password)


def verify_password(password: str, password_hash: str) -> bool:
    try:
        return _hasher.verify(password_hash, password)
    except VerifyMismatchError:
        return False
    # Everything else (malformed hash, etc.) also counts as a failed login.


# --- Access token (JWT) --------------------------------------------------------


def create_access_token(user_id: int, user_type: str) -> str:
    cfg = get_settings()
    now = datetime.now(timezone.utc)
    payload = {
        "sub": str(user_id),
        "user_type": user_type,
        "iat": now,
        "exp": now + timedelta(minutes=cfg.access_token_minutes),
    }
    return encode(payload, cfg.secret_key, algorithm=cfg.jwt_algorithm)


def decode_access_token(token: str) -> dict | None:
    """Returns the claim dict, or None for any invalid/expired token."""
    try:
        return decode(
            token, get_settings().secret_key, algorithms=[get_settings().jwt_algorithm]
        )
    except InvalidTokenError:
        return None


# --- Refresh token (opaque, hashed at rest) -------------------------------------


def _hash_refresh_token(raw: str) -> str:
    return hashlib.sha256(raw.encode("utf-8")).hexdigest()


def new_refresh_token() -> tuple[str, str, datetime]:
    """Returns (raw_token, token_hash, expires_at). Only the hash is stored."""
    cfg = get_settings()
    raw = secrets.token_urlsafe(32)
    expires = to_db_utc(datetime.now(timezone.utc) + timedelta(days=cfg.refresh_token_days))
    return raw, _hash_refresh_token(raw), expires


def lookup_refresh_token(raw: str) -> str:
    """Map a raw presented token to its stored hash (lookup key)."""
    return _hash_refresh_token(raw)
