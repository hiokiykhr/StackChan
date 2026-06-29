# StackChan Hermes / Companion 改造ワークログ

最終更新: 2026-05-19 09:45 JST
対象リポジトリ: `/Users/p00939/dev/stackchan`
関連バックエンド: `/Users/p00939/dev/xiaozhi-esp32-server`
実機ポート: `/dev/tty.usbmodem21201`（monitor は `/dev/cu.usbmodem21201` も可）
ESP-IDF: `/Users/p00939/esp-idf-v5.5.4/export.sh`

## 目的

StackChan を XiaoZhi 互換プロトコルのまま Hermes / くろー用の身体として使えるようにする。
会話、NFC、owner_lite / sensitive_unlocked、交通系IC残高照会、HERMES音声/口パクの実機品質を段階的に改善する。

## 今日までに実装・検証した成果物

### 1. メインメニュー名の変更

変更:
- `AI.AGENT` を `XIAOZHI` に変更。

対象:
- `firmware/main/apps/app_ai_agent/app_ai_agent.cpp`

検証:
- `idf.py build` 成功。
- `idf.py -p /dev/tty.usbmodem21201 flash` 成功。

### 2. HERMES モード app の追加/連携

変更:
- `firmware/main/apps/app_hermes/` を追加。
- StackChan firmware 側から XiaoZhi / Hermes 互換 application を起動できる流れを維持。

対象:
- `firmware/main/apps/app_hermes/`
- `firmware/main/apps/apps.h`
- `firmware/main/main.cpp`

注意:
- 既存 XiaoZhi 互換 transport を壊さず、backend 側 WebSocket / OTA と接続する方針。
- OTA URL は `http://mm0939.local:8003/xiaozhi/ota/`、WebSocket は `ws://mm0939.local:8000/xiaozhi/v1/` を想定。

### 3. NFC / FeliCa / 交通系IC残高照会

変更:
- Setup 配下に `IC Balance` worker を追加。
- M5Unit-NFC / ST25R3916 / FeliCa の初期化・polling・Read Without Encryption 経路を ESP-IDF 実装へ移植。
- 交通系IC残高表示を glyph 安全な ASCII 表示に変更。

対象:
- `firmware/main/apps/app_setup/app_setup.cpp`
- `firmware/main/apps/app_setup/workers/workers.h`
- `firmware/main/apps/app_setup/workers/balance.cpp`
- `firmware/main/hal/nfc/nfc_service.h`
- `firmware/main/hal/nfc/nfc_service.cc`

重要な実装メモ:
- FeliCa polling system code は 16bit として扱い、送信は big-endian。
- 試行順は `0x0003`, `0x80DE`, `0xFFFF`。
- ST25R3916 の FeliCa 212kbps 設定は `0x11`。
- `I_nre` は `0x40`。
- `REG_TX_DRIVER` は `0x80`。
- NFC-A の RF 状態を引きずらないよう、FeliCa 前に `CMD_STOP_ALL_ACTIVITIES` と `tx_en|rx_en` clear を行う。
- field settle delay: STOP/TX/RX clear 後 2ms、field on 後 25ms。
- polling transceive timeout: 50ms。
- 残高 decoding は公式 M5Unit-NFC `JapanTransportationICCard.cpp` 準拠。
  - service: `0x008B`
  - block: `0`
  - block offset: `11-12`
  - endian: little-endian
  - 式: `balance = block[11] | (block[12] << 8)`

実機検証:
- 11円カード: `raw_balance=0b00` から 11円として読めることを確認。
- 302円カード: 期待 raw は `2e01`、表示は `Balance: 302 yen`。
- アンテナ位置がシビア。カードは頭部/上部に密着させ、1〜2秒保持が安定。

既知の残課題:
- `polling failed (bitrate=212k, system=FFFF)` はカード位置/タイミングによって初回に出ることがある。
- 直後の再試行で成功するケースを確認済み。

### 4. Companion / owner auth / NFC event 連携

変更:
- firmware から companion event を送る経路を追加。
- NFC検出時に `nfc_auth_detected` / `nfc_auth_removed` を送る。
- payload には互換用 `card_id_hash` / `card_label_hint` に加えて、明示的な `card_idm` を追加。
- IDm / hash は個人識別に近いため、ドキュメントや共有ログでは実値を書かない。

対象:
- `firmware/main/hal/nfc/nfc_service.cc`
- `firmware/xiaozhi-esp32/main/application.h`
- `firmware/xiaozhi-esp32/main/application.cc`
- `firmware/main/hal/hal_head_touch.cpp`
- `firmware/main/hal/board/hal_bridge.cc`

backend 側の対応:
- `/Users/p00939/dev/xiaozhi-esp32-server/docs/stackchan-hermes-companion-worklog-2026-05-07.md` を参照。

### 5. HERMES 音声途切れ・口パク同期ズレ対策

調査ログで見えた問題:
- backend から来る音声 sample rate は 16kHz。
- 実機 output sample rate が 24kHz だったため、firmware 側で resampling が走っていた。
- 以前の警告:
  - `Server sample rate 16000 does not match device output sample rate 24000, resampling may cause distortion`
- 再生キューが 2 frame で、60ms frame 換算だと約120msしか余裕がなかった。
- 口パクが `tts.start` / `kDeviceStateSpeaking` の論理状態に同期していて、実音声再生より先走っていた。

変更:
- audio input/output sample rate を 16kHz に統一。
- playback queue depth を 2 から 6 へ拡張。
- `SPEAKING` 状態だけでは `SpeakingModifier` を起動しないようにし、`sentence_start` 受信時に専用 status `__stackchan_lipsync_speaking` を投げて口パク開始する interim 修正を入れた。

対象:
- `firmware/main/hal/board/config.h`
- `firmware/xiaozhi-esp32/main/audio/audio_service.h`
- `firmware/main/hal/board/stackchan_display.cc`
- `firmware/xiaozhi-esp32/main/application.cc`

検証:
- `idf.py build` 成功。
- `idf.py -p /dev/tty.usbmodem21201 flash` 成功。
- post-flash 起動ログで `sample_rate_hz: 16000` を確認。
- 上記 sample-rate mismatch 警告は起動ログ上では出なくなった。

既知の残課題:
- HERMES会話時に `TCP receive failed: -1` / `Websocket disconnected` がまだ出ていた。
- 音声バッファ/口パクより前段の transport / Wi-Fi power save / WebSocket lifecycle が残課題。
- 次回は「ブツブツ途切れる」のか「会話チャンネル自体が落ちる」のかを実機で切り分ける。

## 検証コマンド

firmware build:

```bash
cd /Users/p00939/dev/stackchan/firmware
bash -lc 'source /Users/p00939/esp-idf-v5.5.4/export.sh >/dev/null && idf.py build'
```

firmware flash:

```bash
cd /Users/p00939/dev/stackchan/firmware
bash -lc 'source /Users/p00939/esp-idf-v5.5.4/export.sh >/dev/null && idf.py -p /dev/tty.usbmodem21201 flash'
```

serial port check:

```bash
lsof /dev/tty.usbmodem21201 /dev/cu.usbmodem21201 2>/dev/null || true
```

monitor:

```bash
cd /Users/p00939/dev/stackchan/firmware
bash -lc 'source /Users/p00939/esp-idf-v5.5.4/export.sh >/dev/null && idf.py -p /dev/tty.usbmodem21201 monitor'
```

非TTY環境で monitor が失敗する場合:
- `idf.py monitor` は `Monitor requires standard input to be attached to TTY` で失敗することがある。
- Hermes agent runtime では PTY 指定、または raw serial reader を使う。

## 次回の優先タスク

1. 最新ファームで HERMES モードを実機体感確認。
   - 音声途切れが改善したか。
   - 口パク先走りが改善したか。
2. まだ途切れる場合は WebSocket 切断原因を追う。
   - `TCP receive failed: -1`
   - `Websocket disconnected`
   - Wi-Fi power save が会話中に `LOW_POWER` へ戻っていないか。
3. transport が安定したら、口パク開始を `sentence_start` ではなく実PCM再生開始/再生中 buffer level へさらに近づける。
4. IC残高照会の 302円カード確認。
5. おひろちゃんモード用 NFC 登録を backend 再起動後に実機確認。

## プロダクトバックログ

- M5StickS3 を StackChan のカメラファインダーとして活用する。
  - 想定ユースケース: StackChan本体カメラの見えている範囲を手元で確認する。
  - 検討観点: 映像転送方式、UI/操作系、給電/バッテリー、StackChan本体との通信方式、屋内/屋外運用時のレイテンシと安定性。

## 注意事項

- `lv_font_montserrat_24` は日本語や `¥` を表示できないため、実機 UI 文言は ASCII に寄せる。
- IDm / UID / hash の実値は共有ドキュメントに残さない。
- Cisco Skill Scanner の安全方針により、Critical/High/Medium 判定の skill は原則使わない。
- 既存 XiaoZhi 互換 OTA / WebSocket / live toolcall path を壊さないこと。
