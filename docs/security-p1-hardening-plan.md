# P1 Security Hardening Plan

This document plans the next security-hardening pull request after the P0 fixes in PR #1. It is intentionally planning-only and does not change runtime behavior.

## Scope

P1 covers authorization design, transport protection, Hermes-only build isolation, and outbound-network reduction that are larger than the focused P0 patch.

## Goals

1. Prevent device/account takeover and IDOR across device-bound resources.
2. Require protected transport for public deployments.
3. Provide a Hermes-only build flavor that excludes XiaoZhi, EzData, M5Stack MQTT, Tenclass OTA, China-IP defaults, and related UI/routes where possible.
4. Move from RSA-encrypted bearer material to authenticated device credentials.
5. Add egress deny-by-default controls and artifact verification.

## Workstreams

### 1. Device bind takeover

Current risk:

- Device bind and update flows rely on identifiers that may be guessable or replayable.
- Existing device ownership transitions need explicit authorization and audit semantics.

Planned controls:

- Introduce owner-scoped authorization checks before bind, unbind, update, and restore-default operations.
- Require one-time bind challenge or physical-presence code for first bind.
- Reject binding if the device is already owned unless the current owner authorizes transfer.
- Add audit logs for bind, unbind, transfer, and reset events.
- Add tests for attacker user attempting to bind another user's device.

Acceptance criteria:

- A user cannot bind or mutate a device already bound to another user.
- Rebinding requires explicit transfer flow or admin-approved reset.
- Regression tests cover successful owner flow and rejected non-owner flow.

### 2. dance/post/Agent reset IDOR

Current risk:

- Resource IDs for dance, post, comments, pano, and Agent reset operations may be used without verifying that the authenticated principal owns the target resource or device.

Planned controls:

- Add shared authorization helper for `uid`, `mac`, `device_id`, and resource ownership.
- Apply helper to dance CRUD, post/comment CRUD, pano operations, and Agent reset endpoints.
- Return 404 or 403 consistently without leaking cross-user resource existence.
- Add table-driven tests for owner, non-owner, missing-auth, and deleted-resource cases.

Acceptance criteria:

- Non-owner requests cannot read, update, delete, or reset another user's resources.
- Existing owner clients continue to work with valid tokens.

### 3. WebSocket arbitrary MAC forwarding

Current risk:

- App frames can include a target MAC in payload and forward data if that MAC is online.
- This may allow an authenticated app client to send media/control frames to a device it does not own.

Planned controls:

- Bind each WebSocket connection to the authenticated principal and authorized device list.
- Before forwarding Opus/JPEG/control frames to a MAC, verify ownership or active pairing/session state.
- Consider replacing payload MAC routing with server-side session mapping.
- Add tests for cross-MAC forwarding rejection.

Acceptance criteria:

- A client authenticated for MAC A cannot send Opus/JPEG/control frames to MAC B.
- Call/subscription state is checked server-side, not trusted from payload alone.

### 4. HTTPS/WSS enforcement

Current risk:

- The server and clients can be configured for HTTP/WS, exposing credentials and media to network observers.

Planned controls:

- Add deployment config flag `ALLOW_INSECURE_LOCAL_TRANSPORT` default false except loopback/development.
- Reject public `http://` and `ws://` endpoints in app/server/firmware config.
- Support reverse-proxy headers (`X-Forwarded-Proto`) only from trusted proxy ranges.
- Add documentation and tests for local dev, LAN, and public deployments.

Acceptance criteria:

- Public deployment refuses insecure HTTP/WS unless explicitly marked local-only.
- Firmware/app examples use HTTPS/WSS.

### 5. Hermes-only build flavor

Current risk:

- Operationally using Hermes is insufficient while XiaoZhi/EzData/M5Stack MQTT/Tenclass code remains compiled and routable.

Planned controls:

- Add build/config flag `HERMES_ONLY` for server, firmware, and remote tools.
- In Hermes-only builds, compile out or disable XiaoZhi API routes/controllers/models, EzData, M5Stack MQTT, Tenclass OTA, and M5Stack external login/registration flows.
- Fail build if Hermes-only artifacts contain forbidden domains or China-IP defaults.
- Add CI job to build Hermes-only flavor and run string/domain checks.

Acceptance criteria:

- Hermes-only artifacts contain no reachable XiaoZhi/EzData/MQTT/Tenclass endpoints.
- Server route table excludes XiaoZhi routes in Hermes-only mode.

### 6. XiaoZhi/EzData/M5Stack MQTT/Tenclass/China-IP defaults removal

Current known residuals to remove or gate:

- `firmware/main/Kconfig.projbuild`: China IP and Tenclass defaults.
- `firmware/main/hal/hal_ezdata.cpp`: `ezdata2.m5stack.com`, `uiflow2.m5stack.com:1883`.
- `server/internal/controller/dance/dance_v2_get_list.go`: China IP default.
- `server/internal/xiaozhi/xiaozhi.go`: `xiaozhi.me`.

Planned controls:

- Replace external defaults with empty values in Hermes-only mode.
- Require explicit opt-in for cloud integrations.
- Add `scripts/check_forbidden_endpoints.sh` for source and final artifact scans.

Acceptance criteria:

- Source scan and artifact scan fail on forbidden domains/IPs for Hermes-only builds.
- Cloud integrations cannot be enabled accidentally by defaults.

### 7. Egress deny-by-default

Current risk:

- Even if routes are unused, runtime may still call external services through scheduled refresh, OTA, MQTT, or token refresh paths.

Planned controls:

- Centralize outbound HTTP/MQTT/WebSocket creation behind an allowlist policy.
- Default allowlist for Hermes-only: loopback, configured Hermes relay, configured OpenAI/API gateway if explicitly required.
- Add runtime logging for blocked egress without logging secrets.
- Add integration tests with a fake transport to verify blocked hosts.

Acceptance criteria:

- New outbound destinations require explicit configuration and tests.
- Hermes-only mode blocks all non-allowlisted egress.

### 8. Permanent RSA device auth replacement

Current risk:

- RSA encryption alone does not prove device identity. If encrypted token material is replayed or generated by any party with enough context, authentication is weak.

Planned replacement options:

- Preferred: per-device HMAC credentials with key ID, nonce, timestamp, audience, and replay cache.
- Stronger option: mutual TLS for devices capable of certificate provisioning.
- Transitional option: challenge-response over existing RSA channel while migrating clients.

Required token fields for HMAC option:

- `kid`
- `mac` or stable device ID
- `aud` (`rest`, `websocket`, or `webhook`)
- `iat`
- `nonce`
- `method`
- `path`
- request body hash for state-changing REST calls
- HMAC-SHA256 signature

Migration plan:

1. Add server-side credential table and key ID support.
2. Accept old RSA token only when `ALLOW_LEGACY_RSA_DEVICE_AUTH=1`.
3. Issue or provision per-device HMAC secret during owner-approved bind.
4. Update firmware/app clients to sign REST and WebSocket handshakes.
5. Add replay cache with bounded memory and persistent nonce window if needed.
6. Remove legacy RSA auth after migration window.

Acceptance criteria:

- Replay of a captured request outside nonce/window is rejected.
- REST and WebSocket tokens are audience-separated.
- Legacy RSA auth is opt-in and emits deprecation warnings.

## Test plan

- Go: `gofmt`, `go test ./...`, `go test -race ./...`, `go vet ./...`, `govulncheck ./...`.
- Firmware: Hermes-only and normal flavor builds.
- Python: Hermes Buddy webhook tests.
- Security checks: forbidden endpoint grep, artifact `strings`, secret scan, egress fake-transport tests.

## PR sequencing

1. Ownership/IDOR authorization helpers and tests.
2. WebSocket MAC-forwarding authorization.
3. HTTPS/WSS enforcement and config migration docs.
4. Hermes-only build flavor and forbidden endpoint checks.
5. Egress policy centralization.
6. RSA-to-HMAC/mTLS device-auth migration.
