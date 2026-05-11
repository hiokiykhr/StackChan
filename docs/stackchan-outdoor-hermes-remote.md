# StackChan 屋外 HERMES Agent 接続設計

最終更新: 2026-05-07 JST
対象:
- firmware: `/Users/p00939/dev/stackchan/firmware`
- backend: `/Users/p00939/dev/xiaozhi-esp32-server/main/xiaozhi-server`

## 目的

StackChan を自宅 LAN だけでなく、iPhone テザリングなどの屋外 Wi-Fi 経由でも HERMES Agent backend へ接続できるようにする。

要件:
1. Wi-Fi 設定を複数保持する。
2. 保存順を優先順位として扱い、高優先度 SSID が見つからない/接続できない場合に低優先度 SSID へ順次フォールバックする。
3. 屋外から backend へ接続する場合は、平文公開や無制限 token minting を避ける。
4. 既存の OTA / WebSocket / XiaoZhi 互換 protocol を壊さない。

## 実装済み firmware 変更

### 優先順位付き SSID フォールバック

対象ファイル:

`/Users/p00939/dev/stackchan/firmware/managed_components/78__esp-wifi-connect/wifi_station.cc`

変更点:
- 従来は scan 結果を RSSI 順に並べ、保存済み SSID と一致した AP へ接続していた。
- そのため、iPhone テザリングの電波が強いと、自宅 SSID より先に iPhone 側へ接続する可能性があった。
- 変更後は `SsidManager::GetSsidList()` の保存順を接続優先順位として扱う。
- 同じ SSID に複数 BSSID がある場合だけ、その SSID 内で RSSI が最も強い BSSID を選ぶ。

接続順の意味:

```text
ssid       -> priority 0, highest
ssid1      -> priority 1
ssid2      -> priority 2
...
ssid9      -> priority 9, lowest
```

既存の Wi-Fi config AP には `/saved/set_default?index=N` があり、保存済み SSID を先頭に移動できる。新規追加 SSID は既存実装どおり先頭へ追加される。

推奨の保存順:

1. 自宅/作業場など通常使う安定 Wi-Fi
2. iPhone テザリング SSID
3. 予備 SSID

屋外で iPhone を使う場合は、自宅 SSID が scan に出ないので、次の優先 SSID である iPhone テザリングへ自動接続する。

## 実装済み backend 変更

対象ファイル:

- `/Users/p00939/dev/xiaozhi-esp32-server/main/xiaozhi-server/core/websocket_server.py`
- `/Users/p00939/dev/xiaozhi-esp32-server/main/xiaozhi-server/core/api/ota_handler.py`
- `/Users/p00939/dev/xiaozhi-esp32-server/main/xiaozhi-server/config.yaml`
- `/Users/p00939/dev/xiaozhi-esp32-server/main/xiaozhi-server/test/test_secure_remote_auth.py`

### 公開運用向け auth semantics

既存挙動:
- `server.auth.enabled=true` でも、`allowed_devices` に入っている device-id は WebSocket token 検証をスキップしていた。
- OTA endpoint は、`allowed_devices` が空の場合、任意の device-id/client-id に token を発行できる状態だった。

今回の追加:

```yaml
server:
  auth:
    enabled: true
    allowed_devices:
      - "<StackChan device-id>"
    skip_token_for_allowed_devices: false
    expire_seconds: 604800
```

`skip_token_for_allowed_devices: false` の意味:
- `allowed_devices` に含まれる device-id だけを許可する。
- 許可済み device-id でも WebSocket token 検証を必須にする。
- OTA は許可済み device-id にだけ短期 token を返す。
- 未許可 device-id からの OTA request は HTTP 403 を返す。

互換性:
- default は `skip_token_for_allowed_devices: true` のままにしてあり、LAN 既存運用は壊さない。
- 屋外/公開トンネル運用では必ず `false` にする。

## セキュアなリモート接続の推奨構成

推奨は Cloudflare Tunnel / Tailscale Funnel / HTTPS reverse proxy などで、TLS 終端済みの public URL を用意する構成。

### 推奨 URL 例

実値は環境側で決める。ドキュメントには秘密情報や実 token は書かない。

```text
OTA:       https://stackchan.example.com/xiaozhi/ota/
WebSocket: wss://stackchan.example.com/xiaozhi/v1/
Vision:    https://stackchan.example.com/mcp/vision/explain
```

### backend `data/.config.yaml` 例

`config.yaml` ではなく `data/.config.yaml` に置く。

```yaml
server:
  websocket: wss://stackchan.example.com/xiaozhi/v1/
  vision_explain: https://stackchan.example.com/mcp/vision/explain
  auth_key: "<32文字以上のランダム値>"
  auth:
    enabled: true
    allowed_devices:
      - "<StackChan device-id>"
    skip_token_for_allowed_devices: false
    expire_seconds: 604800
```

注意:
- `auth_key` はリポジトリに commit しない。
- `allowed_devices` には StackChan 実機の device-id だけを入れる。
- `allowed_devices: []` のまま公開しない。任意 device-id に token を発行する構成になりうる。
- `skip_token_for_allowed_devices: true` のまま公開しない。device-id のみで通ってしまう。

### tunnel / reverse proxy の経路

backend は内部で以下を listen する。

```text
WebSocket server: 0.0.0.0:8000
HTTP/OTA server: 0.0.0.0:8003
```

公開側では path ごとに振り分ける。

```text
https://stackchan.example.com/xiaozhi/ota/*          -> http://localhost:8003/xiaozhi/ota/*
https://stackchan.example.com/xiaozhi/ota/download/* -> http://localhost:8003/xiaozhi/ota/download/*
wss://stackchan.example.com/xiaozhi/v1/*             -> ws://localhost:8000/xiaozhi/v1/*
https://stackchan.example.com/mcp/vision/explain      -> http://localhost:8003/mcp/vision/explain
```

Cloudflare Tunnel なら WebSocket proxy を有効にする。nginx 等なら `Upgrade` / `Connection` header を通す。

## firmware 側 OTA URL

StackChan は起動時 OTA check URL から WebSocket URL/token を受け取る。

屋外でも使う場合は、firmware の Wi-Fi advanced 設定 `ota_url` または build-time `CONFIG_OTA_URL` を public HTTPS OTA URL にする。

例:

```text
https://stackchan.example.com/xiaozhi/ota/
```

既存の自宅 LAN 専用設定:

```text
http://mm0939.local:8003/xiaozhi/ota/
```

は、iPhone テザリング経由では名前解決/到達できない可能性が高い。

## iPhone テザリング登録手順

1. iPhone 側で「インターネット共有」を ON にする。
2. StackChan を Wi-Fi config mode に入れる。
3. `Xiaozhi-xxxx` AP に iPhone/PC から接続し、`http://192.168.4.1` を開く。
4. iPhone テザリング SSID と password を登録する。
5. saved list で優先順位を確認する。
   - 自宅 Wi-Fi を priority 0
   - iPhone テザリングを priority 1
6. advanced 設定で `ota_url` を public HTTPS OTA URL にする。
7. config mode を終了して再接続する。

## 検証ポイント

firmware monitor で確認するログ:

```text
Found priority AP[0]: <home ssid> ...
Found priority AP[1]: <iPhone tethering ssid> ...
WiFi connecting to <ssid>
Got IP: ...
Connecting to websocket server: wss://...
```

屋外/iPhone テザリング時:
- 自宅 SSID が scan に存在しなければ iPhone SSID へ進む。
- OTA で返る WebSocket URL が `wss://...` になっている。
- WebSocket 接続時に `Authorization: Bearer ***` が付く。
- backend 側で未許可 device-id の OTA が 403 になる。

## 2026-05-08 検証結果

実行時刻: 2026-05-08 01:32 JST

firmware build:

```bash
cd /Users/p00939/dev/stackchan/firmware
bash -lc 'source /Users/p00939/esp-idf-v5.5.4/export.sh >/dev/null && idf.py build'
```

結果: 成功。`stack-chan.bin` 生成、app binary size `0x39e2f0`、smallest app partition `0x4f0000`、free `0x151d10`。

flash:

```bash
idf.py -p /dev/tty.usbmodem21101 flash
```

結果: 2026-05-08 01:54 JST に成功。ESP32-S3 を検出し、bootloader / app / partition table / ota data / generated assets の書き込みと hash verify が完了。MAC はログに出るが、このドキュメントには保持しない。

backend tests:

```bash
cd /Users/p00939/dev/xiaozhi-esp32-server/main/xiaozhi-server
.venv/bin/python -m pytest test/test_secure_remote_auth.py test/test_companion_connection_hooks.py test/test_companion_event_validator.py
```

結果: 成功、`29 passed`。

secure remote config validator:

```bash
.venv/bin/python scripts/validate_secure_remote_config.py /tmp/stackchan-valid-remote-config.yaml
```

結果: 成功、`Secure remote config: OK`。

## 残作業

- 実 public domain / tunnel URL を決める。
- `data/.config.yaml` に実 URL と auth_key を入れる。
- StackChan の advanced `ota_url` を public HTTPS OTA URL に更新する。
- iPhone テザリング SSID を実機登録する。
- StackChan を USB 接続した状態で monitor し、優先フォールバックを実機確認する。
