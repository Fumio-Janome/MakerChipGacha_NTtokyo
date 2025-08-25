<!-- Use this file to provide workspace-specific custom instructions to Copilot. For more details, visit https://code.visualstudio.com/docs/copilot/copilot-customization#_use-a-githubcopilotinstructionsmd-file -->

# ESP32-WROOM コインセレクタ貯金箱プロジェクト

このプロジェクトはESP-IDFフレームワークを使用したESP32-C3向けの組み込みソフトウェアです。

## 開発ガイドライン

### コーディング規約
- C言語のコードはESP-IDFの標準的な書式に従ってください
- 関数名と変数名は英語またはローマ字を使用
- ログメッセージは日本語で記述
- GPIO定義は明確なマクロ名を使用

### ESP32-WROOM固有の考慮事項
- GPIO4をコインセレクタのパルス入力として使用
- GPIO8を内蔵LEDとして使用
- ESP32-WROOMのRISC-Vアーキテクチャに対応
- 電力消費を考慮した実装

### 割り込み処理
- ISRハンドラは最小限の処理に留める
- キューを使用してメインタスクに処理を委譲
- デバウンス処理を適切に実装

### FreeRTOS使用方針
- タスクベースの設計を採用
- 適切なタスク優先度を設定
- キューやセマフォを活用したタスク間通信

### 拡張機能の開発時
- 既存のGPIO定義を変更する場合は配線図も更新
- 新機能追加時はREADME.mdも更新
- コインの種類を増やす場合は構造体設計を見直し
