# 苗床モニタリングシステム 設計書

## 経緯

当初は育苗ヒーターの温度制御システム（DS18B20 + リレー）として計画していた（参照: My_garden/smart/seedling_heater.md）。
しかし、ヒートマット（HaruDesign SA-12C）に温度制御機能が付属しているため、制御は不要となり、**記録のみ**のシステムに方針変更した。

温度だけでなく湿度も記録するため DHT22 を採用し、苗の成長を撮影するカメラも追加した。

## システム概要

ベランダのヒートマット上に置いた苗床（フード付き）の環境をモニタリングするシステム。

- DHT22 で温度・湿度を 10 分間隔で計測
- Grove Serial Camera Kit で苗の成長を 1 時間間隔で撮影
- ESP32 から WiFi 経由で PC/サーバーに HTTP POST でデータ送信
- PC/サーバー側で CSV と JPEG を蓄積
- 蓄積した画像からタイムラプスを生成（後日）

## システム構成図

```
[フード付き苗床]（ヒートマット上）
    │
    ├─ [DHT22] ─── DATA ──→ ESP32 GPIO25
    │
    └─ [Grove Serial Camera Kit]
         ├─ TX ──→ レベル変換 ──→ ESP32 GPIO16 (RX2)
         └─ RX ←────────────── ESP32 GPIO17 (TX2)
                                    │
                                    │ WiFi (HTTP POST)
                                    ↓
                            [PC / Raspberry Pi]
                              ├─ /sensor → sensor_YYYY-MM.csv
                              └─ /image  → images/YYYYMMDDTHHMMSS.jpg
```

## ハードウェア構成

### 使用機材

| 部品 | 型番・仕様 | 用途 |
|------|-----------|------|
| マイコン | ESP32 DevKit（ESP-WROOM-32、30 ピン） | 制御・WiFi 通信 |
| 温湿度センサー | DHT22（3 ピンモジュール、プルアップ内蔵） | 温度・相対湿度の計測 |
| カメラ | Grove Serial Camera Kit（UART、JPEG） | 苗の成長撮影 |
| レベル変換 | 抵抗分圧（5kΩ + 10kΩ）またはレベル変換モジュール | カメラ TX (5V) → ESP32 RX (3.3V) |

### 配線

| 接続元 | 接続先 | 備考 |
|--------|--------|------|
| カメラ VCC | ESP32 VIN (5V) | USB 給電の 5V 系統から供給 |
| カメラ GND | ESP32 GND | |
| カメラ TX | **レベル変換** → ESP32 GPIO16 (RX2) | **5V→3.3V 変換必須**。直結は GPIO 破損の恐れ |
| カメラ RX | ESP32 GPIO17 (TX2) | 3.3V のまま通る場合が多い |
| DHT22 VCC | ESP32 3V3 | 3.3V 駆動 |
| DHT22 GND | ESP32 GND | |
| DHT22 DATA | ESP32 GPIO25 | 3 ピンモジュール（プルアップ内蔵）のため直結 |

### 電源の注意

- カメラは 5V 駆動。3V3 ピンからは供給しない
- DHT22 は 3.3V 駆動
- GND は全デバイスで共通にする
- USB 給電で運用

### レベル変換（必須）

ESP32 の GPIO は 3.3V 系（絶対最大定格 3.6V）。カメラ TX の 5V 信号を直接入力すると GPIO が破損する。

**抵抗分圧の場合:**
```
カメラ TX ──[5kΩ]──┬──→ ESP32 GPIO16 (RX2)
                   │
                 [10kΩ]
                   │
                  GND
```
出力電圧: 5V × 10k / (5k + 10k) ≒ 3.3V

## 通信方式

ESP32 → PC/サーバーに HTTP POST。家庭内 LAN 内のみで使用。

### HTTP API 仕様

#### `POST /sensor`

温湿度データを送信する。

- Content-Type: `application/json`
- リクエストボディ:
```json
{
  "temperature": 25.3,
  "humidity": 65.2,
  "timestamp": "2026-03-21T14:30:00+09:00"
}
```
- レスポンス: `200 OK`

#### `POST /image`

JPEG 画像を送信する。

- Content-Type: `image/jpeg`
- ヘッダー: `X-Timestamp: 2026-03-21T14:30:00+09:00`
- リクエストボディ: JPEG バイナリ
- レスポンス: `200 OK`
- サイズ上限: 100KB

## データ保存

### 保存先

サーバー側で設定可能。当面は `My_garden/data/seedling_monitor/`。

```
seedling_monitor/
├── sensor_2026-03.csv      # 温湿度ログ（月単位）
└── images/
    └── 20260321T143000.jpg  # タイムスタンプ付き
```

### CSV フォーマット

ヘッダー: `timestamp,temperature,humidity`

```csv
timestamp,temperature,humidity
2026-03-21T14:30:00+09:00,25.3,65.2
2026-03-21T14:40:00+09:00,25.1,66.0
```

月単位で分割する（`sensor_2026-03.csv`、`sensor_2026-04.csv`、...）。
書き込みは毎回 flush する。

### 画像ファイル命名

`YYYYMMDDTHHMMSS.jpg`（JST）。秒まで含めて重複を防ぐ。

### 画像管理

- CSV（メタデータ）: Git 管理
- JPEG 画像: DVC + Dropbox で管理（Git リポジトリの肥大化を防止）

## 計測・撮影間隔

| 項目 | 間隔 | 備考 |
|------|------|------|
| 温湿度 | 10 分 | 暫定値。運用後に調整可能 |
| 撮影 | 1 時間 | もっと間引いてもよい |

間隔はファームウェアの `#define` で変更可能にする。

## 既知の課題と対策

### メモリ制約

640x480 JPEG は 30-60KB。WiFi スタック使用時の ESP32 空き RAM は約 100KB。
→ **320x240 から開始**。問題なければ解像度を上げる。チャンク転送も検討。

### NTP 時刻同期

ESP32 の RTC は電源投入時にリセットされる。
→ WiFi 接続後に `configTime()` で NTP サーバー（`ntp.nict.jp`）から取得。タイムゾーンは JST (+9)。
NTP 取得失敗時は起動からの経過秒数をフォールバックとして使用。

### 送信失敗時のリトライ

- 温湿度: NVS（Non-Volatile Storage）に直近数件をバッファ。送信成功時にまとめて送る
- 画像: 送信失敗時は諦める（メモリ制約上）。失敗した旨をシリアルモニタにログ出力

### WiFi クレデンシャル

`include/credentials.h`（gitignored）で管理。テンプレートは `include/credentials.h.example`。

### サーバー IP アドレス

ルーターで PC に固定 IP を割り当てるか、mDNS（`seedling-server.local`）を使用。
`credentials.h` でサーバーの IP/ホスト名を設定。

### PC 側サーバーのセキュリティ

- 家庭内 LAN 内のみで使用。ポート開放はしない
- 受信データのサイズ上限を設ける（JSON: 1KB、画像: 100KB）
- 保存先パスは固定（パストラバーサル防止）

## 開発ステップ

簡単なものから段階的に難度を上げる。各ステップで動作確認してから次に進む。

- [x] **1. 事前準備** — DHT22 型番確認（3 ピンモジュール）、WiFi クレデンシャルファイル準備、設計書更新
- [x] **2. DHT22 単体テスト** — 完了条件: シリアルモニタに温湿度が 5 回以上連続表示
- [ ] **3. カメラ UART 通信確認** — 完了条件: カメラからの応答バイトを確認（JPEG 取得は後）。前提: レベル変換用抵抗（5kΩ + 10kΩ）の調達
- [ ] **4. WiFi + NTP 時刻同期** — 完了条件: JST 時刻がシリアルモニタに表示
- [ ] **5. 温湿度の HTTP POST** — 完了条件: PC 側でリクエスト受信を確認
- [ ] **6. PC 側受信サーバー** — 完了条件: CSV にデータが追記される
- [ ] **7. Grove Camera JPEG 取得** — 完了条件: JPEG データのサイズと先頭バイト (FFD8) を確認
- [ ] **8. 画像の HTTP POST** — 完了条件: PC 側で JPEG ファイルが保存される
- [ ] **9. 統合 + 定期送信** — 完了条件: 10 分/1 時間の間隔で自動送信が動作
- [ ] **10. 運用準備** — サーバー自動起動（systemd）、DVC 設定

## ファームウェアの場所

`~/Documents/PlatformIO/Projects/temp_RH_camera`（PlatformIO、Arduino framework）

## PC 側サーバーの場所

当面は `~/Documents/My_garden/smart/` に配置（Python、uv 管理）。
将来 Raspberry Pi 等に移設する可能性あり。

## 関連ドキュメント

- 旧設計書（ヒーター制御案）: `~/Documents/My_garden/smart/seedling_heater.md`
- ベランダガーデンプロジェクト: `~/Documents/My_garden/`
