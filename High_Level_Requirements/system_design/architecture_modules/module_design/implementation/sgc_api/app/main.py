"""FastAPI application factory — SGC remote-DB P0 (remote_db_architecture.md).

Route map (§4, all under /v1 except liveness + bootstrap):
    GET    /healthz                        liveness (Docker/proxy)
    GET    /bootstrap-info                 I07 bootstrap JSON
    POST   /v1/auth/register|login|refresh athlete/master self-onboarding (v1)
    GET/PUT/v1/profile                     F20 profile sync
    POST   /v1/runs                        F23 batch upload (one transaction)
    GET    /v1/runs                        own-run browse (F48 v1)
    GET    /v1/runs/{id}                   detail ?include=timestamps,barometric_data
    PUT    /v1/runs/{id}                   rename, If-Match lock_version (F45)
    DELETE /v1/runs/{id}                   F46 cloud leg, 204 cascade
    POST   /v1/me/gdpr-delete              F50 hard delete (§9)
"""

from fastapi import FastAPI

from app.routers import auth, bootstrap, gdpr, health, profile, runs


def create_app() -> FastAPI:
    app = FastAPI(
        title="SGC API",
        version="0.1.0",
        summary="Ski Gate Chrono remote database (P0 prototype)",
        # Docs (OpenAPI/Swagger/ReDoc) stay on for JP review (§3).
    )
    app.include_router(health.router)
    app.include_router(bootstrap.router)
    app.include_router(auth.router)
    app.include_router(profile.router)
    app.include_router(runs.router)
    app.include_router(gdpr.router)
    return app


app = create_app()
