# Extra Options

Extra Options is a standalone mod for Mystical Ninja Starring Goemon: Recompiled. It exposes optional gameplay challenges in the normal mod configuration menu.

## Options

| Option | Default | Behavior |
| --- | --- | --- |
| No Hit | Disabled | Any nonlethal damage immediately reduces the active character's HP to zero. |
| 1 Life | Disabled | Keeps both the runtime and saved life count at one while the player is alive. |
| Loose Ryo on hit | Disabled | Each damaging hit removes 50 Ryo and plays the native floating `-50 Ryo` robbery effect. |
| Hyper Enemies | Disabled | Gives supported enemies, enemy obstacles, and boss controllers 4x movement and attack cadence (2.5x average for Dharumanyo and Tsurami) while retaining their native physics and collision processing. |
| Enemy Multiplier | 1x (Normal) | Spawns two or three copies of supported enemy actors. This option is experimental and may crash the game. |

Multiplayer-only damage and Ryo synchronization are not included because they are network packet features rather than standalone gameplay rules.

Kashiwagi's Hyper implementation is isolated in `src/hyper_kashiwagi.c`. It
accelerates his combat movement, animation, attack timers, travelling shots,
and the moving copy summoned by his low-health attack
to 4x in Impact battles, including title-menu boss rush. Intro/outro sequences,
incoming-hit reactions, player controls, and damage processing retain native
timing. The ordinary-enemy spawn-rate settings and other bosses' speed settings
are unchanged.

## Building

The build requires Bash, Make, a MIPS-capable LLVM Clang, LLD, and `RecompModTool`. Apple Clang cannot target MIPS; on macOS the script automatically selects Homebrew LLVM and LLD.

Run:

```sh
./build_mod.sh -j4
```

The release package is written to `build/mnsg_extra_options.nrm`. There is no debug package for this mod.

`build_mod.sh` looks for `RecompModTool` in this repository, in the sibling `mnsg-recomp-example` repository, or on `PATH`. You can also provide an explicit path:

```sh
RECOMP_MOD_TOOL=/path/to/RecompModTool ./build_mod.sh -j4
```
