"""Unit tests for the GameStream signaling server (main.py).

Run:
  cd server
  pytest tests/ -v
"""

from collections.abc import AsyncIterator, Iterator
from pathlib import Path

import pytest
from httpx import ASGITransport, AsyncClient

import main

app = main.app

# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------

VALID_SESSION_ID = "550e8400-e29b-41d4-a716-446655440000"
INVALID_SESSION_IDS = [
    "not-a-uuid",
    "550e8400e29b41d4a716446655440000",  # no dashes
    "550e8400-e29b-11d4-a716-446655440000",  # version 1, not 4
    "550e8400-e29b-41d4-c716-446655440000",  # variant bits invalid
    # Note: empty string "" is not tested here — an empty path segment causes FastAPI
    # to not match the route at all (405), before our _validate_session_id runs.
]

OFFER_BODY = {"type": "offer", "sdp": "v=0\r\no=- 0 0 IN IP4 127.0.0.1\r\n"}
ANSWER_BODY = {"type": "answer", "sdp": "v=0\r\no=- 1 1 IN IP4 127.0.0.1\r\n"}


@pytest.fixture
def anyio_backend() -> str:
    return "asyncio"


@pytest.fixture(autouse=True)
def reset_sessions() -> None:
    main._sessions.clear()


@pytest.fixture
async def client() -> AsyncIterator[AsyncClient]:
    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as c:
        yield c


@pytest.fixture
def stats_log_path(
    monkeypatch: pytest.MonkeyPatch,
) -> Iterator[Path]:
    path = Path(__file__).resolve().parent / "client_stats.log"
    if path.exists():
        path.unlink()
    monkeypatch.setattr("main._CLIENT_STATS_PATH", path)
    yield path
    if path.exists():
        path.unlink()


# ---------------------------------------------------------------------------
# session_id validation
# ---------------------------------------------------------------------------

@pytest.mark.anyio
@pytest.mark.parametrize("bad_id", INVALID_SESSION_IDS)
async def test_post_offer_rejects_invalid_session_id(
    client: AsyncClient, bad_id: str
) -> None:
    resp = await client.post(f"/api/session/{bad_id}/offer", json=OFFER_BODY)
    assert resp.status_code == 400


@pytest.mark.anyio
async def test_get_offer_rejects_invalid_session_id(client: AsyncClient) -> None:
    resp = await client.get("/api/session/not-a-uuid/offer")
    assert resp.status_code == 400


@pytest.mark.anyio
async def test_post_answer_rejects_invalid_session_id(client: AsyncClient) -> None:
    resp = await client.post("/api/session/not-a-uuid/answer", json=ANSWER_BODY)
    assert resp.status_code == 400


@pytest.mark.anyio
async def test_get_answer_rejects_invalid_session_id(client: AsyncClient) -> None:
    resp = await client.get("/api/session/not-a-uuid/answer")
    assert resp.status_code == 400


# ---------------------------------------------------------------------------
# offer / answer round-trip
# ---------------------------------------------------------------------------

@pytest.mark.anyio
async def test_offer_round_trip(client: AsyncClient) -> None:
    post = await client.post(
        f"/api/session/{VALID_SESSION_ID}/offer", json=OFFER_BODY
    )
    assert post.status_code == 200
    assert post.json() == {"status": "ok"}

    get = await client.get(f"/api/session/{VALID_SESSION_ID}/offer")
    assert get.status_code == 200
    data = get.json()
    assert data["type"] == "offer"
    assert "sdp" in data


@pytest.mark.anyio
async def test_answer_not_ready_returns_404(client: AsyncClient) -> None:
    sid = "660e8400-e29b-41d4-a716-446655440001"
    # First register session via offer so it exists
    await client.post(f"/api/session/{sid}/offer", json=OFFER_BODY)
    resp = await client.get(f"/api/session/{sid}/answer")
    assert resp.status_code == 404


@pytest.mark.anyio
async def test_answer_round_trip(client: AsyncClient) -> None:
    sid = "770e8400-e29b-41d4-a716-446655440002"
    await client.post(f"/api/session/{sid}/offer", json=OFFER_BODY)

    post = await client.post(f"/api/session/{sid}/answer", json=ANSWER_BODY)
    assert post.status_code == 200

    get = await client.get(f"/api/session/{sid}/answer")
    assert get.status_code == 200
    assert get.json()["type"] == "answer"


# ---------------------------------------------------------------------------
# request body validation
# ---------------------------------------------------------------------------

@pytest.mark.anyio
async def test_post_offer_missing_sdp_field(client: AsyncClient) -> None:
    resp = await client.post(
        f"/api/session/{VALID_SESSION_ID}/offer", json={"type": "offer"}
    )
    assert resp.status_code == 400


@pytest.mark.anyio
async def test_post_answer_missing_type_field(client: AsyncClient) -> None:
    resp = await client.post(
        f"/api/session/{VALID_SESSION_ID}/answer", json={"sdp": "v=0\r\n"}
    )
    assert resp.status_code == 400


# ---------------------------------------------------------------------------
# client stats endpoint
# ---------------------------------------------------------------------------

@pytest.mark.anyio
async def test_post_client_stats_rejects_invalid_session_id(
    client: AsyncClient,
) -> None:
    resp = await client.post(
        "/api/client/stats",
        json={"session_id": "test-session", "inbound": {"fps": 60}},
    )
    assert resp.status_code == 400


@pytest.mark.anyio
async def test_post_client_stats_rejects_missing_session_id(
    client: AsyncClient,
) -> None:
    resp = await client.post(
        "/api/client/stats",
        json={"inbound": {"fps": 60}},
    )
    assert resp.status_code == 400


@pytest.mark.anyio
async def test_post_client_stats_accepts_valid_uuid4(
    client: AsyncClient, stats_log_path: Path
) -> None:
    payload = {
        "session_id": VALID_SESSION_ID,
        "inbound": {"fps": 60},
    }
    resp = await client.post("/api/client/stats", json=payload)
    assert resp.status_code == 200
    assert resp.json() == {"status": "ok"}
    assert stats_log_path.exists()


@pytest.mark.anyio
async def test_expired_sessions_are_purged_on_access(
    client: AsyncClient
) -> None:
    old_sid = "880e8400-e29b-41d4-a716-446655440003"
    new_sid = "990e8400-e29b-41d4-a716-446655440004"

    post = await client.post(f"/api/session/{old_sid}/offer", json=OFFER_BODY)
    assert post.status_code == 200
    assert old_sid in main._sessions

    main._sessions[old_sid].created_at -= (main._SESSION_TTL_SECONDS + 1.0)

    # Triggers _get_session() -> _purge_expired_sessions()
    get = await client.get(f"/api/session/{new_sid}/answer")
    assert get.status_code == 404
    assert old_sid not in main._sessions
