# 最安・最速の内部テスト署名フロー

このリポジトリで**複数PCに素早く配ってテストする**ための、いちばん安くて早い実務向けフローです。

## 方針

- **自己署名のコード署名証明書**を 1 回作る
- その証明書で `exe` / `dll` を署名する
- **公開鍵だけ**を各テストPCへ入れる
- その後は署名済みビルドを配る

この方法は、**個人PCや研究室の自由なPCでの内部テスト**にはかなり速いです。

ただし、Windows 11 の Smart App Control は **署名済みでも「信頼された提供元」の証明書か、安全と予測されたコード」しか通さない**ため、自己署名では端末や設定次第でブロックされることがあります。さらに、組織管理の App Control / AppLocker はポリシーが許可しないコードを止めます。([Microsoft Learn](https://learn.microsoft.com/en-us/windows/security/application-security/application-control/app-control-for-business/appcontrol), [Microsoft Learn](https://learn.microsoft.com/en-us/windows/apps/develop/smart-app-control/code-signing-for-smart-app-control))

そのため、このフローは **「内部テストの最安・最速」** であって、**「どんな管理PCでも無条件に通る方法」ではありません**。

## 何が入っているか

- `tools/signing/Create-InternalTestCodeSigningCert.ps1`
  - テスト用の自己署名コード署名証明書を作る
- `tools/signing/Install-InternalTestCodeSigningCert.ps1`
  - 各テストPCへ公開証明書を入れる
- `tools/signing/Sign-HairSystemBuild.ps1`
  - ビルド出力の `exe` / `dll` を再帰的に署名する
- `tools/signing/Create-HairSystemTestBundle.ps1`
  - 署名済みビルドを配布用 ZIP にまとめる
- `build_scripts/*.cmd`
  - 上記 PowerShell を呼ぶ簡易ラッパー

## 最短手順

### 1. 開発PCで証明書を作る

PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\signing\Create-InternalTestCodeSigningCert.ps1
```

既定では次が出力されます。

- `tools/signing/output/hairgpu-internal-test-signing.cer`
- `tools/signing/output/hairgpu-internal-test-signing.pfx`（パスワード指定時のみ）
- `tools/signing/output/hairgpu-internal-test-signing.json`

Microsoft Learn の MSIX 証明書作成例でも、`New-SelfSignedCertificate` でコード署名用 EKU を持つ自己署名証明書を作る手順が案内されています。([Microsoft Learn](https://learn.microsoft.com/ja-jp/windows/msix/package/create-certificate-package-signing))

### 2. ビルドする

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

または Visual Studio で `hairgpu_dx11_viewer` をビルドします。

### 3. ビルド成果物へ署名する

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\signing\Sign-HairSystemBuild.ps1 -BuildRoot .\build
```

`signtool.exe` は Visual Studio / Windows SDK の場所を自動探索します。`signtool.exe` は Microsoft が案内する標準ツールです。([Microsoft Learn](https://learn.microsoft.com/en-us/windows/apps/develop/smart-app-control/code-signing-for-smart-app-control))

### 4. テストPCに公開証明書を入れる

テストPCで:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\signing\Install-InternalTestCodeSigningCert.ps1 -CertPath .\hairgpu-internal-test-signing.cer -Scope CurrentUser
```

管理者権限があるなら `-Scope LocalMachine` も使えます。

### 5. 配布 ZIP を作る

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\signing\Create-HairSystemTestBundle.ps1 -BuildOutputDir .\build\apps\dx11_viewer\Debug
```

これで `dist/` に配布用 ZIP が作られます。

## 研究・学内テスト向けのおすすめ

- まずは `CurrentUser` に証明書を入れる
- ZIP を配る前に `Unblock-File` でブロック属性を外す
- それでも止まるPCは、Smart App Control や組織ポリシーの影響を疑う
- **学校・会社PCでの本命は、署名済みアプリを許可ポリシーへ入れてもらうこと**

## それでもブロックされる場合

### Smart App Control の場合

Smart App Control は、**署名済みでも「信頼されたプロバイダー発行の証明書」でないと考慮しない**と Microsoft が案内しています。([Microsoft Learn](https://learn.microsoft.com/en-us/windows/apps/develop/smart-app-control/code-signing-for-smart-app-control))

つまり、**自己署名は「最安・最速」ではあるが、SAC 完全対応ではない**です。

### App Control / AppLocker の場合

管理ポリシーが有効な端末では、署名の有無だけでなく**ポリシーが信頼すると定義したものだけ**が実行されます。([Microsoft Learn](https://learn.microsoft.com/en-us/windows/security/application-security/application-control/app-control-for-business/appcontrol))

その場合は:

- 管理者に証明書またはアプリの許可を依頼する
- 研究室内 CA や公的なコード署名証明書へ切り替える
- Azure Trusted Signing などへ将来的に移行する

## 補足

Smart App Control をユーザー側でオフにすると、**Windows をリセット/再インストールしない限り再度オンにできない**と Microsoft は案内しています。テストのたびに保護機能を切る運用は避けたほうが安全です。([Microsoft Learn](https://learn.microsoft.com/en-us/windows/security/application-security/application-control/app-control-for-business/appcontrol))
