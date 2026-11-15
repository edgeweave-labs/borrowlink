# BorrowLink draft-3 修訂建議

狀態：提案，**尚未併入 PROTOCOL.md**。協定歸 Boss 決定，本文只列「實作 session、Message profile、stream
binding 時發現的不清楚處」和建議寫法。每一條都標出 C 端（本 repo）和 EdgeWeave Python 端（`link/`、`hub/`）
目前各自怎麼做，沒有任何一邊被改成遷就另一邊：兩邊都照 draft-2 的文字，文字沒寫的地方才各自選擇，列在下面。

日期：2026-11-15。對應實作：`src/session.c`、`profiles/message.c`、`src/binding.c`；
對應 Python：EdgeWeave `link/session.py`、`link/message.py`、`hub/hub.py`（互通測試實際跑的版本見
`tests/interop/run.sh` 的輸出）。

## 1. Stream binding（新增一節）

GATT 仍是原生承載。Wi-Fi 上的裝置、模擬節點和 aipc 服務節點用同一套 session 與 profile，只換承載。
實作：`include/borrowlink/binding.h`、`src/binding.c`；EdgeWeave 原稿：`docs/stream-binding.md`。

### 1.1 記錄格式

位元組流上，每個 frame 前面加 3 bytes（big-endian）：

| Offset | Field | Size | 說明 |
|---:|---|---:|---|
| 0 | channel | 1 | 0 Control、1 Reliable Data、2 Realtime Data（對應 §6 的三個 characteristic） |
| 1 | length | 2 | 後面 frame 的長度，含 3 bytes frame header |
| 3 | frame | length | 原樣的 §7 frame |

- channel 與 opcode 的對應同 §7：Control 只收 HELLO／ACCEPT／STATUS／RESET，Reliable／Realtime 只收 DATA 家族。
- length 只有 16 bits：frame 最長 65535，所以 `max_rx_payload` 在這個承載上的上限是 **65532**。
- 接收端在讀完 3 bytes 記錄標頭時就檢查，不必等 body：
  - length 大於本端的 `max_rx_payload + 3` → `RESET: LIMIT_EXCEEDED`
  - length < 3 或 channel > 2 → `RESET: PROTOCOL_ERROR`
  - channel 與 opcode 不符，或 RELIABLE session 出現 Realtime channel → `RESET: PROTOCOL_ERROR`
  - 先檢查 length 上限、再檢查 channel（兩邊實作的順序相同，所以對同一筆壞資料回的原因相同）。
- HELLO／ACCEPT 各佔其方向的 seq 0，之後從 1 起算（見 §3 第 9 條）。

### 1.2 能力與限制

- 只支援 RELIABLE（TCP 本身可靠有序）。HELLO 要求 REALTIME 時 Host 降級成 RELIABLE（§8 允許）。
- 沒有 bonding。身分驗證由承載層之上負責（EdgeWeave 目前用預先配好的 token，放在 session 之後的第一則訊息；
  那是 EdgeWeave 的應用層規則，不是 BorrowLink 的）。非本機的 Wi-Fi 承載需要 TLS 或等價保護。
- `max_rx_payload` 沒有 ATT_MTU 可依，由各端自己宣告。**建議下限 14**：ACCEPT 的 payload 是 14 bytes，
  發起端用「自己的 `max_rx_payload + 3`」當記錄長度上限，小於 14 就收不下對方的 ACCEPT。C 端 `bl_session_init()`
  拒絕小於 14 的值。

### 1.3 結束（建議寫進 §8）

§8 說「每個方向以 DATA_END_STREAM 關閉，雙方都結束後 session 完成」，但沒說傳輸層先關閉怎麼算。
byte stream 沒有 GATT 那種「連線斷了就是斷了」的區別，需要一條規則：

> 在 byte-stream 承載上，對方「正常關閉」傳輸層（TCP FIN）視為它的方向已結束。
> 若我方已送出 DATA_END_STREAM，session 正常完成；若我方還沒送，則是傳輸錯誤。
> 傳輸層「錯誤中斷」（RST、逾時）永遠是傳輸錯誤。

C 端用獨立的事件 `BL_EV_LINK_CLOSED`（正常關閉）與 `BL_EV_LINK_LOST`（錯誤）表達這條規則。理由見 §3 第 11、12 條。

## 2. 保留 topic 區段與點對點訊息

### 2.1 保留區段

建議把 Message profile 的 16-bit topic 切成：

| 範圍 | 用途 |
|---|---|
| 0x0000 | 保留（不使用） |
| 0x0001–0x00FF | 給聚合層（Host broker 之上的協定）固定使用；各 profile 實作不得拿來當一般數據點 |
| 0x0100–0xFFFF | 由各裝置的能力宣告自行分配 |

EdgeWeave 目前用的（`contract/capabilities.toml` 的 `[topics]`，BorrowLink 不解讀）：

| topic | 用途 | 方向 |
|---:|---|---|
| 0x0001 | 能力宣告（節點連上後第一則訊息，也可再次發布更新） | 節點 → Host |
| 0x0002 | RPC 請求 | 呼叫方 → Host → 目標節點（Host 以 DELIVER 送給目標，source_node_id 是驗證過的呼叫方） |
| 0x0003 | RPC 回覆 | 目標節點 → Host → 呼叫方 |
| 0x0004 | 目錄變動（LATEST，新訂閱者立刻拿到完整目錄） | Host 發布 |

BorrowLink core 不認得這些 topic；它們屬於 Message profile 之上的應用規則。C 端的 `message.h` 因此沒有 topic 常數。

### 2.2 點對點 DELIVER（需要正式定義）

§11 的訂閱只有精確 `source_node_id + topic`，沒有 wildcard，所以 Host 要把「別人呼叫我」的請求送給節點時，節點事先不知道
對方是誰，無法 SUBSCRIBE。EdgeWeave 的做法：Host 對保留 topic 直接 DELIVER，**沒有對應的 SUBSCRIBE**。

建議在 §11 加：「0x0001–0x00FF 範圍的 DELIVER 不需要事先 SUBSCRIBE；接收端依 topic 與驗證過的 `source_node_id`
處理。」C 端的 Message profile 本來就不對 DELIVER 做訂閱比對（沒有訂閱表），所以收得到；若未來 core 加訂閱表，
這條規則必須先定。

## 3. 歧義與建議寫法

「C」是本 repo 的行為，「Py」是 EdgeWeave Python 端的行為。

| # | 位置 | 不清楚的地方 | C | Py | 建議 |
|--:|---|---|---|---|---|
| 1 | §8 HELLO | HELLO／ACCEPT 沒有 flags，`KEEP_CONNECTED` 只存在於 Beacon；沒有 Beacon 的承載（stream binding、Host 發起）無法表達 session 後要不要保持連線 | `bl_session_config.keep_connected` 由 caller 提供（來自 Beacon，或產品設定） | 不處理（TCP 沒有這個概念） | HELLO 加 1 byte flags（`KEEP_CONNECTED`、保留位），或明說「只有 Beacon 帶得到，其餘承載由產品政策決定」 |
| 2 | §11 | Host 把請求送給訂閱者以外的節點的方式沒定義 | 不比對訂閱，直接收 | Host 直接 DELIVER | 見 2.2 |
| 3 | §11 ACK | ACK 只有 `request_id`，PUBLISH 與 DELIVER 只有 `message_id`；對 PUBLISH／DELIVER 的 ACK 用哪個欄位沒寫 | 照 Py：ACK 的 `request_id` 填被確認訊息的 `message_id` | 同 | 明寫：「ACK 的 request_id 欄位在確認 PUBLISH／DELIVER 時帶其 message_id」 |
| 4 | §11 | `message_id`「由 publisher 在每個 topic 內遞增」，但 ACK 以單一 id 配對，每個 topic 各自的計數器會和 `request_id` 以及彼此撞號 | `bl_msg_client`：一個計數器同時給 request_id 與 message_id，跳過 0 和仍在等 ACK 的 id；pending 滿了回 `BL_ERROR_LIMIT_EXCEEDED` | 一個計數器，不會產生 0 | 改成「同一個 endpoint 的 request_id 與 message_id 共用一個 16-bit 遞增空間；在 ACK 到達前不重用；0 不使用」（比「每個 topic 內遞增」好實作也不會撞號）。若堅持每個 topic 各自遞增，ACK 就需要多一個 topic 欄位 |
| 5 | §11 | DELIVER 的 `message_id` 是 publisher 的還是 Host 重新編號 | 不關心；只用它回 ACK | 沿用 publisher 的 | 建議「Host 為每個 subscriber 重新編號」（否則兩個 publisher 的 id 會撞）。要先決定才能定 ACK 的語義 |
| 6 | §7、§8 | seq 不連續時 RESET 用哪個原因 | `PROTOCOL_ERROR` | `PROTOCOL_ERROR` | 寫進 §7 |
| 7 | §15.1 | 要求 Message golden vectors，文件裡一個都沒有 | 見下一節，五個指令各一筆，兩邊逐位元組一致 | 同 | 併入 PROTOCOL.md |
| 8 | §11 | lease 非零時，同一節點用新 session 或 Host 重啟後 subscription 是否保留 | broker 屬於 Host，C 不實作 | 保留信箱，重連後補送；Host 重啟不保留 | 寫明 |
| 9 | §7 | 「seq 從 0 開始」：HELLO 與 ACCEPT 是否各佔其方向的 seq 0、DATA 從 1 起算，沒有明說 | 是（HELLO seq 0、ACCEPT seq 0、之後從 1）；RESET／STATUS 也佔 seq | 同 | 寫進 §7 |
| 10 | §8 | 同時 HELLO：「Host 取消自己的 HELLO」，但 Host 的 HELLO 已經用掉它的 seq 0；之後 ACCEPT 的 seq 是 0 還是 1 沒說 | 在 HELLO_SENT 收到 Host 的 HELLO 時忽略、不消耗 seq（ACCEPT 預期 seq 0）；Py 的 respond 端沒有這個情形（Hub 不主動發 HELLO） | — | 明寫：「取消的 HELLO 不計入 seq；Host 的下一個 frame（ACCEPT）仍是 seq 0」，或乾脆禁止 Host 在 idle 以外發 HELLO |
| 11 | §7、§8 | DATA_END_STREAM 到達時邏輯訊息還沒結束（沒有 END_MESSAGE）算不算錯誤 | 算錯誤：reducer 在 DELIVER_DATA 動作標 `msg_truncated`，Message profile 的 driver 回 `RESET: PROTOCOL_ERROR` | **靜默丟掉半則訊息**並把連線當作正常關閉（實測：`hub` trace 只有 `link_down: closed`，沒有 protocol_error） | 建議「對 Message profile 是錯誤；Opaque stream 由 profile 決定」，並寫出 RESET 原因 |
| 12 | §8 | 半關閉：對方送 DATA_END_STREAM 後，我方還能不能繼續送；全部結束前誰負責關連線 | 可以繼續送（直到自己 END_STREAM）；對方 END_STREAM 之後再收到 DATA 是 `PROTOCOL_ERROR` | Hub 收到節點的 DATA_END_STREAM 就**關掉整條 TCP，沒有送自己的 DATA_END_STREAM**（實測：節點送 END_STREAM 後讀到的下一件事是 EOF） | 先決定半關閉要不要支援。若不支援，改寫「任一方的 DATA_END_STREAM 即結束 session」；若支援，Py 的 Hub 要在收到後送自己的 END_STREAM。在決定之前，C 端用 §1.3 的 `LINK_CLOSED` 規則相容兩者 |
| 13 | §7、§8 | `max_rx_payload` 的下限、以及它是否限制控制 frame | 只限制 DATA 的 payload；控制 frame 受限於 byte stream 的記錄長度上限；初始化要求 ≥ 14 | 同（DATA 才檢查），記錄長度上限用 `my_max_rx + 3` | 寫明下限（建議 ≥ 14，ACCEPT 才收得下）與「控制 frame 不受 DATA 的 payload 上限約束」，或把 HELLO／ACCEPT 的大小明列進上限 |
| 14 | §8 | RESET 沒有 reason byte（payload 空）怎麼辦 | 當作 `PROTOCOL_ERROR` | 同 | 寫明 |
| 15 | §8 STATUS | STATUS 沒有 code byte（payload 空）、或 code 不是 0x00 | 空 payload → `RESET: PROTOCOL_ERROR`；未知 code 原樣交給上層 | 只存起來 | 寫明未知 code 應被忽略還是拒絕 |
| 16 | §8 | ACCEPT 的 `selected_delivery` 比 HELLO 要求的「更高」（升級）、或 xid 不符，要回哪個原因 | `PROTOCOL_ERROR` | `PROTOCOL_ERROR` | 寫明 |
| 17 | §5.2 | ACCEPT／HELLO 的 node_id 與 bond 綁定的不符，「必須拒絕」但沒說用哪個 RESET 原因 | `bl_session_config.expected_peer_node_id` 不符 → `SECURITY_ERROR` | （P1 沒有 bond） | 寫明 `SECURITY_ERROR` |
| 18 | §4 | Presence beacon 的 flags 規則：§4 說接收端忽略保留位，`wire.c` 對 Presence 卻只要有已知旗標就 `MALFORMED`，對其他 profile 放行未知旗標 | 沿用 `wire.c`（本 repo 既有行為，未改） | 同 | 說明 Presence 為何特別嚴格，或統一 |
| 19 | §14 | `bl_session_step` 的 event／action 沒有規定結束時的 link action 要等 adapter 確認 | 每個 action 都要回報 ACTION_DONE／ACTION_FAILED；`SET_LINK_IDLE`／`DISCONNECT` 在最後一個 DONE 之後發出 | — | 在 §14 寫明「adapter 以完成／失敗 event 回報」的具體規則（見 `session.h` 開頭） |
| 20 | §8 | 沒有定義 session 結束時 core 回報給上層的「原因」分類 | `bl_reset_error_class()`：NO_NETWORK／UPSTREAM_FAILED／BUSY → TRANSPORT，UPSTREAM_TIMEOUT → TIMEOUT，SECURITY_ERROR → SECURITY，LIMIT_EXCEEDED → LIMIT，其餘 → PROTOCOL | — | §14 的 error 分類與 §8 的 RESET 原因要有對照表 |

## 4. 建議併入 PROTOCOL.md 的 golden vectors（兩端逐位元組一致）

Message 指令（§11.1 的欄位順序，big-endian）：

```text
SUBSCRIBE    request_id 0x0001, source 0x0102030405060708, topic 0x0100, lease 60, LATEST
             01 0001 0102030405060708 0100 003c 01
UNSUBSCRIBE  request_id 0x0001, source 0x0102030405060708, topic 0x0100
             02 0001 0102030405060708 0100
PUBLISH      message_id 0x0002, topic 0x0100, payload dead
             03 0002 0100 dead
DELIVER      message_id 0x0003, source 0x1112131415161718, topic 0x0100, payload beef
             04 0003 1112131415161718 0100 beef
ACK          request_id 0x0002, ACCEPTED
             05 0002 00
```

Stream binding 記錄（channel 1、length 5、frame `1234 01 aabb`）：

```text
01 0005 123401aabb
```

C 端 `tests/host/test_message.c`、`tests/host/test_binding.c` 內建這些向量；`tests/interop/test_crosscheck.py`
另外用隨機（含非法）輸入逐位元組比對 C 與 Python 的 Beacon／frame／HELLO／ACCEPT／Message 指令／記錄編解碼。

## 5. 本次互通沒有發現 Python 端違反 PROTOCOL.md 的位元組格式

位元組層（Beacon、frame、HELLO、ACCEPT、五個 Message 指令、stream binding 記錄）兩端一致，隨機測試沒有差異。
行為層的差異只有第 3 節的 11、12（Python 的 Hub 在 END_STREAM 的處理上和 PROTOCOL.md §8 的敘述不同）。
兩處都沒有修改 Python，也沒有讓 C 去遷就它：C 端照 PROTOCOL.md（半則訊息被關閉是錯誤；雙方都 END_STREAM 才算完成），
只在 byte-stream 承載上多認一條「傳輸層正常關閉等同對方的 END_STREAM」（第 1.3 節），這條規則建議明文化。
