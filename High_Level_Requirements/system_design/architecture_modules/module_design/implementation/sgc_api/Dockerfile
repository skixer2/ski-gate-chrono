# SGC-API P0 — python:3.12-slim per remote_db_architecture.md §3.
# Non-root user (§10), app code read-only by design; only /data is writable.
FROM python:3.12-slim

# Run as an unprivileged user; SQLite file + WAL live on the /data volume.
RUN useradd --create-home --shell /usr/sbin/nologin appuser

WORKDIR /srv/sgc-api

# Install deps first for layer caching.
COPY pyproject.toml ./
COPY app ./app
RUN pip install --no-cache-dir .

USER appuser

ENV SGC_API_DATABASE_URL="sqlite:////data/sgc.db"

# Schema via Alembic, then serve. Container binds 0.0.0.0:8000 internally;
# docker-compose publishes it on 127.0.0.1:8000 on the host (proto posture).
CMD ["sh", "-c", "alembic upgrade head && uvicorn app.main:app --host 0.0.0.0 --port 8000"]
