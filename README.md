# gkcore

gkcore は Windows 向けの C++ 描画フレームワークです。ゲームからは `<gkcore.h>` と `gk::` API を使います。

ゲームの最小構成とフレームの流れは[クイックスタート](docs/quickstart.md)、画像・モデル・入力・描画効果の使い方は各ガイドを参照してください。

この checkout から Runtime とサンプルを構築するには、Windows 10/11 x64、Visual Studio 2022 または 2026 の C++ 開発環境、v142 14.29 toolset、Windows SDK 10.0.22621.0、対応する CMake、Python 3.9 以降が必要です。`PRE_SETUP.bat` が固定版 The Forge と DXC を取得し、Runtime とサンプルをビルドします。

The Forge、DXC、gkcore を含む third-party の配布条件は[通知一覧](docs/THIRD_PARTY_NOTICES.md)を確認してください。

- [クイックスタート](docs/quickstart.md)
- [キー入力](docs/input.md)
- [画像](docs/images.md)
- [モデル](docs/models.md)
- [モデル照明](docs/lighting.md)
- [ポストエフェクト](docs/effects.md)
- [カスタムシェーダー](docs/custom-shader.md)
- [Runtime SDK の構成](docs/package-layout.md)
