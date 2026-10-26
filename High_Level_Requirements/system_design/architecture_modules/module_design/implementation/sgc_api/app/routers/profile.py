"""/v1/profile — F20 profile sync (profile lives on the phone in
SharedPreferences and mirrors to `users` on login / profile edits)."""

from fastapi import APIRouter, HTTPException, status
from sqlalchemy import select

from app.deps import DbDep, UserDep, WriteAllowedDep
from app.db import ensure_utc
from app.models import User
from app.schemas import ProfileResponse, ProfileUpdate

router = APIRouter(prefix="/v1/profile", tags=["profile"])


def _to_response(user: User) -> ProfileResponse:
    return ProfileResponse(
        id=user.id,
        name=user.name,
        age=user.age,
        category=user.category,
        user_type=user.user_type,
        club_id=user.club_id,
        activation_start_date=ensure_utc(user.activation_start_date)
        if user.activation_start_date else None,
        activation_end_date=ensure_utc(user.activation_end_date)
        if user.activation_end_date else None,
    )


@router.get("", response_model=ProfileResponse)
def get_profile(user: UserDep) -> ProfileResponse:
    return _to_response(user)


@router.put("", response_model=ProfileResponse)
def update_profile(
    payload: ProfileUpdate, user: WriteAllowedDep, db: DbDep
) -> ProfileResponse:
    """PUT is a write ⇒ activation-window gate applies (§5). Reads and
    deletion never do."""
    if payload.name is not None and payload.name != user.name:
        if db.scalar(select(User).where(User.name == payload.name)) is not None:
            raise HTTPException(status.HTTP_409_CONFLICT, "Name already registered")
        user.name = payload.name
    if payload.age is not None:
        user.age = payload.age
    if payload.category is not None:
        user.category = payload.category
    db.commit()
    db.refresh(user)
    return _to_response(user)
