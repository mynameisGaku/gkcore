# gkcore 製品仕様

## 完成したときに届けるもの

gkcore は Windows 向けの C++ ゲーム描画 SDK です。通常の `main` と `gk::` 関数呼び出しから始められ、同じフレームに 3D シーン、2D のゲーム描画、UI を置けます。The Forge の Direct3D 12 描画部は内部に隠し、学習者はウィンドウ、画像、図形、文字、入力を手軽に使えます。

配布 SDK はゲームの build と実行に必要な header、library、実行ファイルだけに絞ります。source、test、The Forge の開発依存物、build / shader tool は開発用の領域に分けます。

## 利用者向けの完成 API 例

以下は利用者が使う予定の API を示します。機能の実装範囲は [ROADMAP.md](ROADMAP.md) で確認できます。

```cpp
#include <gkcore.h>

int main() {
    if (gk::SetWindowSize(1280, 720) != 0 || gk::Init() != 0) return 1;

    const auto world = gk::LoadModel("assets/world.glb");
    const auto hero = gk::LoadImage("assets/hero.png");
    if (!world || !hero) {
        gk::Shutdown();
        return 1;
    }

    gk::SetCamera(gk::Vec3{0.0f, 2.0f, -8.0f}, gk::Vec3{0.0f, 0.0f, 0.0f});
    gk::SetBloomEnabled(true);
    gk::SetBloomIntensity(0.2f);
    gk::SetSaturation(1.1f);
    gk::SetContrast(1.05f);
    gk::SetFxaaEnabled(true);
    gk::SetToneMappingEnabled(true);

    while (gk::ProcessEvents() && !gk::IsKeyDown(gk::Key::Escape)) {
        if (gk::BeginFrame() != 0) break;
        gk::SetDrawLayer(gk::DrawLayer::Scene);
        gk::DrawModel(world);
        gk::DrawImageRotated(hero, 640.0f, 360.0f, 1.0f, 0.0f, true);
        gk::SetDrawLayer(gk::DrawLayer::UI);
        gk::DrawRect(20.0f, 20.0f, 160.0f, 40.0f, gk::ColorRGB(30, 36, 52), true);
        if (gk::Present() != 0) break;
    }

    gk::DeleteImage(hero);
    gk::DeleteModel(world);
    gk::Shutdown();
    return 0;
}
```

この例は完成後に目指す利用形です。PNG 画像の読み込みと画像描画、GLB の基本色係数・画像をモデルへ適用する経路は実装されていますが、Windows/MSVC でのリンクと実 GPU 上の表示は未検証です。PBR 照明、影、環境マップ、alpha mode は未対応です。`gk::DrawString` は UTF-8 文字列をシステム標準フォントで表示する API として実装済みですが、Windows 上の文字表示は未検証です。

## 完成要件

### 使い始めやすさとライフサイクル

- Windows 10/11 x64、Visual Studio 2022、C++17 で SDK の最小サンプルを新しい環境から build できる。呼び出し側は The Forge の header を含まず、通常の `main()` を使う。Runtime API、実装、配布サンプルは C++ 標準ライブラリ/STL に依存しない。開発用の test/tool は別枠で標準ライブラリを使える。
- 初期化、終了、メッセージ処理、キー/マウス入力、描画開始/終了の短い手順を 1 ページの日本語チュートリアルで説明する。
- 初期ウィンドウサイズ、VSync、深度テストなどに学習向けの既定値を持つ。
- 初学者向け説明の実用性を出荷前に測る。C++ の基礎があり Visual Studio を導入済みの学生 3 名に 1 ページガイドを渡し、2 名以上が 30 分以内に最初の画像付きサンプルを起動できることを目標とする。試験は未実施であり、出荷前に結果を記録する。
- リサイズ後も表示領域と 2D 座標が正しく更新される。終了/デバイス喪失時にリソースを安全に解放または復旧し、失敗原因を `gk::GetLastErrorMessage()` で明瞭な日本語または英語で確認できる。
- `gk::Vec3` など型を使った数学 API を備え、生の配列を初心者が手で用意する頻度を減らす。カメラはフレーム開始後の設定変更を以降の描画命令に反映する。ポストエフェクト設定は `BeginFrame()` でフレームへ複写し、描画層は各命令を追加した時点で記録する。

### 2D と 3D

- 3D のシーン描画命令は深度テストを使い、同じ層の命令順を保つ。UI 層はポスト処理後に重ね、UI 層の中でも命令順を保つ。Scene と UI の順序は層の指定で決まり、両層をまたいだ全体の呼び出し順とは異なる。
- 深度テストのある 3D 描画と、画面座標の 2D 描画を明快に使い分けられる。2D/3D の切替を毎回明示的なパス構築として利用者に要求しない。
- 画像はアルファ付き PNG と BMP を読み込める。2D には拡大縮小、回転、透明度、ブレンドと滑らかな拡大縮小を備える。
- `gk::DrawString` で UTF-8 の日本語文字列をシステム標準フォントで表示し、色・サイズを簡単に変えられる。Windows 上の表示確認は残っている。
- 3D モデルは GLB/glTF 2.0 を基本形式にし、静的 mesh の PBR 材質、texture、camera を読み込む。モデルの位置/回転/拡大率を関数で設定できる。
- FBX は ASCII / バイナリ形式の静的メッシュと基本色材質を読み込み、GLB / OBJ と同じ `gk::LoadModel` API で使える。右手系の Y-up、メートル単位、階層・幾何変換、UV の画像原点変換を一貫して扱う。アニメーション、スキニング、モーフは対象外。
- 標準的な環境光、環境マップ、影を簡単な設定で利用でき、初期値でもモデルの形状が認識しやすい。

### ポストエフェクトとシェーダー

- 初心者は少数の関数または設定オブジェクトで Bloom、色調整、ぼかしなどを有効化・調整できる。
- 2D ゲーム内シーンと 3D シーンを含む描画結果にエフェクトを適用できる。HUD/文字などの UI はポスト処理後に描画でき、読みやすさを保つ。
- 2D だけのゲームでも Bloom、色調整などを同じ簡単な API で利用できる。
- 上級者は HLSL のカスタム pixel shader を 2D スプライト/図形へ適用できる。配布 SDK では別途定義した ABI を介して 3D 材質とポストエフェクトにもカスタム shader を適用できる。頂点入力、定数/texture、出力、色空間、alpha の ABI を公開仕様として固定する。shader は開発用 tool で compile し、compiler を Runtime SDK に含めない。
- シェーダーコンパイル/ロードの失敗ではファイル名、エラー箇所、診断内容を取得できる。サンプルシェーダーと変更手順を同梱する。
- 色空間、alpha 合成、ポスト処理の適用範囲を文書化し、既定 UI 色が露出/Bloom で意図せず変わらない。

### リリース SDK

- Runtime パッケージから空のサンプルプロジェクトをビルド・起動できる。
- Runtime マニフェストには公開ヘッダー、リンク用ライブラリ、実際に必要な DLL/ランタイムデータだけを列挙する。
- Runtime に内部ソース、テスト、The Forge の開発ソース/ヘッダー、開発ツール、テストアセット、デバッグシンボルを含めない。
- 開発/テスト/SDK パッケージは別々に生成でき、manifest に対する自動 test で混入を検出する。
- The Forge の版とライセンス、同梱物と再配布条件を明記する。

## 完了判定

製品は以下をすべて満たしたとき完成とします。

1. Windows 10/11 x64 の新しい環境で Runtime SDK だけを使い、The Forge の header を含まない通常の `main()` サンプルを build して起動できる。GPU 統合 test は Windows / DX12 実機上で実行する。固定した The Forge は software adapter を選べないため、upstream に adapter 注入の変更を加えない限り WARP での実行は要件に含めない。
2. サンプルの同じフレームに静的 GLB の PBR 3D シーン、アルファ付き PNG の 2D スプライト、図形、UTF-8 日本語 UI 文字を描ける。
3. HDR、Bloom、トーンマップ、色調整、FXAA が 2D/3D シーンに反映し、明示的に UI を後段で合成して読みやすさを保つ。Windows/DX12 実機 CI では、GPU ごとに保存した基準画像と比較する。同じ GPU、driver、画像内で RGB のいずれかが 2/255 を超えて異なる画素の割合を 1% 以下、全 RGB channel の平均絶対誤差を 2/255 以下とする。異なる GPU 間で同じ画像になることは求めない。
4. Direct3D 12 向け HLSL/FSL を開発用の Forge FSL toolchain と DXC で編集・compile し、生成された `@FSL` artifact 内の DXIL stage を Runtime が選んで読み込める。頂点/ピクセル/ポストエフェクト ABI に従う。Runtime に FSL/DXC compiler tool を含めない。構文 error は source 上の位置とともに報告する。
5. ウィンドウの resize、終了処理、device 再初期化、無効な handle/file、描画初期化失敗、shader reload を自動 test または再現手順で確認し、理由を取得できる。
6. 自動 test がすべて成功し、Runtime SDK だけを使う利用者 sample の build が通り、内容検査で開発専用ファイルの混入が 0 件である。

## ライセンスと再配布

gkcore 自身の配布ライセンスはプロジェクト所有者が決めるまで未確定です。The Forge の開発用 checkout は `cmake/forge-files-lock.json` の commit に固定し、source は別 directory へ取得します。The Forge 自身の LICENSE が Apache-2.0 であることは確認済みですが、内包する第三者 software と Runtime に含める DLL 群の再配布条件は未監査です。最終 manifest を公開する前に依存一式を確認し、必要な notice を整えます。

## 対象範囲

初版は 2D/3D の描画、入力、画像/モデル/フォント、標準ポストエフェクト、ユーザーシェーダー、配布 SDK を対象にします。物理エンジン、ネットワーク、エディター、他ライブラリの全関数との完全互換、全プラットフォーム/全 GPU 対応は対象外です。オーディオやゲーム固有のシーン管理も、描画 API と分けて将来判断します。
