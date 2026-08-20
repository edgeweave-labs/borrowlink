# BorrowLink Protocol

版本：draft-2

日期：2026-08-20

狀態：設計稿，尚未完成實作與硬體驗證

BorrowLink 讓 ESP32 裝置向附近可信任的 Host 借用網路，或透過 Host
交換短訊息。Host 可以是 macOS、Linux、iPhone、Android，或具備相同能力的
gateway。

BorrowLink 是應用層代理協議，不是 IP bridge。ESP32 不會取得 Host 所在網路的
IP 位址；Host 以自己的網路連線代送 HTTP、HTTPS、WebSocket 或其他上游流量。

本文件是 wire format、狀態語義與平台邊界的單一事實來源。draft-2 尚未發布，
因此 draft-1 的常數與格式不具相容性承諾。

---

## 1. 目標與非目標

### 1.1 目標

- 同一套純 C core 可由 ESP32、其他 MCU 與 Host 重用。
- 廣播發現與已連線資料傳輸互不綁死。
- 根據需求選擇可靠傳輸或低延遲傳輸，不讓所有封包承擔最完整模式的成本。
- 支援有限 request-response、長時間雙向 stream，以及小型 message。
- 有訂閱時可維持省電連線；無需求時可完全關閉 BLE。
- 所有緩衝區、狀態、時間與亂數由 caller 提供。

### 1.2 非目標

- v1 不做 IP bridge、NAT 或虛擬網卡。
- v1 不做自製 mesh、routing table、neighbor discovery 或 flooding。
- v1 不保證 hard real-time deadline。
- v1 不支援同一條 GATT link 上多個並行 session。
- v1 不在廣播中傳送敏感資料或應用 payload。

---

## 2. 角色與兩個獨立平面

ESP32 Share Client 是 GAP peripheral 與 GATT server。Host 是 GAP central 與
GATT client。

BorrowLink 分成兩個獨立平面：

~~~text
Beacon plane
  OFF / DISCOVERY / ANNOUNCE

Link plane
  DISCONNECTED / CONNECTED_IDLE / ACTIVE
~~~

Beacon plane 負責發現與提示。Link plane 負責可靠或低延遲資料。已建立連線不代表
廣播一定停止；是否能同時廣播與連線，由 ESP-IDF adapter 依 SoC 能力決定。無法
同時執行時，adapter 可以分時，但不得改變 wire format。

---

## 3. 功耗與廣播政策

廣播政策是持久化設定：

| Policy | 行為 |
|---|---|
| DISABLED | 不發送任何 BorrowLink 廣播 |
| ON_DEMAND | 只有操作、待送訊息或本機觸發時廣播 |
| ENABLED | 允許週期性 presence 與 on-demand 廣播 |

DISABLED 不會中斷既有 GATT 連線。連線斷開後，Host 無法透過 BLE 找回裝置，因此
產品必須至少提供一種本機恢復方式：實體按鍵、開機設定窗口或定時廣播窗口。

Link plane 使用三個狀態：

| State | 行為 |
|---|---|
| DISCONNECTED | 沒有 GATT link |
| CONNECTED_IDLE | 保留連線，使用較長 connection interval 與 peripheral latency |
| ACTIVE | 有資料要傳，要求較短 interval 與低 latency |

沒有訂閱、session 或 mailbox 工作時，產品可以斷線並關閉 BLE。有活躍訂閱時，
優先維持 CONNECTED_IDLE；Host 或硬體拒絕低功耗參數時，adapter 可以退回週期性
重新連線。

connection interval、peripheral latency、supervision timeout、廣播間隔與重複
次數是板級校準值，不是 protocol 常數。實際採用值必須以各板子和各 Host 量測。

Core 只輸出抽象動作：

~~~text
SET_BEACON_OFF
SET_DISCOVERY_BEACON
EMIT_ANNOUNCEMENT
SET_LINK_IDLE
SET_LINK_ACTIVE
DISCONNECT
~~~

NVS、BLE controller 與 connection parameter update 都屬於 ESP-IDF adapter。

---

## 4. 發現廣播

使用 BLE legacy advertising。legacy advertising 是 v1 的共同最低能力；
extended advertising 可以由 adapter 額外使用，但不能成為互通前提。

Primary advertisement 必須包含 BorrowLink Service UUID，讓 Host 能以 UUID filter
掃描。128-bit UUID 已占用大部分 legacy payload，因此 6-byte Beacon Data 放在
scan response 的 128-bit Service Data AD structure；不再重複 local name。

scan response 可能因 Host 背景限制、passive scan 或封包遺失而缺席。Beacon Data
因此只是優化提示：Host 可以只憑 Service UUID 連線，HELLO 才是 profile、xid 與
delivery 的權威資料。

Beacon Data 固定 6 bytes，不含 Service Data AD structure 內重複的 128-bit
Service UUID。所有多位元組整數使用 network byte order（big-endian）。

| Offset | Field | Size | 說明 |
|---|---|---:|---|
| 0 | version | 1 | 本文件為 0x01 |
| 1 | profile | 1 | 預期建立的 session profile |
| 2 | xid | 2 | 非零 operation ID |
| 4 | attempt | 1 | 同一 xid 的嘗試次數，從 0 開始 |
| 5 | flags | 1 | 廣播與 session 提示 |

profile：

| Value | Profile |
|---:|---|
| 0x00 | Presence，沒有待處理 session |
| 0x01 | HTTP exchange |
| 0x02 | Opaque stream |
| 0x03 | Message broker |
| 0x04–0xFF | Reserved |

flags：

| Bit | Name | 說明 |
|---:|---|---|
| 0 | REALTIME_REQUESTED | 希望使用低延遲、可丟失 delivery |
| 1 | ANNOUNCEMENT | 這是 best-effort 提示，不代表資料已可靠送達 |
| 2 | KEEP_CONNECTED | session 後希望回到 CONNECTED_IDLE |
| 3–7 | Reserved | 發送端設為 0；接收端忽略 |

Presence 使用 profile 0x00、xid 0、attempt 0。其他操作的 xid 必須由 caller 的
亂數來源產生，且在一次重試序列中維持不變。

HTTP、xid 0x1234、attempt 0、RELIABLE 的 Beacon Data golden vector：

~~~text
01 01 12 34 00 00
~~~

Host 不認得 version 時不連線；不認得 profile 時可以忽略廣播，或連線後明確回
UNSUPPORTED_PROFILE。

DISCOVERY 是 connectable advertisement。ANNOUNCEMENT 可以是 non-connectable；
若有資料等待 Host 取回，裝置仍須開啟 DISCOVERY 窗口。廣播 payload 不包含
token、URL、node ID 或應用資料。

廣播未加密也未驗證，只能觸發掃描、連線或 UI 提示。任何有權限的操作都必須等到
bonded、encrypted GATT link 建立後才執行。

第一個成功完成安全連線並接受 xid 的 Host 取得該 operation。失敗後裝置依產品
政策增加 attempt 並重新廣播。剛對相同裝置與 xid 失敗的 Host 應延遲回應後續
attempt，讓其他 Host 有機會接手。

---

## 5. 身分與安全

### 5.1 Link security

- 只接受已 bonded 的 Host。
- 使用 LE Secure Connections 與加密 characteristic 權限。
- 初次配對必須由產品提供有人在場的確認流程。
- 未加密、未 bonded 或身分不符的連線立即中止。

Host 代理 HTTPS 時可以看到 URL、headers、body 與 bearer token。BorrowLink 的
bonding 保護 BLE hop，不提供裝置到最終伺服器的端到端機密性。

### 5.2 Node identity

每個 BorrowLink endpoint 在 provisioning 時產生一個非零 64-bit node_id，並
持久化保存。node_id 用於 Host broker 的 subscription 與訊息來源識別，不是
密鑰，也不取代 bonding。

node_id 不放進廣播。Host 先以平台提供的 bonded peripheral identity 辨識廣播。
第一次安全配對後，Host 將 node_id 綁定到該 bond；後續 HELLO 或 ACCEPT 的
node_id 必須相符。Host 發現 bond/node_id mismatch 或 node_id 衝突時必須拒絕，
並要求重新 provisioning。

---

## 6. GATT service

BorrowLink 使用一個自訂 service 與三個用途單一的 characteristic：

| Item | UUID | Properties |
|---|---|---|
| BorrowLink Service | 8767BAC8-7F7D-4DFA-AAE2-F24E1785091E | — |
| Control | 2558681A-40D2-4932-90BC-1DD18B5871EC | Indicate, Write |
| Reliable Data | B809C14E-36ED-4D47-8DF7-6DA79E181B19 | Indicate, Write |
| Realtime Data | C4F31099-2DFA-452A-8024-5F8F7C885E4F | Notify, Write Without Response |

所有 characteristics 都要求 encrypted access。分開 characteristic 不增加每個
frame 的 wire overhead，並避免不同 Host 對同一個 CCCD 的 Notify/Indicate
選擇不一致。

方向與 characteristic：

| Frame | ESP32 → Host | Host → ESP32 |
|---|---|---|
| HELLO、ACCEPT、STATUS、RESET | Control Indicate | Control Write With Response |
| RELIABLE DATA | Reliable Data Indicate | Reliable Data Write With Response |
| REALTIME DATA | Realtime Data Notify | Realtime Data Write Without Response |

Host 連線後發現 service，先訂閱 Control 與 Reliable Data indication。廣播要求
REALTIME 時，再訂閱 Realtime Data notification；這讓 responder 可以安全降級
為 RELIABLE。裝置未收到 Control subscription ready 事件前不得送出 HELLO。
ACCEPT 選定 delivery 後，雙方只使用對應的 Data characteristic，直到 session
結束。

---

## 7. Frame format

每個 GATT characteristic value 恰好承載一個 BorrowLink frame：

| Offset | Field | Size | 說明 |
|---|---|---:|---|
| 0 | seq | 2 | 每個 session、每個方向獨立，從 0 開始、modulo 65536 |
| 2 | opcode | 1 | frame 類型 |
| 3 | payload | remaining | opcode 對應內容 |

payload 上限為 ATT_MTU - 6：ATT header 3 bytes，加 BorrowLink frame header
3 bytes。

opcode：

| Value | Name | 說明 |
|---:|---|---|
| 0x00 | DATA | 邏輯訊息的一部分 |
| 0x01 | DATA_END_MESSAGE | payload 後結束一個邏輯訊息 |
| 0x02 | DATA_END_STREAM | payload 後關閉本方向 |
| 0x03 | DATA_END_MESSAGE_STREAM | 同時結束訊息與本方向 |
| 0x10 | HELLO | 開始 session |
| 0x11 | ACCEPT | 接受 session |
| 0x12 | STATUS | 非終止狀態 |
| 0x13 | RESET | 終止 session |
| 0x14–0xFF | Reserved | 收到即 RESET：UNSUPPORTED_OPCODE |

seq 涵蓋所有 frame。RELIABLE session 收到不連續 seq 時立即 RESET；
REALTIME session 可以接受向前跳號並回報 gap，重複或舊 frame 直接丟棄。

一個邏輯訊息可以跨多個 DATA frame。Core 必須 streaming encode/decode，不得要求
caller 先組成完整 request、response 或 message。

Control characteristic 只接受 HELLO、ACCEPT、STATUS、RESET。Data
characteristic 只接受 DATA family opcode。送到錯誤 characteristic 的 frame
立即 RESET：PROTOCOL_ERROR。

---

## 8. Session handshake 與生命週期

任一 endpoint 都可以在 idle link 上送 HELLO。v1 同一時間只允許一個 session。
ESP32 與 Host 同時送 HELLO 時，ESP32 發起的 session 優先；Host 取消自己的
HELLO，稍後重試。

HELLO payload 固定 15 bytes：

| Field | Size |
|---|---:|
| version | 1 |
| profile | 1 |
| requested_delivery | 1 |
| xid | 2 |
| initiator_node_id | 8 |
| max_rx_payload | 2 |

ACCEPT payload 固定 14 bytes：

| Field | Size |
|---|---:|
| version | 1 |
| selected_delivery | 1 |
| xid | 2 |
| responder_node_id | 8 |
| max_rx_payload | 2 |

requested_delivery：

| Value | Name |
|---:|---|
| 0x00 | RELIABLE |
| 0x01 | REALTIME |

Responder 可以把 REALTIME 降級為 RELIABLE，但不能反向升級。max_rx_payload 不含
3-byte frame header，且不得大於當前 ATT_MTU - 6。每個方向使用對方宣告的值。

HTTP profile 只允許 RELIABLE。Opaque stream 預設 RELIABLE；只有上層明確接受
gap 時才允許 REALTIME。Message broker 可以使用兩種 delivery。

HELLO 與 ACCEPT 的 xid 必須相同，並與 discovery operation 相符。沒有 discovery
的 Host-initiated session 使用 Host 產生的非零 xid。

每個方向以 DATA_END_STREAM 關閉。雙方都結束後 session 完成；若 flags 要求
KEEP_CONNECTED，link 回到 CONNECTED_IDLE，否則可斷線。RESET 會立即結束雙方。

STATUS payload：

| Field | Size | 說明 |
|---|---:|---|
| code | 1 | 目前只定義 0x00 IN_PROGRESS |
| detail | remaining | profile-specific，可為空 |

RESET payload 的第一個 byte 是原因：

| Value | Name | 是否適合換 Host 重試 |
|---:|---|---|
| 0x10 | NO_NETWORK | 是 |
| 0x11 | UPSTREAM_FAILED | 視上層政策 |
| 0x12 | UPSTREAM_TIMEOUT | 是 |
| 0x13 | REJECTED | 否 |
| 0x14 | UNSUPPORTED_PROFILE | 否 |
| 0x15 | PROTOCOL_ERROR | 否 |
| 0x16 | BUSY | 是，退避後 |
| 0x17 | SECURITY_ERROR | 否 |
| 0x18 | LIMIT_EXCEEDED | 否 |
| 0x19 | UNSUPPORTED_OPCODE | 否 |

Core 不自動 retry。它回報結果，由產品政策決定 attempt、退避、換 Host 或改走
Wi-Fi。

---

## 9. HTTP profile

HTTP profile 是有限 request-response。ESP32 通常是 initiator；Host 代送上游
request。

Request 的第一個邏輯訊息：

~~~text
method       1 byte
url_len      2 bytes
headers_len  2 bytes
url          url_len bytes, UTF-8, no NUL
headers      headers_len bytes, UTF-8
body         remaining stream bytes
~~~

method：

| Value | Method |
|---:|---|
| 0x01 | GET |
| 0x02 | POST |
| 0x03 | PUT |
| 0x04 | PATCH |
| 0x05 | DELETE |
| 0x06 | HEAD |
| 0x07 | OPTIONS |

headers 使用 Name: value 加 CRLF 的標準文字形式，最後一個 header 後不要求空白
CRLF。header name/value 不得包含 CR、LF 或 NUL。

Host response：

~~~text
status       2 bytes
headers_len  2 bytes
headers      headers_len bytes, UTF-8
body         remaining stream bytes
~~~

body 長度不放進 profile header；DATA_END_STREAM 是唯一結束標記。HTTP 4xx/5xx 是
合法 response，不是 UPSTREAM_FAILED。UPSTREAM_FAILED 只表示無法取得有效 HTTP
response。

http 與 https URL 都由 Host 執行。https 的 TLS 在 Host 終止，因此 Host 能看到
內容。需要端到端 TLS 時使用 Opaque stream，由真正 endpoint 處理 TLS bytes。

url_len、headers_len 與累計 body 大小都必須先與 caller 設定的上限比較。超限時
RESET：LIMIT_EXCEEDED。

---

## 10. Opaque stream profile

Opaque stream 提供長時間全雙工 byte/message channel，用於 WebSocket proxy、
TCP tunnel 或其他未由 core 解讀的協議。

- DATA_END_MESSAGE 保留 WebSocket 或應用 message boundary。
- DATA_END_STREAM 只關閉發送方向。
- 兩個方向可以交錯傳輸。
- Core 不解讀 stream payload。
- keepalive、WebSocket ping/pong 與上游 reconnect 屬於 Host profile adapter。

v1 core 只保證 framing 與 lifecycle；target、authentication 與上游協議 metadata
由具體 stream profile 定義。未實作對應 profile 的 Host 必須
RESET：UNSUPPORTED_PROFILE。

---

## 11. Message broker profile

Message profile 提供「註冊要聽誰」與小型 publish/deliver。每個 broker command
是一個以 DATA_END_MESSAGE 結束的邏輯訊息。多位元組欄位使用 big-endian。

v1 只支援精確 source_node_id + topic，不支援 wildcard。

### 11.1 Commands

SUBSCRIBE，command 0x01：

~~~text
command          1 byte
request_id       2 bytes
source_node_id   8 bytes
topic            2 bytes
lease_seconds    2 bytes
queue_policy     1 byte
~~~

UNSUBSCRIBE，command 0x02：

~~~text
command          1 byte
request_id       2 bytes
source_node_id   8 bytes
topic            2 bytes
~~~

PUBLISH，command 0x03：

~~~text
command          1 byte
message_id       2 bytes
topic            2 bytes
payload          remaining message bytes
~~~

DELIVER，command 0x04：

~~~text
command          1 byte
message_id       2 bytes
source_node_id   8 bytes
topic            2 bytes
payload          remaining message bytes
~~~

ACK，command 0x05：

~~~text
command          1 byte
request_id       2 bytes
status           1 byte
~~~

queue_policy：

| Value | Policy |
|---:|---|
| 0x00 | ONLINE_ONLY，不保留離線訊息 |
| 0x01 | LATEST，每個 source/topic 只保留最新一筆 |
| 0x02 | FIFO_BOUNDED，依 Host 設定的有限容量保存 |

ACK status：

| Value | Status |
|---:|---|
| 0x00 | ACCEPTED |
| 0x01 | REJECTED |
| 0x02 | NO_CAPACITY |
| 0x03 | UNKNOWN_SOURCE |
| 0x04 | NOT_AUTHORIZED |

lease_seconds 為 0 時，subscription 在 link 結束時失效；非零時最多保留該秒數。
subscriber 是 subscription 的事實來源，重連或更換 Host 後必須重新註冊。不同
Host 的 subscription 與 mailbox 不自動同步。

Host 必須以 HELLO 驗證後的 node_id 填入 DELIVER.source_node_id，不得信任
publisher payload 自稱的來源。RELIABLE message 要求 ACK；REALTIME
PUBLISH/DELIVER 可以丟失、跳號且不重傳。

Host 的 queue 容量是明確上限。容量不足時回 NO_CAPACITY，不得無界成長或靜默
丟棄 RELIABLE message。

request_id 在同一 endpoint 尚未收到 ACK 前不得重用。message_id 由 publisher
在每個 topic 內 modulo 65536 遞增。Broker 對 PUBLISH 回 ACCEPTED 只代表已接受
或排入本機 mailbox，不代表 subscriber 已收到，也不保證 Host crash 後仍存在。

REALTIME PUBLISH 與 DELIVER 必須各自放進單一 DATA_END_MESSAGE frame；超過
negotiated max payload 時由上層縮小或改用 RELIABLE。這讓單一 frame 遺失不會
破壞後續 message boundary。

---

## 12. 廣播、訂閱與即時性的保證

BorrowLink 的保證依接收者狀態而不同：

| 狀態 | 保證 |
|---|---|
| CONNECTED_IDLE / ACTIVE | Host 可以立即嘗試轉送 |
| DISCONNECTED，subscription lease 有效 | 依 queue_policy 保存，重連後交付 |
| Beacon policy DISABLED 且無連線 | 無法由 Host 主動喚醒 |
| REALTIME delivery | best-effort，允許遺失 |

廣播只表示「有事情可能值得處理」，不表示 message 已送達。可靠 delivery 必須經過
加密 link、session 與 ACK 或 stream completion。

手機背景掃描和執行受 OS 排程限制，因此不能承諾固定 discovery latency。
CONNECTED_IDLE 可以減少重連成本，但 Host 仍可能終止連線或拒絕要求的低功耗
參數。

---

## 13. Mesh 邊界

v1 是單跳協議，沒有 ttl、route_id 或 relay role。這是刻意的：

- Host broker 已能完成 source/topic subscription。
- 大型 HTTP/WebSocket payload 不適合 flooding。
- 自製 mesh 會重複 routing、replay protection、provisioning 與 key management。

只有在「沒有 Host、必須跨出直接無線範圍、現場存在常供電 relay」三個條件同時
成立時，才加入 mesh。

未來 mesh 應是 transport adapter，例如 ESP-BLE-MESH custom model。BorrowLink
session payload 可以映射到原生 mesh message，但 routing、TTL、relay、Friend、
Low Power Node 與 network security 由原生 mesh stack 負責，不加入 protocol core。

---

## 14. Core 與 adapter 邊界

Repository 的目標結構：

~~~text
include/borrowlink/
  borrowlink.h

src/
  wire.c
  session.c

profiles/
  http.c
  message.c

adapters/esp_idf/
  gatt.c
  storage.c

tests/
  host/
  fake_adapter/
~~~

純 C core：

- C11。
- 不配置 heap。
- 不建立 thread。
- 不執行 I/O。
- 不讀取系統 clock 或 random。
- 不包含 ESP-IDF、FreeRTOS、socket、HTTP 或 BLE header。
- state、buffer、time、random、limits 全由 caller 提供。

狀態機使用 deterministic reducer：

~~~c
bl_result bl_session_step(
    bl_session *state,
    const bl_event *event,
    bl_action *action);
~~~

一次 event 最多產生一個 action。adapter 執行 action 後，以完成或失敗 event
回報。Action 中的 byte view 只保證有效到下一次 step；需要保留資料的 caller
必須自行複製。

Core error 分類：

~~~text
CALLER_ERROR
PROTOCOL_ERROR
SECURITY_ERROR
LIMIT_EXCEEDED
TIMEOUT
TRANSPORT_ERROR
~~~

Core 不使用 errno、log callback 或動態錯誤字串。

ESP-IDF adapter：

- GAP/GATT lifecycle。
- bonding 與 encrypted permission。
- monotonic clock 與 random。
- NVS node_id、bond 與 beacon policy。
- connection parameter 與功耗校準。
- 將 BLE callback 轉成 core event，並執行 core action。

Host implementations 可以重用 wire/session core，但 Host broker、OS background
lifecycle、HTTP client 與 WebSocket client 留在各 Host project。

---

## 15. 驗證與未決量測

### 15.1 Host-side 必要檢查

- C11 與 C++17 public header compatibility。
- discovery、frame、HTTP、message 的 golden vectors。
- truncated input、reserved opcode、錯誤長度、整數 overflow。
- RELIABLE sequence mismatch 與 REALTIME gap/wrap。
- reducer 的合法與非法 state transitions。
- caller-owned buffer 邊界與 zero-length payload。
- fake adapter 的完整 advertise → connect → session → idle/off trace。

Parser 與 reducer 必須能在 ASan/UBSan 下執行。Fuzz target 等 parser API 穩定後再
加入，不是第一版骨架的前置條件。

### 15.2 上板前不得寫死的值

- discovery 快速與延長窗口。
- presence/announcement interval 與重複次數。
- CONNECTED_IDLE interval、peripheral latency 與 supervision timeout。
- ACTIVE connection interval。
- 各 Host 可接受的 MTU 與 connection parameters。
- RELIABLE 與 REALTIME 吞吐、丟包與耗電。
- 同時連線與廣播的 SoC 限制。
- mailbox 容量與產品級 queue policy。
- Wi-Fi 單 channel scan 的時間與能量。

每塊板可以有不同校準值，但不得改變本文件的 wire format 或狀態語義。

---

## 16. 實作順序

1. wire codec 與 golden vectors。
2. 單 session reducer，先完成 RELIABLE。
3. fake adapter 與 host sanitizers。
4. ESP-IDF GATT adapter、bonding 與 discovery beacon。
5. HTTP profile。
6. CONNECTED_IDLE 功耗量測與校準。
7. Message subscription/profile。
8. REALTIME delivery。
9. 有實際產品需求時才增加 Opaque stream 或 native mesh adapter。

每一步都必須維持 core 無 heap、無 I/O、caller-owned memory 的邊界。
