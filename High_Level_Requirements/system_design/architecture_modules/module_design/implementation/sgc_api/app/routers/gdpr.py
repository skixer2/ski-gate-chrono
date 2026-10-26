"""POST /v1/me/gdpr-delete — F50 hard deletion (§9).

One transaction: user row + runs + timestamps + barometric_data +
group/athlete links + refresh tokens — all cascading children of `users`.
§11.7 (clarified): v1 = FULL hard delete including the user row, so
everything 404s afterwards (the §9/F50 verification criterion).
"""

from fastapi import APIRouter, HTTPException, Response, status
from sqlalchemy import delete, select

from app.deps import DbDep, UserDep
from app.models import BarometricData, Run, Timestamp
from app.schemas import GDPRDeleteRequest
from app.security import RefreshToken

router = APIRouter(prefix="/v1/me", tags=["gdpr"])


@router.post("/gdpr-delete", status_code=status.HTTP_204_NO_CONTENT)
def gdpr_delete(
    payload: GDPRDeleteRequest,
    user: UserDep,
    db: DbDep,
) -> Response:
    if not payload.confirm:
        # §9 steps 2–3: the typed confirmation flag IS the consent.
        raise HTTPException(
            status.HTTP_400_BAD_REQUEST,
            {"code": "confirm_required",
             "detail": 'gdpr-delete requires {"confirm": true}'},
        )

    # Children first (explicit for review), then parents — one transaction,
    # committed atomically at the end. users2groups / users2athletes rows
    # cascade at the DB level via their FK ON DELETE CASCADE.
    run_ids = db.scalars(select(Run.id).where(Run.athlete_id == user.id)).all()
    if run_ids:
        db.execute(delete(Timestamp).where(Timestamp.run_id.in_(run_ids)))
        db.execute(delete(BarometricData).where(BarometricData.run_id.in_(run_ids)))
    db.execute(delete(Run).where(Run.athlete_id == user.id))
    db.execute(delete(RefreshToken).where(RefreshToken.user_id == user.id))

    db.delete(user)  # §11.7: full hard delete — re-registration stays possible
    db.commit()
    return Response(status_code=status.HTTP_204_NO_CONTENT)
