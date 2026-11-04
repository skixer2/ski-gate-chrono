"""Shared FastAPI dependencies: DB session, current user, write gate."""

from typing import Annotated

from datetime import datetime, timezone
from fastapi import Depends, HTTPException, status
from fastapi.security import HTTPAuthorizationCredentials, HTTPBearer
from sqlalchemy import select
from sqlalchemy.orm import Session

from app.db import get_db, ensure_utc
from app.models import SkiClub, User
from app.security import decode_access_token

# Bearer-only; missing/malformed Authorization header ⇒ 401 by the scheme.
_bearer = HTTPBearer(auto_error=False)

DbDep = Annotated[Session, Depends(get_db)]


def get_current_user(
    db: DbDep,
    credentials: Annotated[HTTPAuthorizationCredentials | None, Depends(_bearer)],
) -> User:
    """Authenticate via JWT, then re-load the user row.

    §5: the JWT carries user_type, but "the DB is truth" — user_type and the
    activation window are re-checked against the DB on every request, so a
    demoted/expired/deleted account takes effect immediately.

    404 (not 401) when the subject row is gone: §9/F50 requires every
    authenticated endpoint to 404 after GDPR deletion, which also
    neutralises still-unexpired access tokens (≤60 min residual).
    """
    if credentials is None:
        raise HTTPException(status.HTTP_401_UNAUTHORIZED, "Not authenticated")
    claims = decode_access_token(credentials.credentials)
    if claims is None or claims.get("sub") is None:
        raise HTTPException(status.HTTP_401_UNAUTHORIZED, "Invalid or expired token")

    user = db.scalar(select(User).where(User.id == int(claims["sub"])))
    if user is None:
        # GDPR-hard-deleted account (§9 step 5: everything 404s afterwards).
        raise HTTPException(status.HTTP_404_NOT_FOUND, "Account not found")
    return user


UserDep = Annotated[User, Depends(get_current_user)]


def _window_active(start: datetime | None, end: datetime | None, now: datetime) -> bool:
    """An open-ended window (null bound) counts as active; outside the
    window (not yet started or already expired) counts as inactive.
    ensure_utc: SQLite reads stored datetimes back naive (§db.py)."""
    if start is not None and now < ensure_utc(start):
        return False
    if end is not None and now > ensure_utc(end):
        return False
    return True


def require_write_allowed(user: UserDep) -> User:
    """Activation-window gate for WRITE endpoints (§5, JP-decided 2026-10-26):

        expired ⇒ writes blocked (403); reads + deletion stay allowed,
        so an expired user can always still erase their data.

    Attach to POST/PUT routes only — never to GET or DELETE.
    """
    now = datetime.now(timezone.utc)
    if not _window_active(user.activation_start_date, user.activation_end_date, now):
        raise HTTPException(
            status.HTTP_403_FORBIDDEN,
            {"code": "activation_expired", "detail": "Account activation expired"},
        )
    club: SkiClub | None = user.club
    if club is not None and not _window_active(
        club.activation_start_date, club.activation_end_date, now
    ):
        raise HTTPException(
            status.HTTP_403_FORBIDDEN,
            {"code": "club_expired", "detail": "Club license window expired"},
        )
    return user


WriteAllowedDep = Annotated[User, Depends(require_write_allowed)]
