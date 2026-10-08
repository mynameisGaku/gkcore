# モデルを読み込む

`gk::LoadModel` に UTF-8 のファイルパスを渡すと、静的モデルのハンドルが返ります。OBJ、GLB 2.0、FBX を読み込めます。読み込んだモデルは位置・回転・大きさを設定して `gk::DrawModel` で描画し、使い終えたら `gk::DeleteModel` で解放します。読み込みに失敗すると無効なハンドルが返るため、`gk::GetLastErrorMessage()` で理由を確認してください。

```cpp
#include <gkcore.h>
#include <stdio.h>

/**
 * API失敗を操作名と診断付きで出力する。
 */
bool Check(int result, const char* operation)
{
    if (result == 0)
    {
        return true;
    }
    fprintf(stderr, "%s: %s\n", operation, gk::GetLastErrorMessage());
    return false;
}

/**
 * モデルを読み込み、終了要求またはエラーまで描画する。
 */
int main()
{
    if (!Check(gk::SetWindowSize(1280, 720), "SetWindowSize") || !Check(gk::Init(), "Init"))
    {
        gk::Shutdown();
        return 1;
    }

    // 読み込んだモデルのハンドル。
    const gk::ModelHandle scene = gk::LoadModel("assets/scene.glb");
    if (!scene.IsValid())
    {
        fprintf(stderr, "LoadModel: %s\n", gk::GetLastErrorMessage());
        gk::Shutdown();
        return 1;
    }

    // 後続の描画へ適用するカメラとモデル変換。
    bool failed = !Check(gk::SetCamera(gk::Vec3{ 0.0f, 0.0f, -5.0f }, gk::Vec3{ 0.0f, 0.0f, 0.0f }), "SetCamera") || !Check(gk::SetModelPosition(scene, gk::Vec3{ 0.0f, 0.0f, 0.0f }), "SetModelPosition") || !Check(gk::SetModelRotation(scene, gk::Vec3{ 0.0f, 0.0f, 0.0f }), "SetModelRotation") || !Check(gk::SetModelScale(scene, gk::Vec3{ 1.0f, 1.0f, 1.0f }), "SetModelScale");
    while (!failed)
    {
        if (!gk::ProcessEvents())
        {
            // 終了要求は診断が空で、イベント処理の失敗時は診断が入る。
            const char* diagnostic = gk::GetLastErrorMessage();
            if (diagnostic && diagnostic[0])
            {
                fprintf(stderr, "ProcessEvents: %s\n", diagnostic);
                failed = true;
            }
            break;
        }
        if (gk::IsKeyDown(gk::Key::Escape) || gk::WasKeyPressed(gk::Key::Escape))
        {
            break;
        }
        if (!Check(gk::BeginFrame(), "BeginFrame") || !Check(gk::SetDrawLayer(gk::DrawLayer::Scene), "SetDrawLayer(Scene)") || !Check(gk::DrawModel(scene), "DrawModel") || !Check(gk::Present(), "Present"))
        {
            failed = true;
        }
    }

    failed = !Check(gk::DeleteModel(scene), "DeleteModel") || failed;
    gk::Shutdown();
    return failed ? 1 : 0;
}
```

`gk::SetModelPosition`、`gk::SetModelRotation`、`gk::SetModelScale` は後続の `DrawModel` に使う変換を設定し、それぞれ失敗時にエラーを返します。`ProcessEvents()` が `false` の場合は `GetLastErrorMessage()` を確認し、診断が空ならウィンドウ終了、文字列があればイベント処理の失敗として扱います。Escape は押した瞬間と押し続けている状態のどちらでも終了できます。

## GLB の画像と UV

GLB 2.0 の `baseColorTexture` は、`texCoord` が示す `TEXCOORD_n` の UV（画像上のどこを読むかを示す座標）で画像を参照します。`texCoord` を省略した場合は `TEXCOORD_0`、明示した場合は指定番号のUVセットを使います。たとえば `texCoord: 1` は `TEXCOORD_1` を選びます。詳細は [glTF 2.0仕様の Texture Info](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#_textureinfo_texcoord) を参照してください。

`baseColorTexture`、`metallicRoughnessTexture`、`normalTexture`、`emissiveTexture`、`occlusionTexture` はそれぞれ独立して `texCoord` を選べます。UV座標の対応形式と、sparse accessor・`KHR_texture_transform` の制約は5種類に共通です。

`texCoord` の省略と明示値0、1、2、および normalized U16 / U8 のUV1を使う6種類のGLBを、Release・Debug構成でGPU画像検査しました。UV0とUV1で模様の左右が切り替わることを確認しています。サポート対象の座標がモデルにない場合、負の番号、対応しない成分型、位置とUVの頂点数不一致は読み込み時に診断付きで拒否します。座標の成分は32bitの浮動小数、または0から1へ正規化する符号なし8bit・16bit整数を使えます。UV sparse accessor（座標の一部だけを差分として格納する形式）と、画像に追加の座標変換を加える `KHR_texture_transform` は未対応です。仕様の拡張内容は [KHR_texture_transform](https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_texture_transform/README.md) を参照してください。

![TEXCOORD_0を使ったGLBの画像](images/glb-uv0.png)

UV0では、画像の赤・緑・青・白の四隅がモデルの各頂点側にそのまま現れます。

![TEXCOORD_1を使ったGLBの画像](images/glb-uv1.png)

UV1では左右が反転し、指定されたUVセットが切り替わったことを確認できます。GPU検査の設定と実行範囲は[描画検証](render-validation.md)を参照してください。

GLBの画像は埋め込みPNGに対応します。外部画像、UV sparse、`KHR_texture_transform`は未対応です。GLBの基本色画像のRGBはsRGB、材質係数・MR画像・法線画像は線形値として扱います。標準のモデル描画では、textureごとのsamplerで繰り返し・鏡映・端で固定する指定を反映します。

## GLB の metallic-roughness 画像

GLBの `metallicRoughnessTexture` は、Gチャンネルから粗さ、Bチャンネルから金属度を読みます。これらの値は線形値として扱い、それぞれ材質の `roughnessFactor` と `metallicFactor` を掛けて使います。RとAは使いません。画像が指定されていない場合はGとBがともに1の白い値を使い、材質係数だけで調整します。基本色画像のRGBはsRGBとして読み、metallic-roughness画像は線形データとして扱うため、同じPNGを両方の役割に指定した場合も読み取り時の色変換は役割ごとに異なります。両方の画像は埋め込みPNGに対応します。

基本色画像とmetallic-roughness画像のUV選択は独立しています。たとえば基本色は `TEXCOORD_0`、metallic-roughnessは `TEXCOORD_1` を使えます。未指定時はそれぞれ `TEXCOORD_0` が選ばれます。対応するUVがない場合や、未対応の成分形式・座標変換が指定された場合は読み込み時に診断します。

![係数から計算したmetallic-roughnessの参照モデル](images/model-material-reference.png)

参照画像はmetallic / roughness係数を設定したモデルです。GPU検査ではshader式をテスト側で再現せず、画像テクスチャを使うモデルと同じ係数の参照モデルを比較しました。

![metallic-roughness画像を使ったモデル](images/model-material-texture.png)

画像のG/B値と材質係数の組み合わせが、同じ値を係数で指定した参照モデルと一致することを確認しました。R/Aを変えた画像、係数を掛けた画像、基本色にも同じPNGを使う例、基本色画像なしの例、MASK材質でalphaが0のmetallic-roughness画像も含みます。

![基本色と異なるUVセットを使うmetallic-roughness画像](images/model-material-uv1.png)

この例では基本色を `TEXCOORD_0`、metallic-roughness画像を `TEXCOORD_1` から読みます。UV1の領域ごとに異なる金属度・粗さを参照画像と比較しました。

MR材質はScene/UIのモデル描画に使う内蔵lighting shaderで処理します。独自pixel shaderを選ぶ公開APIは変更していません。独自pixel shaderは既存の非照明描画経路でモデルを描くため、MR/PBRの材質情報は独自shaderへ渡らず、内蔵shaderのMR計算も行われません。MASKモデルに独自shaderを適用した場合の診断は[GLBの透明部分](#glb-の透明部分)を参照してください。

ReleaseのMR画像検査は2/2成功し、Scene 15枚、UI 8枚、連続再読込のframe 130・131の2枚を確認しました。連続再読込では同じGLBを132回新しく読み込み、各frameのPresent前にモデルを削除してから後半2枚を参照画像と比較します。基本色とMRの画像を各モデルごとに作るため、合計264個のtexture resourceを扱い、128 entryのcacheを越えて検査します。これはtexture cacheのentry evictionをまたぐ画像とモデルの寿命検査であり、GPU性能やtexture memory byte budgetの測定ではありません。個別設定と画像は[描画検証](render-validation.md)を参照してください。

Windows 11 Pro、RTX 4070 SUPER / driver 610.74、Visual Studio 2026 / v142、Windows SDK 10.0.22621.0でDebug・Releaseの全43テストが成功し、MRの25画像は構成間でbyte単位に一致しました。再検査は次で実行します。

```bat
ctest --test-dir build/runtime-windows -C Release -R gkcore.model_material --output-on-failure
ctest --test-dir build/runtime-windows-debug -C Debug -R gkcore.model_material --output-on-failure
```

## GLBのsampler設定

標準のモデル描画は、基本色・金属度/粗さ・法線の画像をそれぞれのsamplerで参照します。`wrapS` は横方向、`wrapT` は縦方向のUVが範囲外に出たときの扱いです。

| 指定 | 値 | 範囲外の座標 |
|---|---|---|
| REPEAT | 10497 | 同じ画像を繰り返す |
| MIRRORED_REPEAT | 33648 | 1区間ごとに向きを反転して繰り返す |
| CLAMP_TO_EDGE | 33071 | 画像の端の色で固定する |

samplerまたはwrap指定を省略したGLBは、両方向ともREPEATになります。たとえば `(-0.25, 1.75)` はREPEATでは `(0.75, 0.75)`、MIRRORED_REPEATでは `(0.25, 0.25)` を読みます。横と縦へ別々の設定も使えます。

`magFilter` は拡大、`minFilter` は縮小の補間方法です。NEAREST（9728）は近い画素をそのまま読み、LINEAR（9729）は隣接画素を混ぜます。未指定時はLINEARを使います。ミップマップ（縮小画像を段階的に用意したもの）を指定するminFilterでは、元画像から1×1までの縮小画像を生成します。ミップを指定しない9728/9729と省略値は、従来どおり元画像だけを使います。

同じ画像でも、samplerが違う材質は別々に描きます。画像のGPU資源は画像と色形式で共有し、samplerが違うためにPNGを複製することはありません。公開の独自pixel shaderを選んだモデル、FBX/OBJ、2D画像は従来の固定samplerを使います。独自shaderの入力形式は変更していません。

![REPEATで範囲外UVから参照した画像](images/model-sampler-repeat.png)

![MIRRORED_REPEATで同じUVから参照した画像](images/model-sampler-mirror.png)

![同じ画像を左右で異なるsamplerから参照した描画](images/model-sampler-mixed.png)

sampler検査はRelease/Debugでbyte一致した34画像を取得し、wrapの8設定と正負の整数端、指定省略時のREPEAT、同じ画像を異なる設定で描く左右の材質、基本色/MR/法線の個別sampler、拡大・縮小の補間を確認します。縮小は中心画素、拡大はモデル領域を参照と比較しています。132回の読み直しとPresent前削除を経た最後の2画像も参照と一致しました。詳しい結果は[描画検証](render-validation.md)を参照してください。

再検査は次で実行します。

```bat
ctest --test-dir build/runtime-windows -C Release -R gkcore.model_sampler --output-on-failure
ctest --test-dir build/runtime-windows-debug -C Debug -R gkcore.model_sampler --output-on-failure
```

## GLBのミップマップ

minFilterの指定に応じて、画素間の補間と縮小段の選択を別々に適用します。

| minFilter | 値 | 画素間 | 縮小段の間 |
|---|---|---|---|
| NEAREST_MIPMAP_NEAREST | 9984 | 近い画素 | 近い段 |
| LINEAR_MIPMAP_NEAREST | 9985 | 線形補間 | 近い段 |
| NEAREST_MIPMAP_LINEAR | 9986 | 近い画素 | 線形補間 |
| LINEAR_MIPMAP_LINEAR | 9987 | 線形補間 | 線形補間 |

基本色と自己発光画像はsRGBのRGBを線形の明るさへ直して平均し、sRGBへ戻して保存します。アルファ、金属度/粗さ、法線・遮蔽の画像は線形値のまま平均します。法線は画像を読んだ後で描画shaderが正規化します。段ごとの画像サイズは半分にし、奇数サイズは面積に応じた重みで平均して端の画素も含めます。1×N、N×1も1×1まで作ります。元の画像と最初の段は変更しません。

同じ画像と色形式でも、ミップを使う描画と使わない描画のGPU資源は分けます。同じミップ構成は共有し、1×1画像は指定によらず単一段を使います。基本色・自己発光のsRGBと、MR/法線/遮蔽の線形データは別形式です。現在の上限は一辺16384、元画像64M画素、cache128枠・縮小段を含む論理RGBA byte数256MiBです。GPU側の行整列やCPU生成中の一時領域をこのbyte上限へ含めたものではありません。

![元の画像だけを使った縮小描画](images/model-mip-none.png)

![段間を線形補間したミップの縮小描画](images/model-mip-linear.png)

![左がミップなし、右がミップありの同一画像](images/model-mip-mixed.png)

GPU検査ではRelease/Debugでbyte一致した28画像を取得し、4 filterの中心画素、基本色と線形用途を共有する画像、MR・法線、5×3/1×7の端画素、ミップ有無を同時に描くモデルの再読込を確認します。描画条件と結果は[描画検証](render-validation.md)を参照してください。

```bat
ctest --test-dir build/runtime-windows -C Release -R "gkcore.texture_mip_chain|gkcore.model_mip_capture" --output-on-failure
ctest --test-dir build/runtime-windows-debug -C Debug -R "gkcore.texture_mip_chain|gkcore.model_mip_capture" --output-on-failure
```

元のsampler定義は [glTF Samplers](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#_samplers) を参照してください。

## GLBの自己発光

`emissiveFactor` は線形RGBの係数です。各成分は0〜1、省略時は黒になります。`emissiveTexture` のRGBはsRGBから線形色へ直して係数を掛け、Aは使いません。画像がなければ白を掛けます。`KHR_materials_emissive_strength` の `emissiveStrength` は0以上の有限値で、省略時は1です。1を越える指定もHDR値として保持します。

```json
{
  "emissiveFactor": [1.0, 0.3, 0.1],
  "emissiveTexture": { "index": 0, "texCoord": 1 },
  "extensions": {
    "KHR_materials_emissive_strength": { "emissiveStrength": 4.0 }
  }
}
```

拡張を使うGLBではトップレベルの `extensionsUsed` に `KHR_materials_emissive_strength` を指定します。画像は埋め込みPNGを使い、独立したUV・sampler・ミップ指定を保持します。UV形式と未対応の座標変換は他の材質画像と共通です。同じimage・色形式・ミップ構成なら基本色画像とGPU資源を共有します。

自己発光を反射光へ加え、SceneのHDR描画から露出・トーンマップ・Bloomへ渡します。環境光と方向光が0でも発光色が表示されます。Sceneに描けばBloomで周囲へ光が広がり、UIに描けばポスト処理後に合成されます。周囲のモデルを照らす光源や、間接光を生成する機能ではありません。MASKで捨てた画素は発光しません。

内蔵モデルshaderの機能です。独自pixel shaderを指定すると既存の非照明経路を使い、自己発光入力は渡されません。発光係数・強度は材質の指定を使います。

![自己発光画像を使ったモデル](images/model-emissive-texture.png)

![HDRの自己発光をBloomで広げた描画](images/model-emissive-bloom.png)

実機で取得した45画像はRelease/Debug間でbyte単位に一致しました。照明0、HDR/Bloom、材質の設定差、MASK、Present前のhandle削除後の描画を検査しています。検査方法と結果は[描画検証](render-validation.md)を参照してください。

```bat
ctest --test-dir build/runtime-windows -C Release -R "gkcore.model_emissive|gkcore.model_geometry" --output-on-failure
ctest --test-dir build/runtime-windows-debug -C Debug -R "gkcore.model_emissive|gkcore.model_geometry" --output-on-failure
```

値と画像の定義は[glTF材質仕様](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#_material_emissivefactor)、強度は[KHR_materials_emissive_strength](https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_materials_emissive_strength/README.md)を参照してください。

## GLBの環境遮蔽画像

`occlusionTexture` は、くぼみなどで受ける環境光を弱めるための材質画像です。Rを線形の明るさとして読み、G・B・Aはこの効果に使いません。Rが1なら環境光を保ち、0なら環境光を受けません。

`strength` は効果の強さで、0〜1の有限値を使います。省略時は1、0なら画像による遮蔽を無効にします。環境光を残す割合は `1 + strength * (R - 1)` です。方向光と自己発光の明るさは変えません。画像がないモデルはこれまでの明るさを維持します。

```json
{
  "occlusionTexture": { "index": 0, "texCoord": 1, "strength": 0.5 }
}
```

独立した `texCoord`・sampler・ミップ指定に対応し、埋め込みPNGを使います。UV形式と未対応の座標変換は他の材質画像と共通です。MR画像と同じimage・ミップ構成なら線形のGPU画像を共有します。一般的なORM画像ではRを環境遮蔽、Gを粗さ、Bを金属度として同じimageを使えます。基本色・自己発光のsRGB用とはGPU画像を分けます。

内蔵モデルshaderでScene/UIの両方へ描けます。独自pixel shaderを指定すると既存の非照明経路を使い、環境遮蔽の入力は渡されません。モデルに用意した材質画像を使う機能で、画面や周囲の形状から遮蔽画像を自動生成するものではありません。

![R値による環境光の遮蔽](images/model-occlusion-r-values.png)

実GPUで取得した43画像はRelease/Debug間でbyte単位一致し、全画面の参照比較も一致しました。結果の詳細は[描画検証](render-validation.md#glbの環境遮蔽画像)を参照してください。

```bat
ctest --test-dir build/runtime-windows -C Release -R "gkcore.model_occlusion|gkcore.model_geometry" --output-on-failure
ctest --test-dir build/runtime-windows-debug -C Debug -R "gkcore.model_occlusion|gkcore.model_geometry" --output-on-failure
```

画像と強度の定義は[glTFのocclusionTexture仕様](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#_material_occlusiontextureinfo_strength)を参照してください。

## 同じ画像を使う材質

GLBの `textures[].source` が同じ `images` 項目を指す場合、別々のtexture indexでもPNGを1回だけ読み込んで共有します。共有範囲は1モデルの読み込み内です。別々に `LoadModel` したモデル間で共有するものではありません。基本色・自己発光はsRGB、MR・法線・遮蔽は線形値として扱い、必要な色形式だけをGPUへ転送します。

画像の共有はUVの選択とは別に扱います。たとえば基本色・MRはUV0、法線はUV1を選べます。選択先のUV欠損や未対応の座標変換は、画像を再利用できる場合も診断します。別の `images` 項目は、PNGの内容が同じでも別画像として保持します。同じ画像を違うsamplerで参照する場合も、画像は共有し、材質のsamplerは別に保持します。

同じ画像を3種類のtextureから参照するGLBを50回新しく読み込み、同じ位置に重ねて1frameへ描画してから全モデルのhandleをPresent前に削除するケースを検査しています。旧実装は重複したGPU画像が128 entryのcache上限へ達してPresentに失敗しましたが、画像共有後は描画でき、参照画像と全画素一致しました。描画資源の保持とGLB内の画像参照共有を検査するもので、GPU性能は測定していません。

![同じ位置へ重ねた50モデルの最終描画](images/model-image-sharing.png)

textureとimageの関係は [glTF Texture Data](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#textures) を参照してください。

## GLB の法線マップ

`normalTexture` のRGBを線形値として読み、接線空間の法線へ展開します。Aは使いません。`scale` は横・縦成分に掛ける有限値で、省略時は1、0や負の値にも対応します。基本色・metallic-roughness・法線画像はそれぞれ独立して `texCoord` を選べます。法線画像も埋め込みPNGを使い、UV形式と `KHR_texture_transform` の制約は基本色画像と共通です。

`NORMAL` を省略したGLBは三角形ごとの面法線を生成します。法線画像を使い、`TANGENT` もない場合は、画像が選ぶUVから接線を自動生成します。`TANGENT` は4成分で、XYZは法線に平行でない有効な方向、Wは+1か-1を指定し、同じ三角形の3頂点で揃えます。明示した接線がある場合はその値を使い、不正な値を自動生成で置き換えることはありません。属性欠損・不正値・逆変換できないnode行列は読み込み時に診断します。node変換と描画時のモデル変換では法線と接線を別々に変換し、負のscaleによる鏡映も保持します。

内蔵モデル照明で処理するため、SceneとUIの両方で使えます。独自pixel shaderを指定した場合は既存の非照明経路へ切り替わり、法線マップの照明入力は渡されません。基本色・MR・法線に同じimageを参照するtextureを指定すると、sRGB用と線形用の2つのGPU画像を使い、MRと法線は線形画像を共有します。texture indexが別でも、同じimageの読み込み結果をモデル内で共有します。

![法線マップを使った描画](images/model-normal-texture.png)

![頂点法線だけで作った参照描画](images/model-normal-reference.png)

単色の法線マップと対応する頂点法線を持つ参照モデルを比較します。テスト側は接線基底の幾何変換だけを使い、照明の計算式を再実装して期待色を作りません。

![独立したUV1で法線模様を読む描画](images/model-normal-uv1.png)

既存の法線マップGLBをCPUで検査し、Scene20枚、UI11枚、描画時の鏡映2枚、2種類のGLBをそれぞれ132回読み直した後の各2枚、同じframeへ50モデルを描く1枚を実GPUで確認しました。法線画像のalpha無視、scaleの0・2・負値、接線Wの符号、UV1、共有画像、nodeと描画時の鏡映を参照モデルと比較しています。Release/Debugの画像一致は[描画検証](render-validation.md)に記録しています。接線Wを意図的に無視したshaderは45,000画素の差（RGB最大18）で拒否できました。

再検査は次で実行します。実行結果と条件は[描画検証](render-validation.md)へ記録しています。

```bat
ctest --test-dir build/runtime-windows -C Release -R gkcore.model_normal --output-on-failure
ctest --test-dir build/runtime-windows-debug -C Debug -R gkcore.model_normal --output-on-failure
```

仕様の読み方は [glTF normalTexture](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#_material_normaltexture) を参照してください。glTF全体への対応を意味するものではありません。

### 法線を省略したGLB

`NORMAL` がない場合、三角形の頂点の並びから面法線を作ります。同じ平面の頂点は再利用し、鋭い辺では頂点を分けて各面の向きを保ちます。平均して滑らかにする処理は行いません。計算はnode変換前に行い、その後に法線へ逆転置変換を適用します。

法線未指定時は、入力にある `TANGENT` を無視します。法線画像を使う場合は、生成した法線とその画像のUVから接線を計算します。法線画像がない場合は接線を使わず、UVが退化していても、三角形の面積が有効なら面法線を作れます。明示した `NORMAL` の不正値を生成で置き換えることはありません。

基本色・MR・法線・自己発光・遮蔽の各UVを保持し、参照されない頂点は出力から除きます。面積0の三角形、計算不能な値、生成法線を変換できない特異なnode行列、出力上限超過では読み込みが失敗します。既存の明示法線モデルの読み込み・変換は維持します。

![面法線と接線を生成したGLB](images/model-generated-normal-flat.png)

![共有頂点を鋭い辺で分けたモデル](images/model-generated-normal-fold.png)

35取得画像と判定JSONはRelease/Debug間で完全一致しました。明示した面法線を持つ参照へ、平面、折れ面、UV1、非indexed mesh、node変換、未参照頂点、法線画像の有無を比較します。鋭い辺を平均法線で描く反例も区別します。[描画検証](render-validation.md#glbの面法線生成)に結果を記録しています。

```bat
ctest --test-dir build/runtime-windows -C Release -R "gkcore.model_generated_normal|gkcore.model_normal_generation" --output-on-failure
ctest --test-dir build/runtime-windows-debug -C Debug -R "gkcore.model_generated_normal|gkcore.model_normal_generation" --output-on-failure
```

### 接線を省略したGLB

接線は、法線画像の横方向と縦方向をモデル表面へ対応させるデータです。`TANGENT` がない場合、primitiveの位置・明示法線・`normalTexture.texCoord` が選ぶUVを使い、固定したMikkTSpaceの標準設定で生成します。node変換前に計算し、その後に法線と接線を別々に変換して、非一様scaleと鏡映の向きを保持します。

UVの鏡映や接線の向きが異なる三角形では、同じsource vertexでも必要な頂点分割を行います。同じframeは再利用し、頂点を参照するindexを付け直します。参照されない頂点は生成結果から除きます。基本色・MR・法線・自己発光・遮蔽の各UVや材質は維持します。

三角形やUVの面積が0で接線を定められない場合、参照法線が0または不正な場合、数値範囲を保って計算できない場合、出力上限を越える場合は読み込みが失敗します。任意の軸へ置き換える処理は行いません。`NORMAL` がない場合は先に面法線を生成し、入力にある `TANGENT` は使わずに接線を計算します。明示 `TANGENT` の読込みは従来のままです。

![自動生成した接線で法線マップを描いたモデル](images/model-tangent-generated.png)

![非一様node変換後の角度加重接線モデル](images/model-tangent-weighted.png)

取得した32画像と判定JSONはRelease/Debugで完全一致しました。検査は単純な平面、鏡映UV、UVの回転、UV1、非indexed mesh、共有辺、nodeと描画時の変換、未参照頂点を含みます。角度加重で作る接線は解析的な参照を用意し、非一様変換の前に生成する順序も比較します。詳細は[描画検証](render-validation.md#glbの接線自動生成)を参照してください。

```bat
ctest --test-dir build/runtime-windows -C Release -R "gkcore.model_tangent|gkcore.model_normal" --output-on-failure
ctest --test-dir build/runtime-windows-debug -C Debug -R "gkcore.model_tangent|gkcore.model_normal" --output-on-failure
```

生成方法の指定は[glTF geometry仕様](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#meshes)を参照してください。固定コードと配布noticeは[第三者コンポーネント](THIRD_PARTY_NOTICES.md#mikktspace)に記録しています。


## GLB の透明部分

glTFの `alphaMode` を省略した場合と `OPAQUE` では、画像のalphaと材質のalpha係数を無視して不透明に描きます。`MASK` では、画像のalphaに材質の基本色alpha係数を掛けた値を判定します。`alphaCutoff` を省略すると `0.5` です。判定値がcutoff未満の画素だけを描かず、cutoffと等しい画素は残します。cutoff `0` ではalpha値に関係なくすべて残り、`1`を超える有限値ではすべて抜けます。画像がない材質でも、材質alpha係数で同じ判定をします。切り抜きの判定にalphaを使いますが、残った画素のRGBをalphaで暗くしません。

SceneとUIの内蔵モデルshaderでMASKを処理します。抜いた画素はcolorだけでなくdepthも更新しないため、奥に描いた形状がその部分から見えます。次の画像はopaque描画、MASKによる切り抜き、背面モデルでdepthの状態を確かめたGPU画像です。

![OPAQUEでalpha値を無視したモデル](images/model-alpha-opaque.png)

![MASKで透明部分を切り抜いたモデル](images/model-alpha-mask.png)

![MASKで抜いた部分から背面が見えるdepth検査](images/model-alpha-depth.png)

GPU検査ではScene 11画像、UI 4画像、depth検査1画像を確認しました。有限なcutoff `2` と、負または非有限なcutoffの読み込み拒否も確認しています。`BLEND` は `LoadModel` 時に診断付きで拒否します。MASKモデルを描くときに `gk::SetPixelShader` で独自pixel shaderを選んでいると、alpha情報を独自shaderへ渡すABIがないため `Present` が診断付きで拒否されます。内蔵shaderへ戻すには `gk::SetPixelShader({})` を呼んでください。ここで説明した検査は標準の `OPAQUE` と `MASK` を対象としており、glTF JSON全体のschema検証を保証するものではありません。alpha modeの仕様は[glTF 2.0仕様のAlpha Coverage](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#alpha-coverage)を参照してください。

## ファイルの置き方

```text
assets/
├── scene.fbx       # LoadModel へ渡すモデル
└── textures/
    ├── wall.png     # 相対パスで参照する画像
```

FBX が外部画像を参照するときは、FBX ファイルの場所を基準にした相対パスで読み込みます。絶対パスは拒否されます。モデルと画像をまとめて移動できるよう、関連ファイルは同じ `assets` フォルダーに置いてください。

FBX モデルと相対パスの画像が読み込みから描画へ進む流れです。

```mermaid
flowchart LR
    M["assets/scene.fbx"] --> L["gk::LoadModel"]
    T["assets/textures/wall.png<br/>相対パスの画像"] --> L
    L --> H["ModelHandle"]
    H --> D["gk::DrawModel"]
```

## FBX の対応範囲

FBX は ASCII 形式とバイナリ形式の静的メッシュを読み込めます。外部画像はFBXファイルの場所を基準にした相対パスで参照します。絶対パスは拒否されます。外部PNGと埋め込みPNGの取り込みをCPUテストで確認しています。FBXのUVは先頭のUVセットを使い、V座標を反転して画像の左上原点に合わせます。FBXのBMP読み込みは未確認です。

FBXのモデルは右手系のY-up、メートル単位へそろえ、階層変換とメッシュの幾何変換を反映します。材質のRGB係数は線形値、画像のRGBはsRGBとして扱います。画像は端の色で固定し、繰り返し・鏡映やUV変換は反映しません。透明度係数はアルファ値に保持しますが、アルファ合成方式の切り替えは未対応です。

モデルの材質では基本色係数とGLBのmetallic / roughness係数を使います。OBJとFBXはmetallic `0`、roughness `1` で描画します。方向光と一様な環境光による材質照明を設定できます。使い方は[モデル照明ガイド](lighting.md)を参照してください。

影、環境マップ / IBL、`BLEND`、アニメーション、スキニング、モーフターゲット、レイヤー合成、手続き的に生成する画像は未対応です。対応状況は[機能一覧](ROADMAP.md)、GPU画像を含む検証結果は[描画検証](render-validation.md)を参照してください。
