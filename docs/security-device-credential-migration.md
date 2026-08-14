# Device credential migration plan

## 背景

現在のREST/WebSocket認証は、MACアドレス等をサーバー公開鍵で暗号化した値を資格情報として扱っている。公開鍵は秘密ではないため、RSA暗号化だけでは送信者本人性を証明できない。MACアドレスも資格情報として扱ってはいけない。

## 移行方針

1. 既存RSA暗号化tokenは互換目的のlegacy credentialとして短期的に残すが、新規導入では使用しない。
2. 新credentialは、デバイスごとの共有秘密またはデバイス固有秘密鍵を用いる。
3. 最小実装案はHMAC-SHA256 challenge/responseとする。
4. 署名対象には以下を含める。
   - HTTP methodまたはWebSocket audience
   - canonical path
   - body hash
   - timestamp
   - nonce
   - device ID
   - key ID
5. サーバーはnonce再利用を保存して拒否する。
6. RESTとWebSocketはaudience/key scopeを分離する。
7. appはユーザーJWTで認証し、DB上で所有するdeviceだけを操作する。
8. デバイス鍵は初回プロビジョニング、ローテーション、失効、工場出荷リセットの手順を持つ。
9. 移行期間はlegacy credentialを明示的設定でのみ許可し、監査ログにlegacy利用を記録する。

## 非採用

- 公開鍵の秘匿を前提にする対策
- MACアドレス難読化
- timestamp追加のみの対策
- 独自暗号方式

## 今回の暫定対応

- token parserのpanicを修正し、形式・MAC・timestamp・長さ検証を追加する。
- TokenAuthMiddlewareはfail-closedにする。
- 恒久的な本人認証方式への完全移行は、既存device互換性への影響が大きいため別コミット/別PRで実施する。
