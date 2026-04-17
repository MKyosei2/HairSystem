# One-project Visual Studio layout

この版では、Visual Studio のソリューションを **1 プロジェクト構成** に統合しています。

## 変更点
- `HairSystem.sln` は **HairSystem** 1 プロジェクトのみを読み込みます。
- `HairSystem.vcxproj` は、次を 1 つのアプリケーションとしてまとめてビルドします。
  - `apps/dx11_viewer/src/*`
  - `src/hairgpu_core/*`
- 旧 `hairgpu_core` / `hairgpu_cli` / `hairgpu_dx11_viewer` の個別 `.vcxproj` は削除しました。
- `apps/hairgpu_cli/src/main.cpp` は参照用として残し、ビルド対象からは外しています。

## 使い方
1. `HairSystem.sln` を開く
2. 構成を `Debug|x64` または `Release|x64` にする
3. そのまま `HairSystem` をビルド / 実行する

## 意図
- スタートアッププロジェクトを毎回切り替えなくてよい
- ソリューションエクスプローラー上で、**実行本体が 1 つ** だけ見える
- コア実装と DX11 viewer を同じプロジェクト内で追いやすい
