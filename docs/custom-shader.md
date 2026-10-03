# カスタムシェーダー開発

gkcore の公開ヘッダーには `gk::LoadPixelShader`、`gk::SetPixelShader`、`gk::SetShaderFloat4`、`gk::DeleteShader` があります。helper 側では typed handle、0..63 の `gk::Float4` slot、draw packet へ定数をコピーする契約をテストしています。ただし、The Forge renderer への user shader pipeline 接続はまだ実装されていません。これらの関数を呼んでも現時点ではカスタム shader の見た目を確認できません。

## 開発用コンパイル

The Forge の Direct3D 12 shader は HLSL ソースを Runtime で直接 compile しません。gkcore の固定 shader 一式は FSL script が DXC を呼び、Direct3D 12 用の `@FSL` artifact を生成します。開発者が同じ処理を再実行する command は次のとおりです。

```bat
python tools\build_gkcore_shaders.py --forge-root .devtools\The-Forge --dxc-root .devtools\dxc-1.8.2405 --output-dir build\gkcore_shaders
```

この command は固定済みの `shaders/` にある gkcore 内蔵 color / sprite shader を FSL/DXC で compile し、`.vert`、`.frag` と root signature 2 件を `build/gkcore_shaders/CompiledShaders/DIRECT3D12/` に出力します。PRE_SETUP は同じ tool を使います。Runtime SDK に Python、FSL script、DXC compiler を含めず、生成済み shader artifact だけを package に含めます。内蔵 shader があることは利用者が書いた shader を実行できることを意味しません。

この revision の FSL binary は `@FSL` header、metadata、hash/offset/size の derivative table、その後ろの DXIL stage container を含みます。DXIL stage は DXBC container wrapper (`DXBC` magic、part table、`DXIL` part) として保存されます。Python の構造検査には合成データを使います。別の C++ parser test は、固定 The Forge 1.8.2405 と DXC 1.8.2405 で生成した実際の color pixel shader artifact を読み、vertex shader artifact を pixel shader として拒否します。この artifact は Linux 上の DXC 実行ファイルへの一時 symlink を使って作成したもので、Windows の shader build、root signature、GPU pipeline は検証していません。

```sh
python3 tests/shader_contract_tests.py
python3 tests/shader_contract_tests.py --artifact build/gkcore_shaders/CompiledShaders/DIRECT3D12/gkcore_color.frag --dxc .devtools/dxc-1.8.2405/bin/x64/dxc.exe --require-reflection
```

2つ目の command は実 artifact が作られた後に使います。FSL artifact の構造と DXC reflection を確認し、gkcore が確定した shader ABI の入力/出力を照合します。現在の shader pipeline が完成しているという意味ではありません。

## 実装予定 ABI と適用範囲

現在検討中の primitive shader ABI は、CPU が clip-space position (`float4`) と color (`float4`) を渡し、Direct3D 12 VS/PS shader model 6.0 と DXIL を使う形です。primitive pixel shader は position/color 入力から `SV_Target0` を出力し、texture/descriptor binding を持ちません。sprite shader の source と内蔵 artifact は Runtime package に含めますが、描画 API からの texture binding はまだ接続されていません。2D image sampling、`SetShaderFloat4` の GPU binding、3D material shader、post-effect shader は個別の ABI を設計してから対応します。この内容は renderer と FSL shader でまだ統合していない提案です。

gkcore の公開 helper では shader handle ごとに定数を保持し、draw call を queue した時点の shader/constant state を packet へ複写します。helper の単体テストでこの CPU 側契約を検証しています。DXC reflection parser は shader input/output 契約の検査器です。GPU pipeline 作成や custom shader の描画成功は検証していません。

エラー表示では source path、stage、DXC diagnostics の行と列を利用者へ返す予定です。FSL shader source、実際の compile command、UI/2D 用 texture shader、post-process sample は ABI が固定され、Windows GPU 上でテストされてから追加します。
