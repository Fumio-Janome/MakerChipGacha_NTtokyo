# Maker Chip Gacha (ESP32-C3)

## 概要
Maker Chip Gachaは、ESP32-C3（ESP-IDF）を用いたコインセレクタ式の電子ガチャ貯金箱です。500円硬貨を認識し、ボタン操作で購入履歴をロギング、Wi-Fi経由で設定や履歴閲覧も可能です。

---

## 主な仕様

- **対応MCU**: ESP32-C3（WROOM/mini/SuperMini等）
- **コイン認識**: GPIO4（COIN_SELECTOR_PIN）でパルス入力、500円/100円等を判別
- **ボタン操作**: BOOTボタン（GPIO9）または外部ボタン（GPIO10）
- **LED制御**: オンボードLED（GPIO0）/外部LED（GPIO1）
- **LCD表示**: SPI接続LCD（ピン定義はcommon.h参照）
- **Wi-Fi設定**: Webサーバ経由でSSID/PASS設定
- **NVS保存**: 購入履歴・Wi-Fi設定・カウント等を不揮発領域に保存
- **NTP対応**: 日時ロギングはNTP時刻を利用
- **FreeRTOS**: タスク/キュー/セマフォによるマルチタスク設計

---

## 使い方・操作方法

### 1. 初期セットアップ
- ESP32-C3に本ファームを書き込み、電源ON
- LCDにタイトル・Wi-Fi設定画面が表示されます
- Webブラウザで本体のIPアドレスにアクセスし、SSID/PASSを設定

### 2. コイン投入・購入操作
- 500円硬貨を投入するとLCDに「ボタンを押してください」と表示
- BOOTボタンまたは外部ボタンを押すと、
	- 日時がNVSにロギング
	- 購入カウントが加算
	- LEDやLCDで演出

### 3. ログ閲覧
- 起動時にシリアルターミナルへ購入履歴（日時）が出力されます
- Web設定画面からも履歴参照可能（実装状況による）

### 4. リセット・NVS初期化
- BOOTボタン長押しでカウントや履歴をリセット
- NVS全消去は`nvs_flash_erase()`+リブートで実施
- BUY_LOG_NAMESPACE配下のみ初期化も可能

---

## ピンアサイン（デフォルト: ESP32-C3 Mini）
| 機能         | ピン番号 | マクロ名           |
|--------------|----------|--------------------|
| コイン入力   | GPIO3    | COIN_SELECTOR_PIN  |
| BOOTボタン   | GPIO9    | LOG_BUTTON_PIN   |
| オンボLED    | GPIO0    | SERVO_MOTOR_PIN    |
| 外部LED      | GPIO1    | EXT_LED_PIN        |
| 外部ボタン   | GPIO10   | EXT_BUTTON_PIN     |
| LCD MOSI     | GPIO18   | LCD_MOSI_PIN       |
| LCD SCLK     | GPIO19   | LCD_SCLK_PIN       |
| LCD CS       | GPIO6    | LCD_CS_PIN         |
| LCD DC       | GPIO5    | LCD_DC_PIN         |
| LCD RST      | GPIO4    | LCD_RST_PIN        |
| LCD BLK      | GPIO7    | LCD_BLK_PIN        |

---

## ビルド・書き込み手順
1. ESP-IDF v5.4環境を用意
2. `idf.py menuconfig` でWi-Fiやピン設定を確認
3. `idf.py build` でビルド
4. `idf.py -p (COMポート) flash monitor` で書き込み＆シリアルモニタ

---

## 注意事項・ヒント
- NVS初期化やリセットは慎重に（全データ消去）
- スタックオーバーフロー対策のため大きな配列はmalloc/free推奨
- ログ出力はprintfでTAG無しも可能
- 詳細なピン定義や拡張は`common.h`を参照

---

## ライセンス
MIT License

---

## 作者
Fumio-Janome / MakerChipGacha_NTtokyo
