# Maker Chip Gacha (ESP32-C3)

## 概要
Maker Chip Gachaは、ESP32-C3（ESP-IDF）を用いたMakerCHipガチャ用FWです。
100円or500円or専用コインを認識し、500円投入後ボタン操作でMakerChipを購入できます。
購入履歴をロギングします。

---

## 主な仕様

- **MCU**: ESP32-C3 mini
- **コイン認識**: コインセレクタからのパルスを検出して500円/100円/専用コインを判別
- **ボタン操作**: 外部ボタン、ログ操作用オンボードBOOTボタン
- **LED制御**: 外部LED
- **LCD表示**: SPI接続LCD：タイトル・メッセージ・日時を表示
- **Wi-Fi設定**: Webサーバ経由でSSID/PASS設定
- **NVS保存**: 購入履歴・Wi-Fi設定を不揮発領域に保存
- **NTP対応**: 日時ロギングはNTP時刻を利用
- **FreeRTOS**: タスク/キュー/セマフォによるマルチタスク設計

---

## 使い方・操作方法

### 1. 起動
- LCDにタイトル・メッセージが表示されます。
- 待ちのメッセージ表示。Wifi接続、NTP取得でコイン投入メッセージ表示。
- NTP取得のためのWifi接続設定
  - SSID：Cerevo_MakerChipGacha
  - PASS：12345678
  - IPアドレス：192.168.4.1

### 2. コイン投入・購入操作
- 500円を検知すると外部LEDが点滅し、LCDに「PLEASE PUSH BUTTON」と表示
- 外部ボタンを押すと、
	- MakerChip送出
	- 日時をNVSにロギング
	- 購入カウントを加算

### 3. 購入ログ参照
- Wifi接続設定画面で参照
  - 画面下部のログ参照ボタン押下
- オンボードBOOTボタン操作で参照
  - 短押し：シリアルターミナルへ購入履歴（日時）を出力
  - 5秒長押し：購入履歴をクリア
  - 10秒長押し：NVS初期化(SSIDも含め全クリア)

---

## ピンアサイン（ESP32-C3 Mini）
| 機能         			| ピン番号 | マクロ名           |
|-----------------------|----------|--------------------|
| コイン検知(パルス受信)| GPIO3    | COIN_SELECTOR_PIN  |
| 外部LED      			| GPIO1    | EXT_LED_PIN        |
| 外部ボタン   			| GPIO10   | EXT_BUTTON_PIN     |
| コイン送出サーボモータ| GPIO0    | SERVO_MOTOR_PIN    |
| LCD SCLK     			| GPIO19   | LCD_SCLK_PIN       |
| LCD MOSI     			| GPIO18   | LCD_MOSI_PIN       |
| LCD RST      			| GPIO4    | LCD_RST_PIN        |
| LCD DC       			| GPIO5    | LCD_DC_PIN         |
| LCD CS       			| GPIO6    | LCD_CS_PIN         |
| LCD BLK      			| GPIO7    | LCD_BLK_PIN        |
| オンボードBOOTボタン	| GPIO9    | RESET_BUTTON_PIN   |

---

## ビルド環境
1. VSCode配下ESP-IDF v5.4環境

---

## 注意事項
- NVS初期化やリセットは慎重に（全データ消去）
- 詳細なピン定義や拡張は`common.h`を参照

---

## ライセンス
MIT License

---

## 作者
Fumio-Janome / MakerChipGacha_NTtokyo
