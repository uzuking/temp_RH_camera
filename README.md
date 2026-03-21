# temp_RH_camera

苗床モニタリングシステムの ESP32 ファームウェア。

ヒートマット上の苗床（フード付き）の温湿度を DHT22 で記録し、Grove Serial Camera Kit で苗の成長を撮影する。WiFi 経由で PC/サーバーに HTTP POST でデータを送信する。

## 設計書

詳細な設計（配線、API 仕様、開発ステップ等）は [docs/design.md](docs/design.md) を参照。

## 環境

- PlatformIO（Arduino framework）
- ボード: ESP32 DevKit（ESP-WROOM-32、30 ピン）

## セットアップ

1. PlatformIO でこのプロジェクトを開く
2. WiFi クレデンシャルを設定:
   ```bash
   cp include/credentials.h.example include/credentials.h
   # credentials.h を編集して SSID、パスワード、サーバー IP を設定
   ```
3. ビルド & アップロード:
   ```bash
   pio run -t upload
   ```

## 配線概要

| 接続元 | 接続先 | 備考 |
|--------|--------|------|
| カメラ VCC | ESP32 VIN (5V) | |
| カメラ TX | **レベル変換** → ESP32 GPIO16 | 5V→3.3V 変換必須 |
| カメラ RX | ESP32 GPIO17 | |
| DHT22 VCC | ESP32 3V3 | |
| DHT22 DATA | ESP32 GPIO25 | プルアップ抵抗が必要な場合あり |

詳細は [docs/design.md](docs/design.md) を参照。

## 関連プロジェクト

- ベランダガーデンプロジェクト: `~/Documents/My_garden`
- PC 側受信サーバー: `~/Documents/My_garden/smart/`（当面の配置先）
