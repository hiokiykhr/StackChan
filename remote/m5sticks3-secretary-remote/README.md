# M5StickS3 Hermes Hardware Buddy

Phase 1 firmware for a tiny Hermes hardware buddy on M5StickS3.

Related docs:
- `/Users/p00939/dev/stackchan/docs/m5sticks3-hermes-hardware-buddy.md`
- `/Users/p00939/dev/stackchan/docs/m5sticks3-secretary-remote-roadmap.md`
- `/Users/p00939/dev/stackchan/docs/m5sticks3-secretary-remote-phase1-spec.md`

## Goal

Provide a tiny physical Hermes companion that can quickly trigger a few fixed actions and show lightweight status on-screen.
This variant is now styled as a cute `Hermes-chan` buddy, inspired by the stateful character approach in CodeBuddy.

Phase 1 actions:
- `secretary.memo.capture`
- `secretary.reminder.quick_add`
- `secretary.task.next`
- `secretary.mode.home`

## Device behavior

- `BtnA`: move selection
- `BtnB`: send selected action
- top bar shows Hermes buddy status
- a cute on-screen `Hermes-chan` changes mood by state:
  - `sleep`: offline / Wi-Fi absent
  - `attention`: webhook missing but device awake
  - `idle`: ready and waiting
  - `busy`: sending request
  - `celebrate`: success
  - `heart`: warm `HOME` stand-by response
  - `error`: request failure
- result screen shows short Hermes feedback

If Wi-Fi or webhook is not configured yet:
- the device still boots
- `HOME` works as a local stand-by action
- other actions show setup hints instead of trying to send blindly

## Configuration

Set these in `idf.py menuconfig` under `Hermes Hardware Buddy Config`:
- Wi-Fi SSID
- Wi-Fi password
- webhook URL
- device ID
- firmware label
- HTTP timeout

## Build

```bash
cd /Users/p00939/dev/stackchan/remote/m5sticks3-secretary-remote
bash -lc 'source /Users/p00939/esp-idf-v5.5.4/export.sh >/dev/null && idf.py set-target esp32s3 && idf.py build'
```

## Flash

```bash
cd /Users/p00939/dev/stackchan/remote/m5sticks3-secretary-remote
bash -lc 'source /Users/p00939/esp-idf-v5.5.4/export.sh >/dev/null && idf.py -p /dev/cu.usbmodemXXXX flash'
```

## Webhook contract

Request body shape:

```json
{
  "device": {
    "id": "m5sticks3-hermes-hardware-buddy",
    "type": "M5StickS3",
    "firmware": "hardware-buddy-phase1"
  },
  "request": {
    "id": 1,
    "timestamp_ms": 1762290000000,
    "action": "secretary.task.next"
  },
  "context": {
    "battery": 84,
    "wifi_rssi": -59
  }
}
```

Response body shape:

```json
{
  "ok": true,
  "message": "受け取ったよ",
  "display": {
    "title": "NEXT",
    "body": "次は webhook 登録だよ"
  }
}
```

## Notes

- Phase 1 intentionally stays thin and fixed-action.
- Voice, push notifications, and persona animation are deferred.
- Runtime secrets should be supplied via `menuconfig`, not committed into source.
