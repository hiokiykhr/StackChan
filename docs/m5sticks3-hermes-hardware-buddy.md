# M5StickS3 Hermes Hardware Buddy 構想

最終更新: 2026-07-01 16:34:13 JST

> For Hermes: この文書は、余った M5StickS3 を Claude Hardware Buddy 的な「Hermes Hardware Buddy」として再定義し、今ある秘書リモコンMVPをその方向へ発展させるための設計メモである。

## Goal

M5StickS3 を、単なる webhook リモコンではなく、
- Hermes をすぐ呼べる
- Hermes の状態がわかる
- 生活フローの切り替えを支援する
- 使うほど相棒感が増す
小型の物理 buddy 端末にする。

## 一言でいうと

Hermes Hardware Buddy は、
「机や持ち歩きで使える、Hermes 専用の物理インターフェース」である。

CLI や Discord や iPhone を開かずに、
- すぐ投げる
- すぐ見る
- すぐ思い出す
を実現する。

## Claude Hardware Buddy 的にほしい価値

### 1. Instant summon
Hermes を“開く”のではなく“呼ぶ”。
- ボタンを押す
- すぐ1機能に入る
- 待たされない

### 2. Ambient awareness
Hermes の状態が常時うっすら見える。
- 今日は何かあるか
- 次タスクはあるか
- 未処理が詰まっているか
- 今 busy か

### 3. Physical ritual
行動切り替えの儀式に使える。
- 今から集中
- これをあとで
- これを覚えて
- 今の次やることを出して

### 4. Companion feeling
道具で終わらず、存在感がある。
- 表情
- 短い一言
- 状態に応じた反応
- 押したときの返事

---

## おひろさん向けの勝ち筋

おひろさん文脈では、M5StickS3 を以下の役割にすると強い。

### A. 脳の退避先
- 思いつきを逃さない
- 「あとでやる」を詰める
- スマホを開く前に退避できる

### B. 行動切り替えスイッチ
- 今から集中
- 休憩する
- 帰宅した
- 次の1個を出す

### C. 小さな Hermes の顔
- 今は idle か
- 通知があるか
- 今日やることが残っているか
- ちょっと気にかけてくれている感じがあるか

---

## これを満たす MVP の再定義

今ある「秘書リモコンMVP」を、Hardware Buddy MVP として言い換える。

### Hardware Buddy Phase 1
主役は still「入力」だが、表示も少し buddy 化する。

#### 最低限ほしい機能
1. Quick Capture
   - `MEMO`: 思いつきの退避起点
2. Quick Remind
   - `REMIND`: あとでやる起点
3. Next Focus
   - `NEXT`: 次の1タスクを返す
4. Presence
   - Wi-Fi 状態
   - Hermes 応答成功/失敗
   - 一言メッセージ

#### 追加したい buddy 要素
- 送信成功時に毎回同じ表示ではなく、短い返答を返す
  - `OK: 受け取ったよ`
  - `NEXT: OTA URL を確認して`
  - `LATER: あとで扱うね`
- 画面上部に小さな状態帯を置く
  - `IDLE`
  - `BUSY`
  - `TODO 3`
  - `TODAY 1`

---

## 理想の Hardware Buddy 機能セット

### 1. Push-to-Hermes
ボタン1発で起動する定型機能。
- メモ起点
- リマインド起点
- 次タスク取得
- 帰宅モード
- 出費記録起点
- 今の気分/状態記録

### 2. Ambient display
常時見える最小状態。
- 次の予定
- 未処理タスク数
- リマインド件数
- Hermes busy/idle
- StackChan 接続状態

### 3. Mode rituals
生活モードの切り替え。
- Focus
- Break
- Home
- Going out
- Capture only

### 4. Tiny conversation
将来的に内蔵マイクを使い、短い音声トリガーを入れる。
- 「覚えといて」
- 「あとで」
- 「次なに」

### 5. Personality layer
- 顔
- 一言
- 応答のニュアンス
- 状態に応じた idle animation

---

## おすすめ構成

### フェーズ1: Buddy Remote
入力中心。
- 3〜5 action
- webhook / API 経由
- 小さな status 表示

### フェーズ2: Buddy Dashboard
見る価値を増やす。
- TODO件数
- 次の予定
- busy/idle
- 通知あり

### フェーズ3: Buddy Persona
相棒感を入れる。
- 顔
- 状態アニメ
- 押した時の返答
- 音声 or 効果音

---

## 具体的な Home 画面案

```text
+------------------------+
| HERMES   IDLE   TODO 3 |
|                        |
| > MEMO                |
|   REMIND              |
|   NEXT                |
|   HOME                |
|                        |
| A:MOVE       B:SEND   |
+------------------------+
```

Result 画面イメージ:

```text
+------------------------+
| OK   NEXT              |
|                        |
| OTA URL を確認して     |
| そのあと webhook登録   |
|                        |
| Hermes buddy ready     |
+------------------------+
```

---

## 既存秘書リモコン案との関係

既存の秘書リモコン案は捨てなくてよい。
むしろそれを Hardware Buddy の Phase 1 とみなすのが自然。

対応関係:
- 秘書リモコン = buddy の input layer
- 状態表示端末 = buddy の ambient layer
- 表情端末 = buddy の persona layer

つまり別プロジェクトではなく、同じ流れでよい。

---

## 最初に実装すべき action

MVP ならこの4つが強い。

1. `secretary.memo.capture`
2. `secretary.reminder.quick_add`
3. `secretary.task.next`
4. `secretary.mode.home`

特に `mode.home` は Hardware Buddy っぽさが強い。
押した瞬間に
- 帰宅ログ
- 次の予定確認
- 今夜やること1件返す
みたいな flow にできる。

---

## Hermes 側で持たせたい返答スタイル

Hardware Buddy では、無機質な API 応答より「短く返事する」のが大事。

例:
- `Memo captured`
  ではなく
  `受け取ったよ`
- `Next task fetched`
  ではなく
  `次は webhook 登録だよ`
- `Reminder flow accepted`
  ではなく
  `あとで扱えるようにしたよ`

この一言が buddy 感を作る。

---

## 実装上の優先順位

### 最優先
1. 既存 M5StickS3 secretary remote を buddy naming へ寄せる
2. action を 3→4 に増やすか検討
3. status bar を追加
4. Hermes 側レスポンスを短い buddy 文体にする

### 次点
5. TODO件数 / 次予定の軽量表示
6. idle / busy の状態反映
7. HOME / FOCUS の mode action

### 後回し
8. 音声
9. 顔アニメ
10. 常時 push 通知

---

## 現時点の推奨

余った M5StickS3 で作るべきものは、
「Hermes の超便利ツール」としては
単なるリモコンではなく `Hermes Hardware Buddy` が正解。

最初の着地はこれ。
- 小さい
- すぐ使える
- 実用がある
- 将来ちゃんと相棒化できる

## 次の一歩

1. 既存 secretary remote app を buddy コンセプトへリネーム/再整理
2. `mode.home` を追加するか決める
3. status bar を追加する Phase 1.1 仕様を書く
4. Hermes 側 webhook response を buddy 文体にする
