"""ORM models — the 10 REQ-DB tables (sgc_requirements.md §REQ-DB, v6.3).

Field names are copied EXACTLY from the REQ-DB table spec. Do not rename.

v1-active subset (remote_db_architecture.md §1/§12): ski_clubs (window
enforcement), users (auth/profile), runs + timestamps + barometric_data
(F23 payload), refresh_tokens (security.py). groups, users2groups,
users2athletes, courses, course_gates exist from day one so P2/P3 are
additive — the API does not touch them yet (§4: courses stub in v1).

Schema additions sanctioned by remote_db_architecture.md (flagged inline):
  * users.password_hash        — §5 / §11.2 (JP accepted 2026-10-26)
  * users.club_id              — implied by the REQ-DB diagram
                                 "ski_clubs ──< users"; needed for the club
                                 activation-window check (§5). Nullable:
                                 v1 self-registration may have no club.
  * runs.client_run_uuid       — §4 idempotency key (F43 offline queue
                                 re-sends the same upload).

Every child FK uses ON DELETE CASCADE so run deletion (F46 cloud leg),
GDPR hard-delete (§9) and the nightly retention purge (§5) are single-row
deletes at the parent.
"""

from datetime import datetime

from sqlalchemy import (
    Boolean,
    CheckConstraint,
    DateTime,
    Float,
    ForeignKey,
    Integer,
    Text,
    func,
)
from sqlalchemy.orm import Mapped, mapped_column, relationship

from app.db import Base

# Portable timestamptz alias (§2 guardrail 3): timezone-aware UTC everywhere.
DateTimeTZ = DateTime(timezone=True)

# Enum-ish values live as TEXT + CHECK constraints (§2 guardrail: portable,
# no dialect-specific ENUM types). Vocabularies per REQ-DB notes.
USER_TYPES = ("athlete", "trainer", "parent", "friend", "master")
VISIBILITIES = ("full", "athlete_only", "denied")
GROUP_ROLES = ("trainer", "athlete")
RELATIONSHIPS = ("trainer", "parent", "friend")
ARM_SIDES = ("L", "R")


class SkiClub(Base):
    """ski_clubs — club license window."""

    __tablename__ = "ski_clubs"

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    name: Mapped[str] = mapped_column(Text, nullable=False, unique=True)
    activation_start_date: Mapped[datetime | None] = mapped_column(
        DateTimeTZ, nullable=True
    )
    activation_end_date: Mapped[datetime | None] = mapped_column(
        DateTimeTZ, nullable=True
    )

    users: Mapped[list["User"]] = relationship(
        back_populates="club", passive_deletes=True
    )


class User(Base):
    """users — cloud account. Login identifier = name (unique).

    REQ-DB has no credential field; password_hash is the §11.2 addition.
    The activation window (§5, JP-decided) gates WRITES only — expired
    users keep read + delete rights so they can always erase their data.
    """

    __tablename__ = "users"

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    name: Mapped[str] = mapped_column(Text, nullable=False, unique=True)
    age: Mapped[int | None] = mapped_column(Integer, nullable=True)
    category: Mapped[str | None] = mapped_column(Text, nullable=True)
    activation_start_date: Mapped[datetime | None] = mapped_column(
        DateTimeTZ, nullable=True
    )
    activation_end_date: Mapped[datetime | None] = mapped_column(
        DateTimeTZ, nullable=True
    )
    user_type: Mapped[str] = mapped_column(Text, nullable=False)
    password_hash: Mapped[str] = mapped_column(Text, nullable=False)  # §11.2
    club_id: Mapped[int | None] = mapped_column(
        ForeignKey("ski_clubs.id", ondelete="SET NULL"), nullable=True
    )

    __table_args__ = (
        CheckConstraint(
            "user_type IN ('athlete','trainer','parent','friend','master')",
            name="ck_users_user_type",
        ),
    )

    club: Mapped[SkiClub | None] = relationship(back_populates="users")
    runs: Mapped[list["Run"]] = relationship(
        back_populates="athlete",
        # GDPR delete + account removal rely on the DB-level cascade.
        passive_deletes=True,
        cascade="all, delete-orphan",
    )


class Group(Base):
    """groups — inter-club training groups (no FK to ski_clubs). P2."""

    __tablename__ = "groups"  # quoted by the ORM where needed (GROUP keyword)

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    name: Mapped[str] = mapped_column(Text, nullable=False)
    activation_start_date: Mapped[datetime | None] = mapped_column(
        DateTimeTZ, nullable=True
    )
    activation_end_date: Mapped[datetime | None] = mapped_column(
        DateTimeTZ, nullable=True
    )
    default_visibility: Mapped[str] = mapped_column(
        Text, nullable=False, default="full"
    )

    __table_args__ = (
        CheckConstraint(
            "default_visibility IN ('full','athlete_only','denied')",
            name="ck_groups_visibility",
        ),
    )


class User2Group(Base):
    """users2groups — many-to-many user ↔ group. Only group trainers may
    add/remove members (REQ-DB). P2."""

    __tablename__ = "users2groups"

    user_id: Mapped[int] = mapped_column(
        ForeignKey("users.id", ondelete="CASCADE"), primary_key=True
    )
    group_id: Mapped[int] = mapped_column(
        ForeignKey("groups.id", ondelete="CASCADE"), primary_key=True
    )
    role: Mapped[str] = mapped_column(Text, nullable=False)

    __table_args__ = (
        CheckConstraint("role IN ('trainer','athlete')", name="ck_u2g_role"),
    )


class User2Athlete(Base):
    """users2athletes — many-to-many self-ref join: follower → athlete.
    A kid can have many followers; a follower many kids. P2."""

    __tablename__ = "users2athletes"

    user_id: Mapped[int] = mapped_column(
        ForeignKey("users.id", ondelete="CASCADE"), primary_key=True
    )
    athlete_id: Mapped[int] = mapped_column(
        ForeignKey("users.id", ondelete="CASCADE"), primary_key=True
    )
    relationship: Mapped[str] = mapped_column(Text, nullable=False)

    __table_args__ = (
        CheckConstraint(
            "relationship IN ('trainer','parent','friend')",
            name="ck_u2a_relationship",
        ),
    )


class Run(Base):
    """runs — one gate-timing run. Data rows (timestamps/barometric_data)
    are immutable post-upload; every write bumps lock_version (optimistic
    concurrency, If-Match on the client, 409 on mismatch)."""

    __tablename__ = "runs"

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    start_datetime: Mapped[datetime] = mapped_column(DateTimeTZ, nullable=False)
    athlete_id: Mapped[int] = mapped_column(
        ForeignKey("users.id", ondelete="CASCADE"), nullable=False
    )
    # Daily phone-set counter; informational label only — NO server-side
    # uniqueness (§11.8: phone deletions can legitimately renumber).
    run_number: Mapped[int] = mapped_column(Integer, nullable=False)
    visibility: Mapped[str | None] = mapped_column(Text, nullable=True)
    # User-editable label (nullable; phone defaults to date + run_number).
    name: Mapped[str | None] = mapped_column(Text, nullable=True)
    # uint8: bumped by the phone when the bit-packing format changes.
    format_version: Mapped[int] = mapped_column(Integer, nullable=False)
    # uint32: server-set. 1 on create, +1 on every write.
    lock_version: Mapped[int] = mapped_column(Integer, nullable=False, default=1)
    # §11.10: always NULL in v1 (course auto-match needs P2 groups).
    course_id: Mapped[int | None] = mapped_column(
        ForeignKey("courses.id", ondelete="SET NULL"), nullable=True
    )
    # §4 idempotency key: client-generated UUID; a retry after a lost ACK
    # returns the existing run instead of duplicating it (F43).
    client_run_uuid: Mapped[str] = mapped_column(Text, nullable=False, unique=True)

    __table_args__ = (
        CheckConstraint(
            "visibility IN ('full','athlete_only','denied') OR visibility IS NULL",
            name="ck_runs_visibility",
        ),
        CheckConstraint(
            "format_version BETWEEN 0 AND 255", name="ck_runs_format_version"
        ),
        CheckConstraint(
            "lock_version BETWEEN 0 AND 4294967295", name="ck_runs_lock_version"
        ),
    )

    athlete: Mapped[User] = relationship(back_populates="runs")
    timestamps: Mapped[list["Timestamp"]] = relationship(
        back_populates="run",
        passive_deletes=True,
        cascade="all, delete-orphan",
        order_by="Timestamp.timestamp_ms",
    )
    barometric_data: Mapped[list["BarometricData"]] = relationship(
        back_populates="run",
        passive_deletes=True,
        cascade="all, delete-orphan",
        order_by="BarometricData.time_offset_ms",
    )


class Timestamp(Base):
    """timestamps — gate strikes relative to T=0. guessed=true = estimated
    from orientation data (no impact detected), shown with '*' on the phone."""

    __tablename__ = "timestamps"

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    timestamp_ms: Mapped[int] = mapped_column(Integer, nullable=False)
    arm_side: Mapped[str] = mapped_column(Text, nullable=False)
    run_id: Mapped[int] = mapped_column(
        ForeignKey("runs.id", ondelete="CASCADE"), nullable=False
    )
    guessed: Mapped[bool] = mapped_column(
        Boolean, nullable=False, default=False
    )

    __table_args__ = (
        CheckConstraint("arm_side IN ('L','R')", name="ck_timestamps_arm_side"),
    )

    run: Mapped[Run] = relationship(back_populates="timestamps")


class BarometricData(Base):
    """barometric_data — 10 Hz altitude + vertical speed (one row per 100 ms),
    derived on the phone from pressure (F30). Raw 100 Hz data never leaves
    the phone (F23)."""

    __tablename__ = "barometric_data"

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    run_id: Mapped[int] = mapped_column(
        ForeignKey("runs.id", ondelete="CASCADE"), nullable=False
    )
    time_offset_ms: Mapped[int] = mapped_column(Integer, nullable=False)
    altitude_m: Mapped[float] = mapped_column(Float, nullable=False)
    speed_mps: Mapped[float] = mapped_column(Float, nullable=False)

    run: Mapped[Run] = relationship(back_populates="barometric_data")


class Course(Base):
    """courses — course metadata. Created by a trainer/master. API-stubbed
    in v1 (§4); tables exist so P3 is additive."""

    __tablename__ = "courses"

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    name: Mapped[str] = mapped_column(Text, nullable=False)
    created_by: Mapped[int] = mapped_column(
        ForeignKey("users.id", ondelete="CASCADE"), nullable=False
    )
    created_at: Mapped[datetime] = mapped_column(
        DateTimeTZ, nullable=False, server_default=func.now()
    )

    gates: Mapped[list["CourseGate"]] = relationship(
        back_populates="course", passive_deletes=True, cascade="all, delete-orphan"
    )


class CourseGate(Base):
    """course_gates — delta-based gate position (F57.1–F57.4, AD-007):
    START at index 0 with ΔP=0; pressure deltas from START pressure (Pa);
    GPS as relative vectors from the previous gate. v1: no tag_id/NFC."""

    __tablename__ = "course_gates"

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    course_id: Mapped[int] = mapped_column(
        ForeignKey("courses.id", ondelete="CASCADE"), nullable=False
    )
    gate_number: Mapped[int] = mapped_column(Integer, nullable=False)
    delta_p: Mapped[float] = mapped_column(Float, nullable=False)
    delta_lat: Mapped[float] = mapped_column(Float, nullable=False)
    delta_lon: Mapped[float] = mapped_column(Float, nullable=False)
    altitude: Mapped[float] = mapped_column(Float, nullable=False)
    set_by: Mapped[int] = mapped_column(
        ForeignKey("users.id", ondelete="CASCADE"), nullable=False
    )

    course: Mapped[Course] = relationship(back_populates="gates")
