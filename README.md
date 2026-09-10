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
| Game Over to Title | Disabled | Skips the game over continue/goodbye menu and returns to the title screen as soon as the player runs out of lives. |

Multiplayer-only damage and Ryo synchronization are not included because they are network packet features rather than standalone gameplay rules.

Kashiwagi's Hyper implementation is isolated in `src/hyper_kashiwagi.c`. It
accelerates his combat movement, animation, attack timers, travelling shots,
and the moving copy summoned by his low-health attack
to 4x in Impact battles, including title-menu boss rush. Intro/outro sequences,
incoming-hit reactions, player controls, and damage processing retain native
timing. The ordinary-enemy spawn-rate settings and other bosses' speed settings
are unchanged.

Taisamba 2 (`src/hyper_taisamba.c`) and Balberra (`src/hyper_balberra.c`)
also use dedicated 4x Impact implementations. Taisamba's ground/air attacks,
projectiles, returning weapon, and arena ascent share the accelerated pace.
Balberra's movement, opening pods, deck weapons, drones, charge-up attacks,
and travelling shots are accelerated, including his one-shot rocket-volley
cue. Intro/outro sequences, incoming-hit reactions, and shared player input
retain native timing.

D'Etoile (`src/hyper_detoile.c`) also runs at 4x, both after Balberra in
story mode and in the standalone Impact encounter. His six attack branches,
movement, combat animations, volleys, meteor flight, and charge effects share
the faster pace. Afterimage history is sampled with each extra movement step.
The shield and melee contact proxies follow his faster pose without replaying
collision processing; meteor hit-stun, player-special sequences, actual hit
reactions, and the introduction/ending retain their native timing.

## Building

The build requires Bash, Make, a MIPS-capable LLVM Clang, LLD, and `RecompModTool`. Apple Clang cannot target MIPS; on macOS the script automatically selects Homebrew LLVM and LLD.

Run:

```sh
./build_mod.sh -j4
```

Each run generates two packages:

- `build/mnsg_extra_options.nrm`: normal build, with debugging compiled out.
- `build/debug_mnsg_extra_options.nrm`: debug build, with one-hit Impact boss kills enabled.

`src/debug.c` is controlled by the compile-time flag `EXTRA_OPTIONS_DEBUG`.
The script passes `EXTRA_OPTIONS_DEBUG=1` for debug and `=0` for normal;
plain `make` defaults to `0`. Switching the flag rebuilds cached objects.
There is no debug menu setting. The debug build preserves native hit/block,
defeat, and scripted phase handling, and works independently of Hyper Enemies.
For Balberra, a damaging hit on his body or any vulnerable body component
also makes the body HP lethal. Component damage remains normal; the native
body update processes defeat, including any pending special-hit reaction.
Blocked hits do not trigger the cheat.
Both packages share the same mod ID, so install only one at a time.

`build_mod.sh` looks for `RecompModTool` in this repository, in the sibling `mnsg-recomp-example` repository, or on `PATH`. You can also provide an explicit path:

```sh
RECOMP_MOD_TOOL=/path/to/RecompModTool ./build_mod.sh -j4
```

## Regression tests

With a sibling `Goemon64Recomp` checkout containing `RecompiledFuncs`, run
`bash tests/debug_balberra_native_test.sh` (or set `GOEMON64_RECOMP_DIR`).
This executes the generated native damage and body-finalizer functions with
the debug hook enabled and disabled, checking lethal body/part hits, blocks,
invulnerability, and special-hit reaction priority. Generated game code stays
outside this repository. These tests do not replace in-game boss testing.

D'Etoile's native movement and attack-clock checks run with
`bash tests/hyper_detoile_native_test.sh`, using the same sibling checkout.
The production-hook guard tests can run independently:

```sh
cc -std=c11 -Wall -Wextra -Werror -I include tests/hyper_detoile_test.c -o /tmp/hyper_detoile_test
/tmp/hyper_detoile_test
cc -std=c11 -Wall -Wextra -Werror -I include tests/game_over_to_title_test.c -o /tmp/game_over_to_title_test
/tmp/game_over_to_title_test
```
