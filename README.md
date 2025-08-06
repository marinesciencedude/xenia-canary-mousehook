# Mousehook

This is a fork of [emoose's Xenia build](https://github.com/emoose/xenia) as originally [ported to Canary by Marcelo20XX](https://www.reddit.com/r/emulation/comments/qppb6d/goldeneye_xbla_with_updated_xenia_canary_mousehook/).

Mousehook implements mouse input into games by injecting into game memory, most commonly to manipulate camera values or cursors.

## Supported Games
| Game  | Supported versions  | Title ID | Mouse support | Notes|
| --- | --- | --- | --- | --- |
| Orange Box | All Games TU0 | 4541080F | Fair |
| Portal Still Alive |  | 58410960 | Fair |
| CSGO | | 5841125A | Fair |
| CSGO Beta |  | 5841125A | Fair |
| Left 4 Dead 2 | TU0 | 454108D4 | Fair |
| Left 4 Dead | TU0, GOTY | 45410830 | Fair |
| Portal 2 |  TU0 | 45410912 | Fair |
| Team Fortress 2 | TU0 | 4541080F | Fair |
| Bloody Good Time |  | 584109B3 | Fair |
| Postal III | | 4541080F | Fair |
| GoldenEye XBLA | Nov 16th 2007, also renamed as 'Aug 25th 2007' | 584108A9 | Good | <sub> Camera X rotation might not work when using the tank in the Runway and Street level |
| Perfect Dark Zero | TU0,TU3 & Platinum Hits base | 4D5307D3 | Poor | <sub> No mousehook for Spycam in <br/>Mousehook bindings break in menus and switches to using HID.Winkey bindings.|
| Perfect Dark XBLA | b33, b52 (TU0), b102, b104, b107 | 584109C2 | Fair |<sub> Camera doesn't work with the camspy <br/>No mousehook for HoverBike |
| Halo 3 | TU0/TU3 & 08172 'delta' | 4D5307E6 | Fair |
| Halo 3: ODST | | 4D530877 | Fair |
| Halo Reach | TU0/TU1 | 4D53085B | Fair |
| Halo 4 | TU0/TU8 | 4D530919 | Fair |
| Crackdown 2 | TU0/TU5 | 4D5308BC | Poor | <sub>Only works on-foot |
| Saints Row 1 | TU1 US/TU0 JP | 545107D1 / 545107F8 | Fair[^1] | <sub> weapon10 Reloads current weapon and in conjuction with sr_disable_shared_reload=true which decouples reloading from the internal (A) button which in-game is shared between reload and pickup/replace items. <br/>X-Axis can randomly flick to north when using the McManus sniper rifle. <br/>In-game frame limiter might cause mouse to stutter, use Unlock FPS patch and limit framerate externally if desired. <br/>X axis may stutter when moved while entering vehicles.| 
| Saints Row 2 | TU3 (8.0.3) | 545107FC | Good[^1] | <sub> ***Some*** diversions/activities might not work great with the mouse, use arrow keys binding for RS <br/>In-game frame limiter might cause mouse to stutter, use Unlock FPS patch and limit framerate externally if desired. <br/>X axis may stutter when moved while entering vehicles. <br/>X-Axis camera in vehicles might not work without `sr_better_drive_cam` set to true (already defaulted to true) | 
| Dark Messiah of Might and Magic | Singleplayer & Multiplayer | 55530804 | Fair|
| Just Cause | TU0 | 534307D5 | Poor | <sub> Only works on-foot
| Red Dead Redemption | Original TU0/TU9, Undead Nightmare (Platinum Hits) TU4 & Game Of The Year Edition Disk 1/2 TU0| 5454082B | Good | <sub> Duel crosshair isn't mousehooked, RS is emulated when in duels |
| Far Cry Instincts: Predator | TU0 | 555307DC | Fair |
| Dead Rising 2 Case West | TU0 | 58410B00 | Fair | <sub>Disable cam-chase in-game options. |
| Dead Rising 2 Case Zero | TU0 | 58410A8D | Fair | <sub>Disable cam-chase in-game options. |
| Call Of Duty 3 | Singleplayer & Multiplayer TU0/TU3 | 415607E1 | Fair | <sub> Quicktime events that use Right-stick doesn't work with the mouse, have to use modifier binding to emulate RS <br/>Default: **Capslock**
| Call Of Duty 4 | Singleplayer & Multiplayer TU0/TU4 / 253,270,290 & 328 alphas | 415607E6 | Fair |
| Call Of Duty World At War | Singleplayer & Multiplayer TU7 | 4156081C | Fair |
| Call Of Duty Modern Warfare 2 | Singleplayer & Multiplayer TU0 / 482 alpha SP ".xex only" | 41560817 | Fair |
| Call Of Duty Future Warfare "NX1" | Nightly_SP_maps / nx1sp.xex / nx1mp_demo.xex / nx1mp.xex / NightlyMPmaps | 4156089E | Fair |
| Call Of Duty Black Ops 2  | Greenlight .xex only <br/>DLC 5 builds xex/exe <br/>version ZMBUILD-764 c4b2078a | 415608C3 | Fair |
| Call Of Duty Ghosts Alpha | 2-iw6mp.exe / 1-iw6sp.exe / default.xex "May 08 2013 build" | 4156088E | Fair |
| Call Of Duty Advanced Warfare | Singleplayer & Multiplayer TU17 | 41560914 | Fair | <sub> Modifier bound to readback_resolve <br/>Default: **Capslock** |
| Wolfenstein | Singleplayer TU0 | 415607DE | Fair |
| Gears Of Wars 1 | TU0/TU5 | 4D5307D5 | Fair[^1] | <sub> Might not work in languages other than in English, also some patches could break mousehook.
| Gears Of Wars 2 | TU0/TU6 | 4D53082D | Fair[^1] | <sub> Might not work in languages other than in English, also some patches could break mousehook.
| Gears Of Wars 3 | TU0/TU6 | 4D5308AB | Fair[^1] | <sub> Might not work in languages other than in English, also some patches could break mousehook. 
| Gears Of Wars Judgement | TU0/TU4| 4D530A26 | Fair[^1] | <sub> Might not work in languages other than in English, also some patches could break mousehook.
| Section 8 | TU0 | 475007D4 | Fair | <sub> Might not work in languages other than in English, also some patches could break mousehook. <br/>Gun models exhibit odd swaying. |
| Minecraft | TU75 (1.0.80) | 584111F7 | Good | <sub> Camera exhibits stuttering when moving the player and camera at the same time. |

[^1]: Mousehook implements a right-stick workaround for these games, it disables right-stick usage by slowing sensitivity to an extremely slow speed and ties mouse movement to it, this fixes several in-game camera modes ranging from vehicles, ADS, auto centering & more.
### [Netplay Mousehook](https://github.com/marinesciencedude/xenia-canary-mousehook/tree/netplay_canary_experimental)

<p align="center">
    <a href="https://github.com/xenia-canary/xenia-canary/tree/canary_experimental/assets/icon">
        <img height="256px" src="https://raw.githubusercontent.com/xenia-canary/xenia/master/assets/icon/256.png" />
    </a>
</p>

<h1 align="center">Xenia Canary - Xbox 360 Emulator</h1>

Xenia Canary is an experimental fork of the Xenia emulator. For more information, see the
[Xenia Canary wiki](https://github.com/xenia-canary/xenia-canary/wiki).

Come chat with us about **emulator-related topics** on [Discord](https://discord.gg/Q9mxZf9).
For developer chat join `#dev` but stay on topic. Lurking is not only fine, but encouraged!
Please check the [FAQ](https://github.com/xenia-canary/xenia-canary/wiki/FAQ) page before asking questions.
We've got jobs/lives/etc, so don't expect instant answers.

Discussing illegal activities will get you banned.

## Status

Buildbot | Status | Releases
-------- | ------ | --------
Canary (🪟, 🐧) | [![CI](https://github.com/xenia-canary/xenia-canary/actions/workflows/Orchestrator.yml/badge.svg?branch=canary_experimental)](https://github.com/xenia-canary/xenia-canary/actions/workflows/Orchestrator.yml/badge.svg?branch=canary_experimental) [![Codacy Badge](https://app.codacy.com/project/badge/Grade/cd506034fd8148309a45034925648499)](https://app.codacy.com/gh/xenia-canary/xenia-canary/dashboard?utm_source=gh&utm_medium=referral&utm_content=&utm_campaign=Badge_grade) | [Latest](https://github.com/xenia-canary/xenia-canary/releases/latest) ◦ [All](https://github.com/xenia-canary/xenia-canary/releases) ◦ [Old](https://github.com/xenia-canary/xenia-canary-releases/releases)

### Experimental Netplay

Buildbot | Status | Releases
-------- | ------ | --------
Windows | [![Codacy Badge](https://app.codacy.com/project/badge/Grade/d814c4b6aa444dcc9c1631e0224b2739)](https://app.codacy.com/gh/AdrianCassar/xenia-canary/dashboard?utm_source=gh&utm_medium=referral&utm_content=&utm_campaign=Badge_grade) | [Latest](https://github.com/AdrianCassar/xenia-canary/releases/latest)

## Quickstart

See the [Quickstart](https://github.com/xenia-canary/xenia-canary/wiki/Quickstart) page.

## FAQ

See the [frequently asked questions](https://github.com/xenia-canary/xenia-canary/wiki/FAQ) page.

## Game Compatibility

See the [Game compatibility list](https://github.com/xenia-canary/game-compatibility/issues)
for currently tracked games, and feel free to contribute your own updates,
screenshots, and information there following the [existing conventions](https://github.com/xenia-canary/game-compatibility/blob/canary/README.md).

## Building

See [building.md](docs/building.md) for setup and information about the
`xb` script. When writing code, check the [style guide](docs/style_guide.md)
and be sure to run clang-format!

## Contributors Wanted!

Have some spare time, know advanced C++, and want to write an emulator?
Contribute! There's a ton of work that needs to be done, a lot of which
is wide open greenfield fun.

**For general rules and guidelines please see [CONTRIBUTING.md](.github/CONTRIBUTING.md).**

Fixes and optimizations are always welcome (please!), but in addition to
that there are some major work areas still untouched:

* Help work through [missing functionality/bugs in games](https://github.com/xenia-canary/xenia-canary/labels/compat)
* Reduce the size of Xenia's [huge log files](https://github.com/xenia-canary/xenia-canary/issues/1526)
* Skilled with Linux? A strong contributor is needed to [help with porting](https://github.com/xenia-canary/xenia-canary/labels/platform-linux)

See more projects [good for contributors](https://github.com/xenia-canary/xenia-canary/labels/good%20first%20issue). It's a good idea to ask on Discord and check the issues page before beginning work on
something.

## Disclaimer

The goal of this project is to experiment, research, and educate on the topic
of emulation of modern devices and operating systems. **It is not for enabling
illegal activity**. All information is obtained via reverse engineering of
legally purchased devices and games and information made public on the internet
(you'd be surprised what's indexed on Google...).
