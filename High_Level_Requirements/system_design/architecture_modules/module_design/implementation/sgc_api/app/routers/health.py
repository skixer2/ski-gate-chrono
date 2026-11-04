"""GET /healthz — liveness for Docker / reverse proxy (§4)."""

from fastapi import APIRouter

router = APIRouter(tags=["meta"])


@router.get("/healthz")
def healthz() -> dict[str, str]:
    # Deliberately trivial: it proves the process is up. A DB check would
    # couple liveness to storage and cause restart loops during WAL fsyncs.
    return {"status": "ok"}
