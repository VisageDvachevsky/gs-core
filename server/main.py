"""GameStream — Stage 3 WebRTC signaling server.

REST API (for C++ host and browser test.html):
  POST /api/session/{id}/offer   — C++ host submits offer SDP
  GET  /api/session/{id}/offer   — browser fetches offer (long-poll, 30 s)
  POST /api/session/{id}/answer  — browser submits answer SDP
  GET  /api/session/{id}/answer  — C++ host polls for answer (404 if not ready)

Static files:
  GET /test.html                 — minimal browser WebRTC client

Run:
  cd server
  uvicorn main:app --host 0.0.0.0 --port 8765 --reload
"""

import asyncio
import json
import logging
import re
import time
from dataclasses import dataclass, field
from pathlib import Path

from fastapi import FastAPI, HTTPException, Request
from fastapi.responses import JSONResponse
from fastapi.staticfiles import StaticFiles

logging.basicConfig(level=logging.DEBUG, format="%(asctime)s [%(levelname)s] %(message)s")
log = logging.getLogger("gamestream")

app = FastAPI(title="GameStream Signaling", version="0.1.0")
_CLIENT_STATS_PATH = Path(__file__).resolve().parent / "client_stats.log"

# ---------------------------------------------------------------------------
# Session validation
# ---------------------------------------------------------------------------

# UUIDv4: 8-4-4-4-12 hex digits, version nibble = 4, variant nibble = 8/9/a/b
_UUID4_RE = re.compile(
    r"^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$",
    re.IGNORECASE,
)

# Sessions older than this are eligible for cleanup
_SESSION_TTL_SECONDS: float = 3600.0  # 1 hour


def _validate_session_id(session_id: str) -> None:
    if not _UUID4_RE.match(session_id):
        raise HTTPException(status_code=400, detail="session_id must be a UUIDv4")


# ---------------------------------------------------------------------------
# Session state
# ---------------------------------------------------------------------------

@dataclass
class Session:
    offer:        dict[str, str] | None = None
    answer:       dict[str, str] | None = None
    offer_event:  asyncio.Event = field(default_factory=asyncio.Event)
    answer_event: asyncio.Event = field(default_factory=asyncio.Event)
    created_at:   float = field(default_factory=time.monotonic)


_sessions: dict[str, Session] = {}


def _purge_expired_sessions() -> None:
    """Remove sessions older than _SESSION_TTL_SECONDS to bound memory growth."""
    now = time.monotonic()
    expired = [sid for sid, s in _sessions.items()
               if (now - s.created_at) > _SESSION_TTL_SECONDS]
    for sid in expired:
        log.info("Session %s: expired, purging", sid)
        del _sessions[sid]


def _get_session(session_id: str) -> Session:
    _purge_expired_sessions()
    session = _sessions.get(session_id)
    if session is None:
        session = Session()
        _sessions[session_id] = session
        log.debug("New session: %s", session_id)
    return session


# ---------------------------------------------------------------------------
# REST endpoints
# ---------------------------------------------------------------------------

@app.post("/api/session/{session_id}/offer")
async def post_offer(session_id: str, request: Request) -> dict[str, str]:
    """C++ host submits an SDP offer.  Resets the session if called again."""
    _validate_session_id(session_id)
    body = await request.json()
    if "sdp" not in body or "type" not in body:
        raise HTTPException(status_code=400, detail="Body must contain 'type' and 'sdp'")

    # Reset session so a new test run can reuse the same session_id.
    s = _sessions.get(session_id)
    if s is not None and s.offer is not None:
        log.info("Session %s: resetting for new offer", session_id)
        _sessions[session_id] = Session()
    s = _get_session(session_id)

    s.offer = {"type": body["type"], "sdp": body["sdp"]}
    s.offer_event.set()
    log.info("Session %s: offer received (%d bytes SDP)", session_id, len(body["sdp"]))
    return {"status": "ok"}


@app.get("/api/session/{session_id}/offer")
async def get_offer(session_id: str) -> JSONResponse:
    """Browser long-polls for the host offer (waits up to 30 s)."""
    _validate_session_id(session_id)
    s = _get_session(session_id)
    try:
        await asyncio.wait_for(s.offer_event.wait(), timeout=30.0)
    except TimeoutError as exc:
        raise HTTPException(
            status_code=408, detail="Offer not available within 30 s"
        ) from exc
    return JSONResponse(s.offer)


@app.post("/api/session/{session_id}/answer")
async def post_answer(session_id: str, request: Request) -> dict[str, str]:
    """Browser submits its SDP answer."""
    _validate_session_id(session_id)
    body = await request.json()
    if "sdp" not in body or "type" not in body:
        raise HTTPException(status_code=400, detail="Body must contain 'type' and 'sdp'")

    s = _get_session(session_id)
    s.answer = {"type": body["type"], "sdp": body["sdp"]}
    s.answer_event.set()
    log.info("Session %s: answer received (%d bytes SDP)", session_id, len(body["sdp"]))
    return {"status": "ok"}


@app.get("/api/session/{session_id}/answer")
async def get_answer(session_id: str) -> JSONResponse:
    """C++ host polls for the browser answer.  Returns 404 if not ready yet."""
    _validate_session_id(session_id)
    s = _get_session(session_id)
    if s.answer is None:
        raise HTTPException(status_code=404, detail="Answer not ready")
    return JSONResponse(s.answer)


@app.post("/api/client/stats")
async def post_client_stats(request: Request) -> dict[str, str]:
    """Browser submits periodic stats snapshots (JSONL)."""
    try:
        body = await request.json()
    except json.JSONDecodeError as exc:
        raise HTTPException(status_code=400, detail=f"invalid JSON: {exc}") from exc

    if not isinstance(body, dict):
        raise HTTPException(status_code=400, detail="JSON body must be an object")

    session_id = body.get("session_id")
    if not isinstance(session_id, str) or not session_id:
        raise HTTPException(status_code=400, detail="session_id is required and must be a UUIDv4")
    _validate_session_id(session_id)

    record = {
        "ts": time.time(),
        "session_id": session_id,
        "data": body,
    }
    try:
        with _CLIENT_STATS_PATH.open("a", encoding="utf-8") as f:
            f.write(json.dumps(record, ensure_ascii=False) + "\n")
    except OSError as exc:
        log.warning("Failed to write client stats: %s", exc)
        raise HTTPException(status_code=500, detail="failed to write stats") from exc
    return {"status": "ok"}


# ---------------------------------------------------------------------------
# Static files — serve test.html at /test.html
# ---------------------------------------------------------------------------

# Mount AFTER API routes so FastAPI processes /api/* first.
app.mount("/", StaticFiles(directory="static", html=True), name="static")
