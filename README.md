# Stardew-Style HUD for Harvest Moon 64: Recompiled

Adds a Stardew Valley-style HUD to [Harvest Moon 64: Recompiled](https://github.com/HarvestMoon64Recomp/HarvestMoon64Recomp).

Vanilla HM64 has no gameplay HUD; the clock and gold only appear inside the pause menu. This mod draws
an always-on overlay with RecompUI instead:

- **Top-right:** clock panel with day and date (`Mon. 12`), a weather dot and season, and the time in half-hour steps (`9:30 am`), with a gold counter underneath.
- **Bottom-right:** energy bar (**E**, stamina) that shifts green → yellow → red and grows taller as max stamina rises from power berries, and a health bar (**H**).
  - **E:** tools stop working once it drops below the tool's stamina cost.
  - **H** is `100 − fatigue`, HM64's hidden overwork meter. It only drains when you use tools at night (outside 6 AM–6 PM) or in rain or snow, and recovers with sleep (more if you go to bed early) and some food. It's green above 50, turns yellow at 50 and red at 25 (the game's two "fatigued" warnings), and pulses while red. If it hits 0 you get a sick day the next morning.
- Hidden in menus, cutscenes, the title screen, and before a save is loaded; fades in and out.

Settings in the mod menu: show/hide the HUD, show/hide the health bar, and HUD scale.

## How it works

`src/hud.c` hooks `gfxRetraceCallback` (runs once per frame), reads game globals from the
[decompilation](https://github.com/harvestwhisperer/hm64-decomp) headers (`gHour`, `gGold`,
`gPlayer.currentStamina`, and so on), and updates a RecompUI context that doesn't capture input. Text and
layout are only updated when a value changes.

Visibility is decided by `mainLoopCallbackCurrentIndex` (the game's state machine; `MAIN_GAME` is free
roam) and the `CUTSCENE_ACTIVE` bit of `gCutsceneFlags`.

## Building

Requires LLVM/Clang **18.1.8** (19.x doesn't work for MIPS here), GNU make, and `RecompModTool` from the
[N64Recomp releases](https://github.com/N64Recomp/N64Recomp/releases). From the parent folder:

```powershell
.\build.ps1 -ModDir .\HM64StardewHUD
```

Or manually: `make`, then `RecompModTool mod.toml build`. The output is `build/hm64_stardew_hud.nrm`;
drag it onto the game window to install.

## Roadmap

1. ~~Clock/date/weather/gold panel and energy bar~~ (this version)
2. Toolbar along the bottom showing the equipped tool and rucksack slots (`gPlayer.currentTool`, `toolSlots`, `heldItem`); needs an icon strategy
3. Optional: switch the equipped tool from the toolbar without opening the pause menu
