# FH2 Drift Assist, Drift Suspension Kits & Cockpit Steering

[繁體中文](README.zh-Hant.md)

An integrated drift mod for **Forza Horizon 2 on Azimuth**. One installer adds automatic countersteer, quick steering for controllers and keyboards, and two garage upgrades. **Equip one of the supplied suspensions** to get the steering range the assist was tuned around.

The inspiration is **CarX Drift Racing Online 2**, particularly the way its arcade steering lets you catch a slide and unwind the wheel. In the creator's FH2 playtesting, the combination of assist and drift suspension felt **very close to CarX**. The comparison describes the driving feel. The mod runs its own steering controller against FH2 vehicle physics.

## How it drives

- **Catch a large slide.** The assist estimates the front wheels’ direction of travel using the car's forward speed, lateral speed, yaw and front-axle geometry. As the angle closes it starts to unwind the countersteer. The aim is to let you stay on the throttle and catch the car without the assist itself throwing it into an opposite slide.
- **Make small corrections at parking speed.** The new low-speed response gives a partial stick input time to settle. At 60 Hz with the default setup and the 74° kit, a quarter-stick input reaches half of the requested angle in about 0.15 seconds. Full stick at a standstill reaches 90% of its target in about 0.53 seconds in the steering-controller test. During a slide, the quicker response lets you steer through centre to catch the car.
- **See the countersteer from the cockpit.** The driver's hands and the steering wheel follow the steering applied to the front wheels. The installer fits the longer FH6 movement to each compatible car's original grip, including cars where both hands rest above the horizontal centreline. The standard round-wheel motion turns approximately 270° each way (540° lock to lock). Supported yokes use approximately 140° each way. The 60° and 74° suspension figures refer to the inside front tyre.
- **Keep control over crests.** When the car loses wheel contact or tips too far for a reliable ground estimate, the assist carries forward its last supported steering range. You can still steer in the air. This reduces the abrupt centring and front-wheel twitch that can otherwise show up over bumps or when landing.
- **Switch cars or input devices.** The assist follows the player's current car. It accepts analogue stick input and the combined driving axis supplied by FH2 Mouse & Keyboard. Full steering-key input can still request the full available range with a stick deadzone enabled.
- **Watch the rear move.** An optional drift chase camera moves the pivot forward in both near and far chase views, making it easier to read the rear of the car as it slides.

## Choose your suspension

Open the garage upgrade shop, then **Platform and Handling → Springs and Dampers**. Fit one kit per car.

| Garage upgrade | Inside front-wheel lock | What changes |
|---|---:|---|
| Formula Drift suspension | Up to 60° | Adjustable springs and dampers scaled to front and rear axle load. A good starting point for learning where the car catches. |
| SLR DRIFT 74° ANGLE KITS | Up to 74° | More steering travel for deep slides, with its own spring, damper and alignment profile. |

The listed angle is the inside wheel's maximum. The outside wheel turns through a different angle according to the car’s geometry. At speed, the assist also uses the front slip window to determine how much of that range to request.

The mod creates missing kit entries when FH2 Mod Platform builds its working database. It uses each car's own suspension and front/rear physics records, so compatible add-on cars can receive both kits on their next database build. A vehicle pack that supplies its own kit for the same upgrade level retains it. Local database tests covered **355 stock vehicle records**, a **365-vehicle modded database**, and six load orders for a simulated new vehicle. A new car needs valid native suspension records for its kits to be generated.

## Requirements

| Component | Tested setup |
|---|---|
| Game and launcher | Your own Xbox One copy of Forza Horizon 2, build **1.0.0.64**, running on Windows x64 with Azimuth **1.0.4**. [Azimuth](https://almost-there-games.itch.io/azimuth) |
| Mod loader | **FH2 Mod Platform 2.9.0** (API 10), for plugin loading, database updates and localized menu text. [Official release](https://github.com/Trinculo54/fh2-mod-platform/releases/tag/v2.9.0) |
| Garage localization | The tested setup uses **Extra Options 0.4.4 for Azimuth** with `EnableMediaOverrides=1` so the game reads the merged garage labels. [Official releases](https://github.com/RokkuDayo/ForzaHorizon2_ExtraOptions/releases) |
| Keyboard driving | Install **FH2 Mouse & Keyboard 1.3.0** for its driving keys. A controller can use the assist directly. [Official release](https://github.com/qiangqiang101/FH2MouseKeyboard/releases/tag/1.3.0) |
| Cockpit animation sources | An installed copy of **Forza Horizon 6** with its original `Content/media/animation.zip` and `Content/media/cars` folder. Setup reads the FH6 motion and fits it to the original clips in your FH2 installation. |
| Local animation tools | **64-bit Python 3.10 or later** with **NumPy** (tested with Python 3.12), **Forza Mod Tool**'s `forzatech_acl.dll`, and **ForzaTechStudio**'s `granny2_x64.dll` and `xcompress64.dll`. For Python 3.12, install NumPy with `py -3.12 -m pip install numpy`. [Forza Mod Tool v1.3.0.7](https://github.com/HTTPJXRDN/Forza-Mod-Tool/releases/tag/ForzaModToolV1.3.0.7) · [ForzaTechStudio Native](https://github.com/D3FEKT/ForzaTechStudio/tree/Main/ForzaTechStudio/Native) |

## Install and drive

1. Set up the dependencies above and launch FH2 once. Close the game and Azimuth.
2. Extract the entire **FH2-Drift-Assist-0.6.0-win-x64.zip** and run **Install.exe**. Select the folder containing `azimuth.exe`, then the FH2 **Mount** folder containing `AppxManifest.xml` and `media`.
3. The installer installs the assist, both garage kits and their labels. The animation setup asks for your FH6 `animation.zip` and the three DLLs in the requirements table. If it cannot find Python, select your 64-bit `python.exe` with NumPy installed. Allow the first scan to finish. Keep the selected tools in their original locations.
4. From your Azimuth folder, start **Play FH2 (Drift Assist).cmd**. This launcher checks the active car folders each time you run it and fits the animation to newly added compatible cars before opening Azimuth. You can create a desktop shortcut to this file. In the garage, fit one suspension and adjust the steering under **Mods → FH2 Drift Assist**.

**Upgrading.** Select the same folders and keep the complete extracted package together. Existing `FH2DriftAssist.ini` settings are preserved. The 0.6.0 bundle ships the in-game tested **0.5.2-arcade1** steering DLL. The Mods menu shows that component version. Its speed-dependent response is identical to the playtested build.

**New cars.** The scan covers car archives and loose default driver clips in `media`, enabled Extra Options car packs, `mediapc` and `mediaoverride`. Compatible cars need the original `scene/Drivers/default/leftRightTurn.gr2` with the steering-wheel track and the matching driver bones. Cars without that clip keep their existing behaviour. The scan records unsupported rigs, corrupt archives and externally edited overrides in `DriftAssistAnimation/FH2/last-scan.json` in your Azimuth folder. Starting `azimuth.exe` directly skips this scan. Suspension options come from the mod platform's database rebuild.

## In-game controls

| Setting | Default | Range and effect |
|---|---:|---|
| Drift assist | On | Enables quick steering and automatic countersteer. Switch off for normal steering. |
| Countersteer strength | 150% | 0–150%, in steps of 5%. Higher values increase automatic countersteer. At 0% the game uses normal steering. |
| Steering input speed | 100% | 50–150%, in steps of 5%. Controls front-wheel response when you steer or reverse your input. |
| Steering release speed | 100% | 50–150%, in steps of 5%. Lower values give a gentler unwind when you release the stick or keys. |
| Steering deadzone | 4% | 0–15%, in steps of 1%. Raise this if a resting stick steers slightly. |
| Front slip window | 12° | 4–24°, in steps of 1°. A smaller window keeps the front wheels closer to their travel direction at speed. A larger window gives the driver more room. |
| Cockpit steering sync | On | Drives the cockpit animation from the applied front-wheel angle. Off returns animation control to the normal game input. Installed animation clips stay fitted. |
| Drift chase camera | On | Moves the chase-camera pivot forward. Can be switched independently. |

**To drive with the kits only**, switch off **Drift assist** under **Mods → FH2 Drift Assist**. The loaded plugin continues to register the garage upgrades.

**Removal.** Refit a standard suspension on cars you use and close Azimuth. From a command prompt in the extracted package, run `Animation\AnimationSetup.exe -Game FH2 -Azimuth "YOUR AZIMUTH FOLDER" -Mount "YOUR FH2 MOUNT" -Uninstall` to restore the previous loose animations and remove the launch shortcut. The scanner retains independently edited files and identifies conflicts. Restore files marked `REPLACED` from the timestamped backup in `Azimuth/ModBackups/FH2-Drift-Assist-*/`. Remove the listed `NEW` files that still belong to this installation. If another mod has since edited a garage table, keep the newer file and merge those changes. Restart so the platform can rebuild its working database.

**Troubleshooting.** Check `fh2-mods/platform.log` for FH2 Drift Assist. To check cockpit sync, use the cockpit view, enable both **Drift assist** and **Cockpit steering sync**, and check that **Countersteer strength** is above 0%. If the kits appear with missing names, check the selected FH2 Mount folder and Extra Options' media overrides. If only the keyboard fails, confirm FH2 Mouse & Keyboard is loaded and verify its steering bindings.

## Source and credits

The release includes the corresponding source under `source/`. To build on Windows x64, install **Zig 0.15.2** and use Windows .NET Framework 4.x's C# compiler. Run `.\build.ps1` from a source checkout to build the plugin and installer in `dist/`. The installer embeds the plugin, garage labels and default preset. The upstream dependencies are installed separately.

The steering design also drew on Adam D.'s Assetto Corsa Advanced Gamepad Assist. License and attribution details are included in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
