# Stardew-Style HUD

A mod for [Harvest Moon 64: Recompiled](https://github.com/HarvestMoon64Recomp/HarvestMoon64Recomp) that adds an
always-on HUD in the style of Stardew Valley, plus a toolbar that lets you switch tools and items without opening the
pause menu.

Vanilla HM64 has no gameplay HUD: the clock, date, and gold only appear inside the pause menu, and stamina and fatigue
are hidden entirely. This mod puts all of it on screen.

## Features

### Clock panel (top-right)
- Day of the week and date (`Sat. 6`), season, and a weather indicator.
- The time, in half-hour steps (`9:30 am`).
- Your gold, just below.

### Energy and health bars (bottom-right)
- **E (energy)** is your stamina. It shifts from green to yellow to red as it drains, and the bar grows taller as your
  maximum stamina rises from Power Berries. Tools stop working once it drops below a tool's stamina cost.
- **H (health)** shows HM64's hidden fatigue meter, as `100 − fatigue`. It only drains when you use tools at night
  (outside 6 AM–6 PM) or in rain or snow, and recovers when you sleep (more if you go to bed early) and when you eat
  certain foods. It turns yellow at 50 and red at 25 (the game's two "fatigued" warnings), pulses while red, and if
  it empties you'll wake up sick the next morning.

### Toolbar (bottom-center)
- **Tools row:** your equipped tool, then the tools in your rucksack.
- **Items row:** what's in your hands, then your belongings.
- The first slot in each row is your hand. The rest are packed left, in the order the D-pad cycles through them.
- Seed bags and chicken feed show how many uses are left; the watering can shows how much water is left.
- Icons are read from your own ROM while the game runs, so the mod contains no game art.

### Quick switching (D-pad)
- **Up / Down:** choose which row you're steering. The focused row is brighter, with a gold tab.
- **Left / Right:** rotate that row through your hand. The next tool gets equipped, or the next item comes out of
  your rucksack into your hands, and the old one goes back in line. The items row also stops on empty hands, which
  puts the held item away.
- Only works while you're free to act: not mid-swing, in a menu, conversation, or cutscene, or on horseback. Items
  that can't go in the rucksack (animals, the baby, and so on) can't be swapped out of your hands.
- With the default keyboard controls, the D-pad is **I / J / K / L**.

### General
- Styled to sit alongside the N64 art: muted parchment-and-wood colors, and icons softened to match the game's
  filtering.
- Hidden on the title screen, in menus (which have their own clock), and during cutscenes, and fades in and out on
  scene changes.
- Anchored to the window edges, so it works at any resolution and aspect ratio, including widescreen.

## Installation

1. Download `hm64_stardew_hud.nrm` (from Thunderstore, use **Manual Download**).
2. Drag it onto the game window before starting the game, or use **Install Mods** in the mod menu.
3. Make sure **Stardew-Style HUD** is enabled in the mod menu.

## Configuration

Open the mod menu, select **Stardew-Style HUD**, and choose **Configure**:

| Option | Default | Description |
|---|---|---|
| Show HUD | On | Show or hide the whole HUD. |
| Show Toolbar | On | Show or hide the toolbar. Also turns D-pad switching off. |
| Show Health Bar | On | Show or hide the H bar. |
| HUD Scale | 1.00 | Make the whole HUD bigger or smaller (0.50–2.00). |

## Building from source

Requirements:
- LLVM/Clang **18.1.8** and GNU make. LLVM 19.x doesn't support MIPS correctly for this workflow.
- `RecompModTool` from the [N64Recomp releases](https://github.com/N64Recomp/N64Recomp/releases), placed in the repo
  root (`*.exe` is git-ignored).
- On Windows, PowerShell must be on your `PATH`, because RecompModTool runs `powershell` to zip the mod.

```sh
git clone --recurse-submodules https://github.com/grant-lemoine/HM64StardewHUD.git
cd HM64StardewHUD
make
./RecompModTool mod.toml build
```

The output is `build/hm64_stardew_hud.nrm`.

## How it works

| File | Role |
|---|---|
| `src/hud.c` | Hooks `gfxRetraceCallback` (once per frame), reads game state, and draws the clock panel and bars with RecompUI. Text and layout only update when a value changes. |
| `src/toolbar.c` | Draws the toolbar, and hooks `handlePlayerInput` to read the D-pad. |
| `src/icons.c` | Decodes tool and item icons from the ROM. |

Game state comes straight from globals named in the [HM64 decompilation](https://github.com/harvestwhisperer/hm64-decomp):
`gHour`, `gGold`, `gPlayer.currentStamina`, `gPlayer.fatigueCounter`, the seed quantities, `wateringCanUses`, and so on.

**Visibility.** In free roam, the HUD uses the same check as the game's main loop before it updates the player: the
player entity is attached to the map and not paused. Cutscenes that take control of the farmer detach it, so the HUD
hides for them. It doesn't rely on the `CUTSCENE_ACTIVE` flag, because some areas run background scripts that set it
while you're free to walk around. During conversations, the HUD stays up unless a cutscene is running.

**Switching.** The game only calls `handlePlayerInput` while the farmer is idle and in control, so switching can't
interrupt an action. Taking an item out of the rucksack does what the pause menu does when it closes: it clears the
old held object, sets `gPlayer.heldItem`, and calls `initializePlayerHeldItem()`.

**Icons.** `icons.c` reads the pause menu's tool and item sprite sheets from the ROM with the game's `nuPiReadRom`,
follows the game's own animation tables to each icon, converts the CI4/CI8 image and RGBA5551 palette to RGBA,
applies a light desaturate and blur to match the N64 look, and caches the result.

## Credits

- Built with the [HM64 Recomp mod template](https://github.com/HarvestMoon64Recomp/HM64RecompModTemplate) and
  [N64Recomp](https://github.com/N64Recomp/N64Recomp).
- Game knowledge comes from the [Harvest Moon 64 decompilation](https://github.com/harvestwhisperer/hm64-decomp).
- [Stats Display](https://github.com/SrBananaMan/HarvestMoon64StatsDisplayMod) by SrBananaMan was the reference for
  drawing an in-game RecompUI overlay.

## License

GPL-3.0, inherited from the mod template. See [LICENSE](LICENSE).
