# SGC-API P0 — python:3.12-slim per remote_db_architecture.md §3.
# Non-root user (§10), app code read-only by design; only /data is writable.
FROM python:3.12-slim

# Run as an unprivileged user; SQLite file + WAL live on the /data volume.
RUN useradd --create-home --shell /usr/sbin/nologin appuser

WORKDIR /srv/sgc-api

# Install the package for real (deps + app + console entrypoints).
# NOTE: app/ and alembic/ must be present BEFORE `pip install .` —
# setuptools packages the source tree at install time, so installing
# from a directory containing only pyproject.toml ships an EMPTY
# package (bench 2026-10-26: alembic died with ModuleNotFoundError:
# app). Layer caching for deps sacrificed deliberately (P0 image).
COPY pyproject.toml alembic.ini ./
COPY app ./app
COPY alembic ./alembic
RUN pip install --no-cache-dir .

# SQLite home (named volume mounts here; seeded with this ownership on
# first mount — appuser must own it or sqlite3 fails 'unable to open').
RUN mkdir -p /data && chown appuser:appuser /data

USER appuser

ENV SGC_API_DATABASE_URL="sqlite:////data/sgc.db"

# Schema via Alembic, then serve. Container binds 0.0.0.0:8000 internally;
# docker-compose publishes it on 127.0.0.1:8000 on the host (proto posture).
CMD ["sh", "-c", "alembic upgrade head && uvicorn app.main:app --host 0.0.0.0 --port 8000"]
