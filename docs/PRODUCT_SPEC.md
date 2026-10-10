# gkcore 製品仕様

## 完成したときに届けるもの

gkcore は Windows 向けの C++ ゲーム描画 SDK です。通常の `main` と `gk::` 関数呼び出しから始められ、同じフレームに 3D シーン、2D のゲーム描画、UI を置けます。The Forge の Direct3D 12 描画部は内部に隠し、学習者はウィンドウ、画像、図形、文字、入力を手軽に使えます。

配布 SDK はゲームの build と実行に必要な header、library、実行ファイルだけに絞ります。source、test、The Forge の開発依存物、build / shader tool は開発用の領域に分けます。

## 利用者向けの完成 API 例

以下は利用者が使う完成 API の形を示します。機能の実装・確認範囲は [ROADMAP.md](ROADMAP.md) で確認できます。

```cpp
#include <gkcore.h>

int main()
{
    if (gk::SetWindowSize(1280, 720) != 0 || gk::Init() != 0)
    {
        return 1;
    }

    // 3Dのシーンに使うモデルと、2D表示する画像。
    const auto world = gk::LoadModel("assets/world.glb");
    const auto hero = gk::LoadImage("assets/hero.png");
    if (!world || !hero)
    {
        gk::Shutdown();
        return 1;
    }

    // カメラと、Sceneへ適用する標準ポストエフェクト。
    gk::SetCamera(gk::Vec3{0.0f, 2.0f, -8.0f}, gk::Vec3{0.0f, 0.0f, 0.0f});
    gk::SetBloomEnabled(true);
    gk::SetBloomIntensity(0.2f);
    gk::SetSaturation(1.1f);
    gk::SetContrast(1.05f);
    gk::SetFxaaEnabled(true);
    gk::SetToneMappingEnabled(true);

    // Sceneを描き、ポスト処理後にUIを重ねる。
    while (gk::ProcessEvents() && !gk::IsKeyDown(gk::Key::Escape))
    {
        if (gk::BeginFrame() != 0)
        {
            break;
        }

        gk::SetDrawLayer(gk::DrawLayer::Scene);
        gk::DrawModel(world);
        gk::DrawImageRotated(hero, 640.0f, 360.0f, 1.0f, 0.0f, true);
        gk::SetDrawLayer(gk::DrawLayer::UI);
        gk::DrawRect(20.0f, 20.0f, 160.0f, 40.0f, gk::ColorRGB(30, 36, 52), true);
        if (gk::Present() != 0)
        {
            break;
        }
    }

    // この例で読み込んだ資源を解放して終了する。
    gk::DeleteImage(hero);
    gk::DeleteModel(world);
    gk::Shutdown();
    return 0;
}
```

この例は完成後に目指す利用形です。PNG画像、GLBモデル、標準ポストエフェクトを使う経路は実装され、Windows GPU画像とサンプル画面で日本語の表示も確認済みです。モデルの照明範囲と検証状況は[機能サポート状況](ROADMAP.md)に記載します。

## 完成要件

### 使い始めやすさとライフサイクル

- 現在の固定依存が使う開発・検証環境は Windows 10/11 x64、MSVC v142 14.29、C++17 である。Visual Studioのバージョンを不変の製品要件とはせず、依存構成に応じて更新する。呼び出し側はThe Forgeのheaderを含まず、通常の `main()` を使う。Runtime API、実装、配布サンプルはC++標準ライブラリ/STLに依存させず、開発用のtest/toolでは使用できる。
- 初期化、終了、メッセージ処理、キー/マウス入力、描画開始/終了の短い手順を 1 ページの日本語チュートリアルで説明する。
- 初期ウィンドウサイズ、VSync、深度テストなどに学習向けの既定値を持つ。
- [開発で具体化した受入目標] 初学者向け説明の実用性を出荷前に測る。C++の基礎がありVisual Studioを導入済みの学生3名に1ページガイドを渡し、2名以上が30分以内に最初の画像付きサンプルを起動できることを目標とする。この人数と時間は開発側で置いた検証基準であり、試験は未実施である。
- リサイズ後も表示領域と 2D 座標が正しく更新される。終了/デバイス喪失時にリソースを安全に解放または復旧し、失敗原因を `gk::GetLastErrorMessage()` で明瞭な日本語または英語で確認できる。
- `gk::Vec3` など型を使った数学 API を備え、生の配列を初心者が手で用意する頻度を減らす。カメラはフレーム開始後の設定変更を以降の描画命令に反映する。ポストエフェクト設定は `BeginFrame()` でフレームへ複写し、描画層は各命令を追加した時点で記録する。

### 2D と 3D

- 3D のシーン描画は深度テストを使う。不透明描画は命令順を保ち、BLEND材質は同じcameraを使う連続した標準Model draw群の中でtriangleを奥から手前へ並べる。非モデル/custom描画とcamera変更はsortの境界にする。UI層はポスト処理後に重ね、深度を使わず命令順を保つ。SceneとUIの順序は層の指定で決まり、両層をまたいだ全体の呼び出し順とは異なる。
- 深度テストのある 3D 描画と、画面座標の 2D 描画を明快に使い分けられる。2D/3D の切替を毎回明示的なパス構築として利用者に要求しない。
- 画像はアルファ付き PNG と BMP を読み込める。2D には拡大縮小、回転、透明度、ブレンドと滑らかな拡大縮小を備える。
- `gk::DrawRectOutline(x, y, width, height, color, thickness=1)` で線幅を指定した輪郭矩形を描く。座標、寸法、`x + width`、`y + height`、太さは有限で、幅・高さ・太さは正の値だけを受け付ける。線は指定した外枠の内側へ収め、`2 * thickness >= min(width, height)` なら外枠全体を塗りつぶす。Scene/UI の層と描画 shader を通常の矩形と共有し、UV は矩形全体で連続させる。
- `gk::DrawString` でUTF-8の日本語文字列をシステム標準フォントで表示し、色・サイズを簡単に変えられる。Windows GPU画像とサンプル画面で日本語表示を確認済み。
- 3DモデルはGLB/glTF 2.0を基本形式にし、mesh、PBR材質、texture、cameraを読み込む。モデルの位置/回転/拡大率を関数で設定できる。対応形式ごとのanimation要件は後述の必須対応節を参照する。
- FBX は ASCII / バイナリ形式のメッシュと基本色材質を読み込み、GLB / OBJ と同じ `gk::LoadModel` API で使える。右手系の Y-up、メートル単位、階層・幾何変換、UV の画像原点変換を一貫して扱う。clip、skin、morphも扱い、共通のブレンド・ボーン対応・IKへ接続する。
- 一様な環境光とワールド空間の方向光を簡単な設定で調整できる。初期値でも GLB モデルの面の向きと丸みを認識できる。
- `SetAmbientLight(float)` は有限な `[0, 4]` を受け付け、初期値を `0.2` とする。`SetDirectionalLight(Vec3, float)` は有限な非ゼロ方向と有限な `[0, 16]` 強度を受け付け、方向を内部で正規化し、既定強度を `3.0` とする。方向は光がワールド空間を進む向きを表す。
- 照明値は `BeginFrame()` ごとに取り込まれ、`Shutdown()` 後に初期値へ戻る。無効値で以前の有効値を変更しない。
- モデル専用材質shaderはGLBの基本色係数・画像と、metallic / roughnessの係数・画像を使う。基本色はsRGB、金属度・粗さの画像は線形値で扱い、Gの粗さ・Bの金属度へ材質係数を掛ける。各画像のUV指定とsamplerを独立して保持する。標準GLBモデルはRepeat/MirroredRepeat/ClampToEdgeとNearest/Linearの拡大・縮小補間に対応し、ミップ指定の画像は色空間を保って1×1まで生成し、画素間と縮小段の補間指定をそれぞれ保持する。occlusionTextureは線形Rとstrengthから環境光を残す割合を求め、方向光と自己発光を変えずに環境光だけへ適用する。独立UVとsamplerを保持し、MR画像の同じ線形ミップ構成は共有する。自己発光はemissiveFactor・emissiveTexture・KHR_materials_emissive_strengthを使い、線形RGBの発光を反射光に加えてHDRへ渡す。画像のアルファは使わず、UVとsamplerは独立して保持する。NORMALがないGLBは三角形ごとの面法線を生成し、鋭い辺で頂点を分ける。法線欠損時の入力TANGENTは無視する。法線画像を使いTANGENTが欠けている場合は、法線画像のUVから接線を生成する。生成はnode変換前に行い、接線の不連続には頂点分割で対応する。normalTextureはRGBを線形接線法線へ展開し、scaleをXYへ掛け、node/runtime鏡映の向きを保持する。拡散反射は Lambert、直接光の鏡面反射は GGX 分布、Fresnel、Smith masking-shadowing を使う。均一な環境光は簡易な Lambert 寄与とし、環境マップとは区別する。
- 頂点法線はワールド変換の逆転置で変換し正規化する。法線がない三角形には面法線を使い、退化面には不正な法線を生成しない。
- 2D図形・画像・文字と公開カスタムpixel shaderはunlitで描画する。モデルはUI層でも材質照明を使い、UI合成位置に従う。利用者が差し替える3Dモデル材質shader ABIは完成目標に含むが、現時点では未実装である。
- GLBの標準材質はOPAQUE、MASK、BLENDに対応する。OPAQUEはalphaを無視し、MASKでは画像のalphaと基本色のalpha係数を掛けて判定する。alphaCutoff未満の画素を破棄して色と深度を更新せず、境界値と等しい画素は残す。BLENDは同じalpha積をcoverageとして、基本色RGBをstraight alphaで合成する。Sceneの同一cameraを使う連続した標準Model draw群では、OPAQUE/MASKを深度書き込み付きで先に描き、BLEND triangleを重心view depthで奥から手前へsortする。非モデル/custom描画とcamera変更はsort範囲を区切って命令順を保ち、UIモデルは深度なしで元の順に合成する。BLENDはSceneで深度テストを行い、深度を書き込まない。交差面・循環重なりは重心sortで完全には解決できない。透過入力triangleはframe全体で最大349,525件で、clip外のtriangleも数える。clip後の頂点上限1,048,576件は他の描画と共有し、入力上限以内でもPresent成功を保証しない。独自pixel shaderはalpha mode情報を受け取れないため、MASK/BLEND材質との組み合わせを拒否する。
- 影と環境マップ / IBLは完成時に標準機能として対応する。いずれも現時点では未実装で、現在の対応範囲と確認状況は機能一覧へ記載する。

### ポストエフェクトとシェーダー

- 初心者は少数の関数または設定オブジェクトで Bloom、色調整、ぼかしなどを有効化・調整できる。
- 2D ゲーム内シーンと 3D シーンを含む描画結果にエフェクトを適用できる。HUD/文字などの UI はポスト処理後に描画でき、読みやすさを保つ。
- 2D だけのゲームでも Bloom、色調整などを同じ簡単な API で利用できる。
- 上級者は HLSL のカスタム pixel shader を 2D スプライト/図形へ適用できる。3D 材質用と Scene の合成 HDR 画像を読むポスト用 shader には、それぞれ明確な ABI を用意する。ポスト用 shader は `gk::SetPostEffectShader` で選び、定数は `gk::SetShaderFloat4` で設定する。内蔵 Bloom / 露出 / 色調整 / FXAA の前に適用し、UI には適用しない。shader handle、Float4 定数、画像入力の ABI とフレームへの設定取り込み時点を公開仕様として固定する。shader は開発用ツールでコンパイルし、コンパイラーは Runtime SDK に含めない。
- シェーダーコンパイル/ロードの失敗ではファイル名、エラー箇所、診断内容を取得できる。サンプルシェーダーと変更手順を同梱する。
- 色空間、alpha 合成、ポスト処理の適用範囲を文書化し、既定 UI 色が露出/Bloom で意図せず変わらない。

### リリース SDK

- Runtime パッケージから空のサンプルプロジェクトをビルド・起動できる。
- Runtime マニフェストには公開ヘッダー、リンク用ライブラリ、実際に必要な DLL/ランタイムデータだけを列挙する。
- Runtime に内部ソース、テスト、The Forge の開発ソース/ヘッダー、開発ツール、テストアセット、デバッグシンボルを含めない。
- 開発/テスト/SDK パッケージは別々に生成でき、manifest に対する自動 test で混入を検出する。
- The Forge の版とライセンス、同梱物と再配布条件を明記する。

## 完了判定

製品は以下の共通完了条件と、後述するモデルアニメーションの必須対応をすべて満たしたとき完成とします。

1. Windows 10/11 x64 の新しい環境で Runtime SDK だけを使い、The Forge の header を含まない通常の `main()` サンプルを build して起動できる。GPU 統合 test は Windows / DX12 実機上で実行する。固定した The Forge は software adapter を選べないため、upstream に adapter 注入の変更を加えない限り WARP での実行は要件に含めない。
2. サンプルの同じフレームにGLBのPBR 3Dシーン、アルファ付きPNGの2Dスプライト、図形、UTF-8日本語UI文字を描ける。別の照明サンプルでは同一形状の非金属材質と金属材質を描き、方向光を操作して面の明暗と反射の違いを観察できる。
3. HDR、Bloom、トーンマップ、色調整、FXAAが2D/3Dシーンに反映し、UIを後段で合成して読みやすさを保つ。Windows/DX12実機CIではGPUごとに保存した基準画像と比較する。現在の開発受入基準は、同じGPU・driver・画像で、RGBのいずれかが2/255を超えて異なる画素の割合を1%以下、全RGB channelの平均絶対誤差を2/255以下とする。この許容値は開発側で具体化した回帰基準であり、ユーザーが指定した数値ではない。異なるGPU間で同じ画像になることは求めない。
4. Direct3D 12向けHLSL/FSLを開発用のForge FSL toolchainとDXCで編集・compileし、生成された `@FSL` artifact内のDXIL stageをRuntimeが選んで読み込める。頂点・pixel・post-effectに加え、3Dモデル材質shaderの公開ABIを定義し、各ABIに従って描画できる。RuntimeにFSL/DXC compiler toolを含めず、構文errorはsource上の位置とともに報告する。
5. ウィンドウの resize、終了処理、device 再初期化、無効な handle/file、描画初期化失敗、shader reload を自動 test または再現手順で確認し、理由を取得できる。
6. 自動 test がすべて成功し、Runtime SDK だけを使う利用者 sample の build が通り、内容検査で開発専用ファイルの混入が 0 件である。
7. モデルアニメーションの必須対応を満たす。全対応モデル形式のanimation、外部motionの互換適用、人型役割対応、blend、IK、実モデルでの変形品質、髪・スカート・アクセサリー等の揺れものを確認する。実モデルでanimation・blend・IK・揺れものを使う代表設定において、開発側が固定した基準機器と条件で平均300FPS以上を達成する。基準機器・解像度・効果などの測定条件は[モデルviewerの処理時間](model-performance.md)に記録する。この性能値は全機器での保証を意味しない。

## ライセンスと再配布

gkcore 自身の配布ライセンスはプロジェクト所有者が決めるまで未確定です。The Forge の開発用 checkout は `cmake/forge-files-lock.json` の commit に固定し、source は別 directory へ取得します。The Forge 自身の LICENSE が Apache-2.0 であることは確認済みですが、内包する第三者 software と Runtime に含める DLL 群の再配布条件は未監査です。最終 manifest を公開する前に依存一式を確認し、必要な notice を整えます。

## 対象範囲

初版は2D/3Dの描画、入力、画像/モデル/フォント、標準ポストエフェクト、ユーザーシェーダー、配布SDKを対象にします。一般的なrigid-body物理engine、ネットワーク、エディター、他ライブラリの全関数との完全互換、全プラットフォーム/全GPU対応は対象外です。一方、モデルanimationに重ねる髪・スカート・アクセサリー等のbone揺れものは必須範囲です。オーディオやゲーム固有のシーン管理も、描画APIと分けて将来判断します。

## モデルアニメーションの必須対応

以下はユーザーが明示した完成要求です。一般的な人型骨格へ外部motionを適用できること、特定のmotion提供元だけに限らず互換性を判断して骨の役割を対応付けること、motionのblendとIKを提供すること、実モデルで変形品質を確認することを含む。実モデルを表示する動作中の描画で300FPS以上を目標とする。FPSの計測機器、解像度、標準効果などは再現性のため開発側で定めた条件であり、機器を問わない性能保証を意味しない。現行の基準機器と未達modeは[モデルviewerの処理時間](model-performance.md)に記録する。

対応するすべてのモデル形式をアニメーション可能にします。GLBとFBXはファイル内のアニメーション、OBJは連番モデルを基本方式とします。アニメーションだけのデータを読み込み、互換性のあるモデルへ適用する機能も提供します。再生clip、時刻、速度、loopをモデルごとに設定し、同じモデルデータを複数の再生状態で利用できるようにします。描画予約時点の姿勢を保持し、予約後の時刻変更やhandle削除でその描画結果を変えません。

骨・階層や頂点構成を検査して外部データを結び付けます。対応形式全体を必須範囲として扱い、共通APIの土台だけを完成としません。実装状況と確認済みの機能は機能一覧へ個別に記録します。

アニメーション同士のblendと、ヒューマノイドを含む汎用ボーンの役割対応、IKも必須機能です。名前・階層互換の外部clip適用に加え、ボーン役割を指定してモデル間の姿勢対応を行います。IKは手足の2ボーンと汎用チェーンを対象にし、予約時点の解決済み姿勢を保持します。透明材質のBLENDとアニメーションblendは別機能として実装・検証します。

自然な変形を確認するための開発課題として、twist補助骨への回転配分と大きなIK目標での肩の変形品質を検証します。具体的な補正方式は開発側で選ぶ実装手段です。現在の実装範囲と残課題は機能一覧に記録し、特定の数値だけで自然な変形品質を判定しません。

揺れものはユーザーが明示した完成範囲です。`SetModelSecondaryMotionChain`で連続したbone鎖をinstanceへ登録し、任意で`SetModelSecondaryMotionColliders`から最大64個の身体球・カプセルを登録します。`UpdateModelSecondaryMotion`は現在のanimation・blend・IK姿勢へ重力・風・ばね・減衰を加え、bone長と基準姿勢からの最大曲げ角を保ちながら、各節と鎖線分の離散接触を解きます。設定ファイルは既存の9数値行を保ち、任意の10個目の数値で鎖ごとの制約反復数（1〜32）を上書きできます。髪2本・尻尾1本・スカート10本の鎖と身体形状8個を含むYUMEKA設定、START 12を用意しました。身体接触付きの2区間を各120frame、Debug/Releaseで再生し、計480frameのGPU位置・法線と接触制約を確認しました。保存24組の画像は構成間で一致しています。接近時の揺れと角度・接触品質の判定は継続中です。API、設定、制約は[揺れもの](model-secondary-motion.md)に記載します。

揺れもの状態はモデルinstanceごとに保持し、共有model resourceや別instanceの時間・姿勢へ混ぜません。明示的な更新だけで進め、animation・blend・IKの後、skin処理の前に反映します。骨位置の照会と描画は確定済み姿勢を読むだけなので、複数回呼んでも物理時刻は進みません。停止中も更新を続ければ速度が減衰し、重力を含む力の平衡姿勢へ落ち着きます。重力がある場合は元animation姿勢から少しずれることがあります。`ResetModelSecondaryMotion`は元animation姿勢へ戻し、次の更新で身体接触を解きます。接触制約を満たせない更新は失敗し、直前の確定状態を保ちます。`ClearModelSecondaryMotion`はinstanceの設定と状態を解放します。大きなanchor移動または0.25秒以上更新間隔では鎖を姿勢へ再配置します。

適用対象は書き換え可能な骨格を持つGLB / FBX等のskeletal modelです。連番OBJを含む非skeletal animationには適用しません。正のほぼ一様な祖先scaleだけを受け付け、各軸の相対幅は `128 * FLT_EPSILON` 以下とします。身体の球・カプセルとの離散接触は実装済みですが、更新間の高速なすり抜けを保証する連続判定、cloth mesh、自己衝突、スカート隣接鎖の連結制約、world SRTに対する慣性、固定時間刻みの物理更新は未対応です。身体接触付きのNative描画は確認しましたが、一部のスカートmeshが脚へ埋まる残件と、任意motionの品質確認が残ります。これらは一般物理engineの機能とは分けて扱います。具体的なばね式や数値上限は開発側で選んだ実装条件であり、ユーザー指定の方式ではありません。
