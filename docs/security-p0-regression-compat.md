# P0 hardening compatibility follow-up

This note documents compatibility fixes added after the scoped P0 hardening merge.

## Upload directory compatibility

The P0 upload hardening introduced an allowlist for upload destinations. To avoid breaking existing clients and documented examples, the server accepts these legacy aliases and maps them to canonical safe directories:

| Client value | Canonical directory |
| --- | --- |
| `moments` | `dance` |
| `posts` | `post` |
| `apps` | `app` |

New clients should prefer the canonical directory names where possible. Existing clients may keep sending the legacy values during migration.

## WebSocket idle compatibility

The P0 hardening added a read deadline to WebSocket connections. StackChan clients use application-level binary heartbeat/messages in addition to WebSocket control frames, so the server now refreshes the read deadline after every successful message read from both StackChan and App clients. This prevents valid long-lived sessions from timing out when no WebSocket control pong is observed.

## M5StickS3 remote timestamp behavior

The Hermes Buddy webhook validates request timestamps. The remote now refuses to send actions until SNTP time has synced, instead of sending boot-relative timestamps that the webhook rejects as stale or invalid.

## Server smoke-test startup

The built admin console under `server/web/management` is optional for API/server smoke tests. The server now only calls `SetServerRoot("web/management")` when that directory exists, so API routes and `/file/...` can be smoke-tested from a clean checkout without prebuilt web assets.

## Remaining non-goals

This follow-up does not complete P1 items such as IDOR authorization, HTTPS/WSS enforcement, Hermes-only egress isolation, cloud removal, OTA supply-chain security, or device credential redesign. Those remain tracked separately.
