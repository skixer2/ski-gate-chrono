"""/v1/auth — register, login, refresh (§4, §5).

Activation windows are NOT enforced as a login block: JP decided (§5,
2026-10-26) that expiry degrades the account to read-only — writes get
403 at the endpoint level (deps.require_write_allowed), reads/deletes
keep working, and login must therefore keep working too.
"""

from datetime import datetime, timezone

from fastapi import APIRouter, Depends, HTTPException, status
from sqlalchemy import select
from sqlalchemy.orm import Session

from app.config import get_settings
from app.db import get_db, to_db_utc
from app.models import SkiClub, User
from app.schemas import (
    LoginRequest,
    RefreshRequest,
    RegisterRequest,
    TokenResponse,
)
from app.security import (
    RefreshToken,
    create_access_token,
    hash_password,
    lookup_refresh_token,
    new_refresh_token,
    verify_password,
)

router = APIRouter(prefix="/v1/auth", tags=["auth"])


def _issue_tokens(db: Session, user: User) -> TokenResponse:
    """Access + rotated refresh token. Rotation: every refresh mints a new
    refresh token and revokes the presented one (§10)."""
    raw, token_hash, expires = new_refresh_token()
    db.add(RefreshToken(user_id=user.id, token_hash=token_hash, expires_at=expires,
                        created_at=to_db_utc(datetime.now(timezone.utc))))
    db.commit()
    return TokenResponse(
        access_token=create_access_token(user.id, user.user_type),
        refresh_token=raw,
        expires_in=get_settings().access_token_minutes * 60,
    )


@router.post("/register", status_code=status.HTTP_201_CREATED)
def register(payload: RegisterRequest, db: Session = Depends(get_db)) -> dict:
    """v1 MVP: athlete/master self-registration only (§4)."""
    if db.scalar(select(User).where(User.name == payload.name)) is not None:
        raise HTTPException(status.HTTP_409_CONFLICT, "Name already registered")

    club: SkiClub | None = None
    if payload.club_id is not None:
        club = db.get(SkiClub, payload.club_id)
        if club is None:
            raise HTTPException(status.HTTP_404_NOT_FOUND, "Club not found")

    user = User(
        name=payload.name,
        age=payload.age,
        category=payload.category,
        user_type=payload.user_type,
        password_hash=hash_password(payload.password),
        club_id=club.id if club else None,
    )
    db.add(user)
    db.commit()
    return {"id": user.id, "name": user.name, "user_type": user.user_type}


@router.post("/login", response_model=TokenResponse)
def login(payload: LoginRequest, db: Session = Depends(get_db)) -> TokenResponse:
    user = db.scalar(select(User).where(User.name == payload.name))
    # Uniform 401 — never reveal whether the name exists.
    if user is None or not verify_password(payload.password, user.password_hash):
        raise HTTPException(status.HTTP_401_UNAUTHORIZED, "Invalid credentials")
    return _issue_tokens(db, user)


@router.post("/refresh", response_model=TokenResponse)
def refresh(payload: RefreshRequest, db: Session = Depends(get_db)) -> TokenResponse:
    now = datetime.now(timezone.utc)
    stored = db.scalar(
        select(RefreshToken).where(
            RefreshToken.token_hash == lookup_refresh_token(payload.refresh_token)
        )
    )
    if stored is None or not stored.is_valid(now):
        raise HTTPException(
            status.HTTP_401_UNAUTHORIZED, "Invalid, expired or revoked refresh token"
        )
    user = db.get(User, stored.user_id)
    if user is None:  # GDPR-deleted between issue and refresh
        raise HTTPException(status.HTTP_404_NOT_FOUND, "Account not found")

    stored.revoked_at = to_db_utc(now)  # rotate: single-use refresh tokens (§10)
    return _issue_tokens(db, user)
