"""GET /bootstrap-info — I07 bootstrap indirection (§3).

The app hardcodes THIS address; the JSON tells it where the real API lives.
One indirection so the API can move host/domain without an app release.
In production a static file on the bootstrap host mirrors this payload.
"""

from fastapi import APIRouter

from app.config import get_settings

router = APIRouter(tags=["meta"])


@router.get("/bootstrap-info")
def bootstrap_info() -> dict[str, str]:
    cfg = get_settings()
    return {"api_base": cfg.api_base, "min_app_version": cfg.min_app_version}
