# FH2 Drift Assist 0.6.0

Forza Horizon 2 on Windows x64 through Azimuth. This bundle combines the playtested CarX-inspired steering controller, both drift suspensions, and a longer cockpit steering animation fitted to each compatible car.

- Parking and low-speed stick inputs build progressively. At drift speed you can still countersteer through centre and unwind as the car straightens. The package includes the same 0.5.2-arcade1 steering component used for the driving tests.
- Fit Formula Drift suspension for up to 60° of inside front-wheel lock or SLR DRIFT 74° ANGLE KITS for up to 74°. Both upgrades are registered for compatible add-on cars when FH2 Mod Platform rebuilds its database.
- The driver’s hands follow the applied front-wheel angle. Setup fits a local FH6 steering motion to the car’s original grip. A round wheel travels approximately 540° from lock to lock. Recognized yokes use a shorter 280° sweep.
- Start **Play FH2 (Drift Assist).cmd** from your Azimuth folder after adding a car. Each launch checks for changed or new vehicles before opening Azimuth. A compatible default driver clip and steering rig are required. See `DriftAssistAnimation/FH2/last-scan.json` for cars the scanner skipped.

## Install or upgrade

1. Set up FH2 Xbox One build **1.0.0.64**, **Azimuth**, **FH2 Mod Platform**, and **Extra Options** with `EnableMediaOverrides=1`. For keyboard driving, install **FH2 Mouse & Keyboard**. Start FH2 once, then close the game and Azimuth.
2. For cockpit animation, have an installed copy of **FH6**, **64-bit Python with NumPy**, `forzatech_acl.dll` from Forza Mod Tool, and `granny2_x64.dll` and `xcompress64.dll` from ForzaTechStudio ready. [Forza Mod Tool v1.3.0.7](https://github.com/HTTPJXRDN/Forza-Mod-Tool/releases/tag/ForzaModToolV1.3.0.7) · [ForzaTechStudio Native](https://github.com/D3FEKT/ForzaTechStudio/tree/Main/ForzaTechStudio/Native)
3. Download and fully extract **FH2-Drift-Assist-0.6.0-win-x64.zip**. Run **Install.exe**, select Azimuth and the FH2 Mount, then select the requested local animation files. Existing steering settings are preserved. Use the new launch file, select a car and equip either kit under **Platform and Handling → Springs and Dampers**.

[English installation guide](https://github.com/VeeRoar/FH2-Drift-Assist/blob/v0.6.0/README.md) · [繁體中文安裝指南](https://github.com/VeeRoar/FH2-Drift-Assist/blob/v0.6.0/README.zh-Hant.md)

## 繁體中文

0.6.0 整合包收錄低速漸進的漂移輔助、兩款漂移懸吊和依原車握點調整的 FH6 座艙動畫。漂移手感的靈感來自 CarX Drift Racing Online 2。依製作者試駕，搭配漂移懸吊後，在大角度持續補油、反打及接住車尾時，手感已相當接近 CarX。

完全退出遊戲與 Azimuth，解壓縮整個 ZIP 後執行 **Install.exe**，選取 Azimuth、FH2 Mount 及上述本機動畫來源。既有設定會保留。此後使用 Azimuth 資料夾內的 **Play FH2 (Drift Assist).cmd** 啟動，會在進入遊戲前掃描新增或更換的相容車包。一般圓形方向盤左右各約 270°，60° 與 74° 則分別是兩款懸吊的內側前輪轉角上限。
