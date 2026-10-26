"""Pydantic v2 request/response schemas (API contract layer).

Field names mirror the REQ-DB tables 1:1 so the phone-side sync worker and
the ORM speak the same vocabulary. Validation lives here, not in routers.
"""

from datetime import datetime
from typing import Literal

from pydantic import BaseModel, ConfigDict, Field, field_validator

# --- Auth ----------------------------------------------------------------------


class RegisterRequest(BaseModel):
    """POST /v1/auth/register — v1: athlete/master self-registration only
    (§4). trainer/parent/friend onboarding is P2 (relationship rules)."""

    name: str = Field(min_length=1, max_length=100)
    password: str = Field(min_length=8, max_length=128)
    user_type: Literal["athlete", "master"]
    age: int | None = Field(default=None, ge=0, le=130)
    category: str | None = Field(default=None, max_length=100)
    club_id: int | None = None  # optional; club activation window (§5)


class LoginRequest(BaseModel):
    name: str
    password: str


class RefreshRequest(BaseModel):
    refresh_token: str


class TokenResponse(BaseModel):
    access_token: str
    refresh_token: str
    token_type: Literal["bearer"] = "bearer"
    expires_in: int  # access-token lifetime in seconds


# --- Profile ---------------------------------------------------------------------


class ProfileResponse(BaseModel):
    """GET /v1/profile — F20 profile fields that live on `users`.
    push_to_cloud (F20 toggle) is phone-local state, NOT a DB field."""

    id: int
    name: str
    age: int | None
    category: str | None
    user_type: str
    club_id: int | None
    activation_start_date: datetime | None
    activation_end_date: datetime | None


class ProfileUpdate(BaseModel):
    """PUT /v1/profile — all fields optional; only provided fields change."""

    name: str | None = Field(default=None, min_length=1, max_length=100)
    age: int | None = Field(default=None, ge=0, le=130)
    category: str | None = Field(default=None, max_length=100)


# --- Runs --------------------------------------------------------------------------


class TimestampIn(BaseModel):
    timestamp_ms: int = Field(ge=0)
    arm_side: Literal["L", "R"]
    guessed: bool = False  # true = estimated from orientation data (F27)


class BarometricDataIn(BaseModel):
    time_offset_ms: int = Field(ge=0)
    altitude_m: float
    speed_mps: float


class RunCreate(BaseModel):
    """POST /v1/runs — the whole F23 batch payload in one request:
    run metadata + timestamps[] + barometric_data[], committed in ONE
    transaction. course_id must be null in v1 (§11.10)."""

    client_run_uuid: str = Field(min_length=8, max_length=64)
    start_datetime: datetime  # must be timezone-aware (UTC)
    run_number: int = Field(ge=0)  # informational label, no uniqueness (§11.8)
    format_version: int = Field(ge=0, le=255)  # uint8 (bit-packing version)
    name: str | None = Field(default=None, max_length=200)
    visibility: Literal["full", "athlete_only", "denied"] | None = None
    course_id: int | None = None
    timestamps: list[TimestampIn] = Field(default_factory=list)
    barometric_data: list[BarometricDataIn] = Field(default_factory=list)

    @field_validator("start_datetime")
    @classmethod
    def _require_tz(cls, v: datetime) -> datetime:
        if v.tzinfo is None:
            raise ValueError("start_datetime must be timezone-aware (UTC)")
        return v

    @field_validator("course_id")
    @classmethod
    def _course_id_null_in_v1(cls, v: int | None) -> int | None:
        # §11.10 (JP, accepted): v1 always uploads course_id = null; the
        # auto-match (±2h, same group via trainer) activates with P2 groups.
        if v is not None:
            raise ValueError("course_id assignment arrives with P3; must be null in v1")
        return v


class TimestampOut(TimestampIn):
    id: int

    model_config = ConfigDict(from_attributes=True)  # validate ORM rows


class BarometricDataOut(BarometricDataIn):
    id: int

    model_config = ConfigDict(from_attributes=True)


class RunOut(BaseModel):
    """Run representation. `already_exists` is set only on the idempotent-
    retry path (200, same client_run_uuid) — 201 create leaves it null."""

    model_config = ConfigDict(from_attributes=True)

    id: int
    client_run_uuid: str
    start_datetime: datetime
    athlete_id: int
    run_number: int
    visibility: str | None
    name: str | None
    format_version: int
    lock_version: int
    course_id: int | None
    already_exists: bool | None = None


class RunDetailOut(RunOut):
    """GET /v1/runs/{id}?include=timestamps,barometric_data (F48 viewer /
    comparison rendering, F22/F32/F34)."""

    timestamps: list[TimestampOut] = Field(default_factory=list)
    barometric_data: list[BarometricDataOut] = Field(default_factory=list)


class RunRename(BaseModel):
    """PUT /v1/runs/{id} — rename (F45). Visibility override is P2 (F36)."""

    name: str = Field(min_length=1, max_length=200)


class RunListOut(BaseModel):
    items: list[RunOut]
    next_cursor: str | None  # opaque keyset cursor (last id); null = end


class RunCreatedOut(BaseModel):
    """POST /v1/runs response. already_exists=true (HTTP 200) marks the
    idempotent-retry path; fresh creates are HTTP 201 with false."""

    id: int
    lock_version: int
    already_exists: bool = False


# --- GDPR ----------------------------------------------------------------------


class GDPRDeleteRequest(BaseModel):
    """POST /v1/me/gdpr-delete — §9: typed confirmation flag is mandatory."""

    confirm: bool
