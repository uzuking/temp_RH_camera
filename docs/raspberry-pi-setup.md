# Raspberry Pi サーバーセットアップ手順書

このドキュメントは、2026-03-29 に実施した Raspberry Pi 4 のサーバーセットアップ作業の記録です。
何をなぜやったのかを解説します。

## 前提

- **Raspberry Pi 4 Model B Rev 1.5**
- **OS**: Debian GNU/Linux 12 (Bookworm) — Raspberry Pi OS
- **ネットワーク管理**: NetworkManager（Bookworm のデフォルト）
- **アクセス方法**: キーボード＋モニター直結（マウスなし、GUI なし）
- **WiFi**: 接続済み（SSID: sentakunetdego）

---

## 1. パスワードとセキュリティ設定

### なぜ必要か

Raspberry Pi OS の初期状態では、sudo（管理者権限でコマンドを実行する仕組み）がパスワードなしで使えることがある。
この状態で SSH（リモート接続）を有効にすると、同じネットワーク上の誰でも RPi を完全に操作できてしまう。

### やったこと

#### ユーザーパスワードの設定

```bash
passwd
```

- `passwd` はログイン中のユーザーのパスワードを変更するコマンド
- 新しいパスワードを2回入力する（画面には表示されない。これはセキュリティのための仕様）

#### sudo のパスワード要求を確認

```bash
sudo -k          # sudo のパスワードキャッシュをクリア
sudo whoami       # 管理者として「自分は誰？」を実行 → root と表示されるはず
```

- `sudo -k` : sudo は一度パスワードを入力すると数分間キャッシュする。`-k` でそのキャッシュをリセットする
- `sudo whoami` : パスワードを聞かれて、入力後に `root` と表示されれば正常

#### NOPASSWD 設定の削除

パスワードを聞かれなかった場合、`/etc/sudoers.d/` にある設定ファイルが原因。

```bash
sudo cat /etc/sudoers.d/*          # 全ファイルの中身を表示
ls /etc/sudoers.d/                  # ファイル名一覧を表示
```

今回は `010_pi-nopasswd` というファイルに `NOPASSWD: ALL` の設定があった。

```bash
sudo visudo -f /etc/sudoers.d/010_pi-nopasswd
```

- `visudo` : sudoers ファイルを安全に編集するコマンド（文法チェック付き）
- **変更前**: `uzuki ALL=(ALL) NOPASSWD: ALL`
- **変更後**: `uzuki ALL=(ALL) ALL`
- `NOPASSWD:` を削除しただけ。これで sudo 実行時にパスワードが必要になる
- `Ctrl+O` → `Enter` で保存、`Ctrl+X` で終了

### 用語解説

| 用語 | 意味 |
|------|------|
| `sudo` | "Super User DO" の略。一般ユーザーが管理者（root）権限でコマンドを実行するための仕組み |
| `root` | Linux の最高権限ユーザー。何でもできる（ファイル削除、設定変更、他ユーザーの操作） |
| `sudoers` | sudo の権限設定ファイル。誰がどのコマンドを管理者権限で実行できるかを定義する |
| `NOPASSWD` | sudo 実行時にパスワードを聞かない設定。便利だが危険 |

---

## 2. SSH の有効化

### SSH とは

**Secure Shell** の略。ネットワーク経由で別のコンピュータのターミナルを操作する仕組み。
通信は暗号化されるので、パスワードやコマンドが盗聴されない。

SSH を有効にすると、PC からRPi を操作できる（キーボード＋モニターを直結しなくてよくなる）。

### やったこと

```bash
sudo systemctl status ssh       # SSH サービスの状態を確認
sudo systemctl enable ssh       # 起動時に自動で SSH を開始する設定
sudo systemctl start ssh        # 今すぐ SSH を開始
```

### systemctl とは

`systemctl` は Linux の **サービス管理コマンド**。サービスとは、バックグラウンドで動くプログラムのこと。

| コマンド | 意味 |
|---------|------|
| `systemctl status <サービス>` | サービスの状態を確認（動いてる？止まってる？） |
| `systemctl start <サービス>` | サービスを今すぐ起動 |
| `systemctl stop <サービス>` | サービスを今すぐ停止 |
| `systemctl enable <サービス>` | RPi 起動時にサービスを自動起動する設定をON |
| `systemctl disable <サービス>` | 自動起動をOFF |
| `systemctl restart <サービス>` | 停止してから再起動 |

### PC から SSH で接続

```bash
ssh uzuki@192.168.105.200
```

- `ssh` : SSH クライアントコマンド
- `uzuki@192.168.105.200` : 「ユーザー uzuki として IP 192.168.105.200 に接続する」
- 初回接続時は「このホストを信頼しますか？」と聞かれる → `yes` と入力
- パスワードを入力するとRPi のターミナルに入れる
- `exit` で PC に戻る

### SSH の鍵認証（フィンガープリント）

初回接続時に表示される `ED25519 key fingerprint is SHA256:xxHG3x...` は、RPi の身元証明書のようなもの。
`yes` と答えると PC の `~/.ssh/known_hosts` に記録され、次回以降は聞かれない。
もし RPi を初期化して再接続すると鍵が変わるので警告が出る（その場合は `known_hosts` から該当行を削除する）。

---

## 3. 固定 IP アドレスの設定

### なぜ必要か

ルーターは通常 **DHCP** でデバイスに IP アドレスを自動割り当てする。
この IP はルーター再起動やデバイスの接続順で変わることがある。

ESP32 はサーバーの IP アドレスをファームウェアに書き込んでいるので、IP が変わると通信できなくなる。
固定 IP にすることで、常に同じアドレスでアクセスできるようにする。

### やったこと

#### 現在の状態を確認

```bash
nmcli connection show              # ネットワーク接続の一覧
ip route | grep default            # ゲートウェイ（ルーター）の IP を確認
```

- `nmcli` : **N**etwork**M**anager の **C**ommand **L**ine **I**nterface（コマンドライン操作ツール）
- `ip route` : ルーティングテーブル（ネットワーク経路情報）を表示
- `default via 192.168.105.168` → ゲートウェイ（ルーター）の IP が 192.168.105.168

#### 固定 IP を設定

```bash
sudo nmcli connection modify "sentakunetdego" \
  ipv4.method manual \
  ipv4.addresses 192.168.105.200/24 \
  ipv4.gateway 192.168.105.168 \
  ipv4.dns 192.168.105.168
```

各パラメータの意味：

| パラメータ | 値 | 意味 |
|-----------|-----|------|
| `ipv4.method` | `manual` | 手動で IP を指定する（`auto` = DHCP 自動割当） |
| `ipv4.addresses` | `192.168.105.200/24` | この RPi の IP アドレス。`/24` はサブネットマスク（同じネットワークの範囲を示す） |
| `ipv4.gateway` | `192.168.105.168` | ゲートウェイ（インターネットへの出口＝ルーター）の IP |
| `ipv4.dns` | `192.168.105.168` | DNS サーバー（ドメイン名→IP 変換）。家庭内ではルーターが兼ねることが多い |

#### 設定を反映

```bash
sudo nmcli connection down "sentakunetdego"   # 一旦切断
sudo nmcli connection up "sentakunetdego"     # 再接続（新しい IP で）
```

**注意**: SSH 接続中にこれを実行すると、IP が変わるので接続が切れる。新しい IP で再接続する。

#### 確認

```bash
hostname -I          # 現在の IP を表示 → 192.168.105.200
ping -c 3 google.com  # インターネット接続を確認
```

### もし設定を間違えたら（元に戻す方法）

```bash
sudo nmcli connection modify "sentakunetdego" \
  ipv4.method auto \
  ipv4.addresses "" \
  ipv4.gateway "" \
  ipv4.dns ""
sudo nmcli connection down "sentakunetdego"
sudo nmcli connection up "sentakunetdego"
```

`ipv4.method auto` に戻すと、ルーターから IP を自動取得する状態に戻る。

### /24 って何？（サブネットマスク）

`192.168.105.200/24` の `/24` は、IP アドレスの先頭 24 ビット（= 最初の3つの数字）がネットワーク部であることを示す。

```
192.168.105.200/24
├── 192.168.105 → ネットワーク部（同じネットワークのデバイスは全てここが同じ）
└── 200         → ホスト部（このデバイス固有の番号、1〜254 が使える）
```

つまり `192.168.105.1` 〜 `192.168.105.254` が同じネットワーク内のデバイス。

---

## 4. サーバーの配置

### scp によるファイル転送

```bash
# PC 側で実行
scp -r ~/Documents/My_garden/smart/seedling-server/ uzuki@192.168.105.200:~/Documents/My_garden/smart/
```

- `scp` : **S**ecure **C**o**p**y。SSH の仕組みを使ってファイルを転送する
- `-r` : ディレクトリを再帰的に（中身ごと）コピー
- `送信元パス ユーザー@ホスト:送信先パス` の書式

### uv のインストール

```bash
curl -LsSf https://astral.sh/uv/install.sh | sh
source ~/.local/bin/env
```

- `curl` : URL からデータをダウンロードするコマンド
- `| sh` : ダウンロードした内容をシェルスクリプトとして実行（パイプ）
- `source ~/.local/bin/env` : uv のパスをシェルに読み込む（再ログインでも有効になる）

### サーバーの手動テスト

```bash
cd ~/Documents/My_garden/smart/seedling-server
uv run python main.py
```

初回実行時、uv が自動的に以下を行う：
1. `.python-version` を読んで Python 3.12 が必要と判断
2. Python 3.12 をダウンロード・インストール
3. `.venv`（仮想環境）を作成
4. `main.py` を実行

---

## 5. systemd サービスの作成（自動起動）

### なぜ systemd か

RPi の電源が入るたびに手動で `uv run python main.py` を実行するのは現実的でない。
systemd にサービスとして登録すると、起動時に自動で実行され、クラッシュ時に自動復帰もしてくれる。

### サービスファイルの作成

```bash
sudo nano /etc/systemd/system/seedling-server.service
```

```ini
[Unit]
Description=Seedling Monitor Server
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=uzuki
WorkingDirectory=/home/uzuki/Documents/My_garden/smart/seedling-server
ExecStart=/home/uzuki/.local/bin/uv run python main.py
Restart=on-failure
RestartSec=10

[Install]
WantedBy=multi-user.target
```

### 各セクションの意味

#### [Unit] — サービスの説明と依存関係

| 項目 | 意味 |
|------|------|
| `Description` | サービスの説明文（`systemctl status` で表示される） |
| `After=network-online.target` | ネットワークが使えるようになってから起動する |
| `Wants=network-online.target` | ネットワークサービスも一緒に起動してほしい（なくても起動はする） |

#### [Service] — サービスの実行設定

| 項目 | 意味 |
|------|------|
| `Type=simple` | ExecStart のコマンドがそのままメインプロセスになる（最も一般的な形） |
| `User=uzuki` | root ではなく uzuki ユーザーとして実行（セキュリティのため） |
| `WorkingDirectory` | コマンドの実行ディレクトリ（`cd` してから実行するのと同じ） |
| `ExecStart` | 実行するコマンドのフルパス |
| `Restart=on-failure` | 異常終了したら自動で再起動する（正常終了では再起動しない） |
| `RestartSec=10` | 再起動前に10秒待つ（即座に再起動するとリソースを食い尽くす可能性があるため） |

#### [Install] — 有効化時の設定

| 項目 | 意味 |
|------|------|
| `WantedBy=multi-user.target` | 通常のマルチユーザーモード（一般的な起動状態）で有効にする |

### サービスの有効化と起動

```bash
sudo systemctl daemon-reload                    # サービスファイルの変更を読み込む
sudo systemctl enable seedling-server.service   # 自動起動を有効にする
sudo systemctl start seedling-server.service    # 今すぐ起動する
```

`enable` と `start` は別の操作：
- `enable` = 「次回起動時から自動で起動する」という**設定**
- `start` = 「今すぐ起動する」という**アクション**
- `enable --now` で両方を一度にできる

### 確認・運用コマンド

```bash
# 状態確認
sudo systemctl status seedling-server.service

# ログをリアルタイム表示（Ctrl+C で終了。サーバーは止まらない）
sudo journalctl -u seedling-server.service -f

# 特定時刻以降のログ
sudo journalctl -u seedling-server.service --since "15:30" --no-pager

# 再起動（コード更新後など）
sudo systemctl restart seedling-server.service
```

### journalctl とは

`journalctl` は systemd のログ閲覧コマンド。

| オプション | 意味 |
|-----------|------|
| `-u seedling-server.service` | 特定のサービスのログだけ表示 |
| `-f` | リアルタイムで新しいログを追跡（`tail -f` と同じ） |
| `--since "15:30"` | 指定時刻以降のログだけ表示 |
| `--no-pager` | ページャー（less）を使わずに全て出力 |

---

## 6. 画像ビューアー（簡易 Web サーバー）

画像をブラウザで確認するために、Python の標準機能で HTTP サーバーを起動している。

### サービスファイル

`/etc/systemd/system/image-viewer.service`

```ini
[Unit]
Description=Seedling Image Viewer
After=network-online.target

[Service]
Type=simple
User=uzuki
ExecStart=/usr/bin/python3 -m http.server 9090 --directory /home/uzuki/Documents/My_garden/data/seedling_monitor/images
Restart=on-failure
RestartSec=10

[Install]
WantedBy=multi-user.target
```

- ポート 9090 で画像ディレクトリをそのまま公開
- PC のブラウザで `http://192.168.105.200:9090` にアクセスすると画像一覧が見える
- 家庭内 LAN のみで使用（外部公開はしていない）

---

## 現在動いているサービス一覧

| サービス | ポート | 用途 |
|---------|--------|------|
| `ssh` | 22 | リモートターミナル接続 |
| `seedling-server` | 8080 | ESP32 からのセンサー・画像データ受信 |
| `image-viewer` | 9090 | ブラウザで画像閲覧 |

---

## よく使うコマンド早見表

### SSH 接続（PC 側）

```bash
ssh uzuki@192.168.105.200        # RPi に接続
exit                              # RPi から切断して PC に戻る
```

### ファイル転送（PC 側）

```bash
# PC → RPi にコピー
scp ファイル名 uzuki@192.168.105.200:~/送信先パス/

# RPi → PC にコピー
scp uzuki@192.168.105.200:~/ファイルパス ./
```

### サービス管理（RPi 側）

```bash
sudo systemctl status seedling-server    # 状態確認
sudo systemctl restart seedling-server   # 再起動
sudo journalctl -u seedling-server -f    # ログ監視（Ctrl+C で終了）
```

### データ確認（RPi 側）

```bash
# 最新のセンサーデータ
tail -5 ~/Documents/My_garden/data/seedling_monitor/sensor_2026-03.csv

# 最新の画像
ls -lt ~/Documents/My_garden/data/seedling_monitor/images/ | head -5
```

### ネットワーク確認（RPi 側）

```bash
hostname -I                        # IP アドレスを表示
nmcli connection show              # ネットワーク接続一覧
ping -c 3 google.com              # インターネット接続テスト
```
