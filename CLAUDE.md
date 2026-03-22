# プロジェクトルール

## 絶対ルール

- **ファイルの削除禁止** — ユーザーの明示的な許可なく、いかなるファイルも削除してはならない
- **Python は uv で管理・実行** — パッケージ管理・仮想環境・スクリプト実行はすべて `uv` を使用すること（pip, conda, venv は使わない）

## プロジェクト概要

苗床モニタリングシステムの ESP32 ファームウェア。
DHT22（温湿度）+ Grove Serial Camera Kit（撮影）で苗床を監視し、WiFi HTTP POST で Raspberry Pi にデータ送信する。

## 設計書

**docs/design.md** に全体設計（配線、API 仕様、開発ステップ等）を記載。
変更時はここを正として更新すること。

## 技術スタック

- PlatformIO（Arduino framework）
- ボード: ESP32 DevKit（ESP-WROOM-32、30 ピン）

## WiFi クレデンシャル

`include/credentials.h`（gitignored）で管理。テンプレートは `include/credentials.h.example`。

## 関連リポジトリ

- ベランダガーデンプロジェクト: `~/Documents/My_garden`
- 受信サーバー（開発時）: `~/Documents/My_garden/smart/seedling-server/`（運用時は Raspberry Pi に配置予定）
- My_garden 側のルール: Python は uv で管理

## 開発進捗

docs/design.md の「開発ステップ」セクションのチェックリストで管理。
ステップ完了時はチェックを入れること。
