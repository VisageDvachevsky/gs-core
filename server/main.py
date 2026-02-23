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
import logging
from dataclasses import dataclass, field

from fastapi import FastAPI, HTTPException, Request
from fastapi.responses import JSONResponse
from fastapi.staticfiles import StaticFiles

logging.basicConfig(level=logging.DEBUG, format="%(asctime)s [%(levelname)s] %(message)s")
log = logging.getLogger("gamestream")

app = FastAPI(title="GameStream Signaling", version="0.1.0")


# ---------------------------------------------------------------------------
# Session state
# ---------------------------------------------------------------------------

@dataclass
class Session:
    offer:        dict | None = None
    answer:       dict | None = None
    offer_event:  asyncio.Event = field(default_factory=asyncio.Event)
    answer_event: asyncio.Event = field(default_factory=asyncio.Event)


_sessions: dict[str, Session] = {}


def _get_session(session_id: str) -> Session:
    if session_id not in _sessions:
        _sessions[session_id] = Session()
        log.debug("New session: %s", session_id)
    return _sessions[session_id]


# ---------------------------------------------------------------------------
# REST endpoints
# ---------------------------------------------------------------------------

@app.post("/api/session/{session_id}/offer")
async def post_offer(session_id: str, request: Request) -> dict:
    """C++ host submits an SDP offer.  Resets the session if called again."""
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
    s = _get_session(session_id)
    try:
        await asyncio.wait_for(s.offer_event.wait(), timeout=30.0)
    except asyncio.TimeoutError:
        raise HTTPException(status_code=408, detail="Offer not available within 30 s")
    return JSONResponse(s.offer)


@app.post("/api/session/{session_id}/answer")
async def post_answer(session_id: str, request: Request) -> dict:
    """Browser submits its SDP answer."""
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
    s = _get_session(session_id)
    if s.answer is None:
        raise HTTPException(status_code=404, detail="Answer not ready")
    return JSONResponse(s.answer)


# ---------------------------------------------------------------------------
# Static files — serve test.html at /test.html
# ---------------------------------------------------------------------------

# Mount AFTER API routes so FastAPI processes /api/* first.
app.mount("/", StaticFiles(directory="static", html=True), name="static")
