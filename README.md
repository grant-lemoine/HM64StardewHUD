# Stardew-Style HUD for Harvest Moon 64: Recompiled

Adds a Stardew Valley-style HUD to [Harvest Moon 64: Recompiled](https://github.com/HarvestMoon64Recomp/HarvestMoon64Recomp).

Vanilla HM64 has no gameplay HUD; the clock and gold only appear inside the pause menu. This mod draws
an always-on overlay with RecompUI instead:

- **Top-right:** clock panel with day and date (`Mon. 12`), a weather dot and season, and the time in half-hour steps (`9:30 am`), with a gold counter underneath.
- **Bottom-right:** energy bar (**E**, stamina) that shifts green → yellow → red and grows taller as max stamina rises from power berries, and a health bar (**H**).
  - **E:** tools stop working once it drops below the tool's stamina cost.
  - **H** is `100 − fatigue`, HM64's hidden overwork meter. It only drains when you use tools at night (outside 6 AM–6 PM) or in rain or snow, and recovers with sleep (more if you go to bed early) and some food. It's green above 50, turns yellow at 50 and red at 25 (the game's two "fatigued" warnings), and pulses while red. If it hits 0 you get a sick day the next morning.
- **Bottom-center toolbar:** a tools row (equipped tool, then your 8 rucksack tools) above an items row
  (what's in your hands, then your 8 belongings). The icons are decoded from your own ROM at runtime
  (see `src/icons.c`), so no game art ships with the mod.
- **Switch without the pause menu (D-pad):**
  - Up/Down: choose which row you're steering (the focused row is bright with a gold tab).
  - Left/Right: rotate that row through your hand. The next tool gets equipped, or the next item comes out
    of your rucksack into your hands, and the old one goes back in line. The items row also stops on
    empty hands, which puts the held item away.
  - Only works while you're free to act (not mid-swing, in a menu, conversation, or cutscene, or on
    horseback). Items that can't go in the rucksack (animals, the baby, etc.) can't be swapped out.
- Hidden in menus, cutscenes, the title screen, and before a save is loaded; fades in and out.

Settings in the mod menu: show/hide the HUD, show/hide the toolbar, show/hide the health bar, and HUD scale.

## How it works

`src/hud.c` hooks `gfxRetraceCallback` (runs once per frame), reads game globals from the
[decompilation](https://github.com/harvestwhisperer/hm64-decomp) headers (`gHour`, `gGold`,
`gPlayer.currentStamina`, and so on), and updates a RecompUI context that doesn't capture input. Text and
layout are only updated when a value changes.

Visibility is decided by `mainLoopCallbackCurrentIndex` (the game's state machine; `MAIN_GAME` is free
roam) and the `CUTSCENE_ACTIVE` bit of `gCutsceneFlags`.

`src/toolbar.c` hooks `handlePlayerInput`, which the game only calls while the farmer is idle, and reads
the D-pad there (the game doesn't use it in free roam). Swapping an item into your hands mirrors what the
pause menu does on close: clear the old held object, set `gPlayer.heldItem`, and call
`initializePlayerHeldItem()`.

`src/icons.c` reads the pause menu's tools and holdable-items sprite sheets from the ROM with the game's
`nuPiReadRom`, follows the game's own animation tables to each icon's frame, and converts the CI4/CI8
image and RGBA5551 palette to an RGBA texture, cached after first use.

## Building

Clone with submodules (`git clone --recurse-submodules`), then install:

* LLVM/Clang **18.1.8** and GNU make. LLVM 19.x doesn't support MIPS correctly for this workflow.
* `RecompModTool` from the [N64Recomp releases](https://github.com/N64Recomp/N64Recomp/releases). Put it in
  the repo root; `*.exe` is git-ignored.

Then:

```sh
make
./RecompModTool mod.toml build
```

The output is `build/hm64_stardew_hud.nrm`; drag it onto the game window to install. On Windows,
RecompModTool zips the mod by running `powershell`, so PowerShell must be on your `PATH`.

## Roadmap

1. ~~Clock/date/weather/gold panel, energy and health bars~~
2. ~~Toolbar with ROM-decoded icons and D-pad switching for tools and items~~ (this version)
