# M5StickS3 秘書リモコン Phase 1 仕様

最終更新: 2026-07-01 01:26:22 JST

> For Hermes: この文書は Phase 1 MVP の webhook 契約、M5StickS3 ボタン/UI 割り当て、最小実装方針を固定するための仕様書である。

## Goal

M5StickS3 から、スマホを開かずに Hermes 系の受け口へ最低3種類の定型アクションを送信し、結果を画面で確認できるようにする。

## 対象

- デバイス: M5StickS3
- firmware: 新規 standalone ESP-IDF app
- 通信: Wi-Fi + HTTP POST
- 受け口: Hermes webhook route または同等の HTTP adapter

## Phase 1 でやること

- 3アクション送信
- 成功/失敗表示
- `task.next` だけ短い本文表示
- ボタン2個で完結する操作

## Phase 1 でやらないこと

- 音声入力
- 自由文入力
- 常時 push 通知
- 顔アニメーション
- 複雑な設定 UI

---

## Webhook 契約

### エンドポイント

- Method: `POST`
- URL: `http(s)://<host>/webhooks/<name>` または同等 endpoint
- Content-Type: `application/json`

Hermes 本体の現行 CLI では webhook route 作成に `hermes webhook subscribe <name>` を使う前提とする。
ただし Phase 1 の device 契約自体は、Hermes 直結でも外部 adapter 経由でも同一とする。

### Request schema

```json
{
  "device": {
    "id": "m5sticks3-secretary-remote",
    "type": "M5StickS3",
    "firmware": "phase1-mvp"
  },
  "request": {
    "id": 12,
    "timestamp_ms": 1762290000000,
    "action": "secretary.task.next"
  },
  "context": {
    "battery": 87,
    "wifi_rssi": -58
  }
}
```

### Request field rules

- `device.id`: 端末識別子。複数台対応を見据えて string 固定
- `device.type`: `M5StickS3`
- `device.firmware`: 将来の互換性確認用。初期値は `phase1-mvp`
- `request.id`: 端末内の単調増加整数
- `request.timestamp_ms`: device 側の時刻 ms。SNTP 同期済みなら UNIX epoch ms、未同期なら boot-relative ms を暫定送信してよい
- `request.action`: 下記3種のいずれか
- `context.battery`: 0-100
- `context.wifi_rssi`: 未取得時は `0`

### Action vocabulary

Phase 1 は以下の3つに固定する。

1. `secretary.memo.capture`
   - 用途: 「思いつきメモを残したい」の起点
2. `secretary.reminder.quick_add`
   - 用途: 「あとでやる」「リマインド化したい」の起点
3. `secretary.task.next`
   - 用途: 今やる次タスク1件を短く返してもらう

### Response schema

```json
{
  "ok": true,
  "message": "Next task fetched",
  "display": {
    "title": "NEXT",
    "body": "StackChan の OTA URL を確認"
  }
}
```

### Response field rules

- `ok`: 必須 bool
- `message`: 必須 string。短く 1 行向け
- `display.title`: 任意 string。未指定時は device 側で action 名を表示
- `display.body`: 任意 string。`task.next` では返すことを推奨

### HTTP status expectations

- `200`: 正常処理。body を読む
- `4xx`: 契約違反/認証/未許可。device は `HTTP 4xx` と message を表示
- `5xx`: サーバ失敗。device は `HTTP 5xx` を表示
- timeout / connection error: `NETWORK ERROR`

### Device 側の表示ルール

- `ok=true`: success 色/文言で表示
- `ok=false`: error 色/文言で表示
- `display.body` があれば 2-4 行で本文表示
- `display.body` がなければ `message` のみ表示

---

## M5StickS3 UI / ボタン割り当て

### 画面構成

初版は 1 画面 + 実行中/結果表示のみ。

#### Home
- タイトル: `SECRETARY`
- Wi-Fi 状態: `WIFI OK` / `WIFI NG`
- メニュー3件
  - `MEMO`
  - `REMIND`
  - `NEXT`
- フッター
  - `A:MOVE`
  - `B:SEND`

#### Busy
- `Sending...`
- action 名

#### Result
- `OK` または `NG`
- `message`
- 必要なら `display.body`

### ボタン割り当て

Phase 1 は学習コストを下げるため、役割を固定する。

- `BtnA`: 選択カーソルを次へ進める
- `BtnB`: 現在選択中 action を送信する

補助仕様:
- Home 画面でしか選択変更しない
- 送信中はボタン入力を無視する
- Result 表示後 2 秒で Home に戻る

### 選択遷移

```text
MEMO -> REMIND -> NEXT -> MEMO ...
```

### 初期選択

- 起動直後は `MEMO`

---

## MVP 実装方針

### app 構成

新規 app を以下に置く。

- `/Users/p00939/dev/stackchan/remote/m5sticks3-secretary-remote/`

### 設定方式

ビルド時 `menuconfig` で以下を設定する。

- Wi-Fi SSID
- Wi-Fi password
- webhook URL
- device ID
- HTTP timeout

### Phase 1 の実装順

1. NVS / Wi-Fi 初期化
2. M5Unified 初期化
3. Home 画面描画
4. ボタンで menu 選択
5. HTTP POST 送信
6. JSON response 解析
7. Busy / Result 表示

### 成功判定

- build が通る
- M5StickS3 で menu 3件が見える
- `BtnA` で選択が動く
- `BtnB` で POST が飛ぶ
- 結果が画面に出る

---

## Hermes 側 adapter の期待動作

Phase 1 では device 側を薄く保つため、Hermes 側で action を解釈する。

推奨ルール:
- `secretary.memo.capture`
  - Hermes へ「M5StickS3 から思いつきメモ起点イベントが来た。短く受理して、必要なら follow-up を返す」
- `secretary.reminder.quick_add`
  - Hermes へ「M5StickS3 から quick reminder 起点イベントが来た。短く受理して返す」
- `secretary.task.next`
  - Hermes へ「次にやる1タスクを 1-2 行で返す」

### `task.next` の推奨レスポンス例

```json
{
  "ok": true,
  "message": "Next task fetched",
  "display": {
    "title": "NEXT",
    "body": "M5StickS3 webhook URL を Hermes 側に登録"
  }
}
```

### `memo.capture` / `reminder.quick_add` の推奨レスポンス例

```json
{
  "ok": true,
  "message": "Memo flow accepted"
}
```

---

## Open questions

- Hermes の webhook route だけで十分か、薄い HTTP adapter を別に置くか
- `reminder.quick_add` を将来ワンボタン定型文付きにするか
- `task.next` の本文最大文字数を何文字に制限するか
- Wi-Fi 設定 UI を device 側に入れるか、menuconfig 固定で始めるか

## 次の実装対象

- standalone app を新規作成
- build を通す
- README に build/flash/config 方法を書く
