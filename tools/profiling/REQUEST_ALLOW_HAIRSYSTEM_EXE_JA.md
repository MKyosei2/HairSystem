# 管理者向け許可依頼文

以下のローカル開発用実行ファイルが、Windowsのアプリケーション制御ポリシーにより実行前にブロックされています。

対象:

- `C:\Users\kyose\source\repos\HairSystem\bin\Debug\HairSystem\HairSystem.exe`
- `C:\Users\kyose\source\repos\HairSystem\bin\Release\HairSystem\HairSystem.exe`

目的:

- 卒業研究用のGPU髪シミュレーション実験
- 60FPS達成のためのStep0プロファイリング
- `00_current`, `01_no_readback`, `02_no_self_collision`, `03_low_solver`, `04_no_debug_draw`, `05_render_light`, `06_simulation_light` の比較測定

必要な許可:

- 上記の `HairSystem.exe` をローカルで起動できるようにする
- 可能であれば `C:\Users\kyose\source\repos\HairSystem\bin\` 以下の開発ビルド出力を許可する

補足:

- 実行できない場合、GPUフレーム時間、self collision、solver iteration、readback、renderの実測ができません。
- Step0はEXEが起動して初めて `RESULT_REAL.md` を生成します。
