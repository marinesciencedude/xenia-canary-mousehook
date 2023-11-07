# Netplay Fork

This is a fork of [Xenia-Canary Netplay](https://github.com/ahnewark/sunrise-xenia-canary-netplay) which implements online multiplayer features.
It has been built from the ground up and a handful of games found to work, though many games are yet to be tested. Check out the [Issues](https://github.com/ahnewark/sunrise-xenia-canary-netplay/issues) list for more details.

The REST API powering this fork can be found [here](https://github.com/AdrianCassar/Xenia-WebServices).

Massive thanks to @SarahGreyWolf for testing this fork with me for about a month, couldn't have done it without her.
Also, thank you to @Bo98 for creating the burnout5 xenia fork, I used that as a basis for this, and some of the code is still here I think.

Peace and hair grease

Codie

---

## FAQ:

Is UPnP implemented?
- Yes, **UPnP** is now implemented therefore manual port forwarding is no longer required.

Is **Systemlink** or **XLink Kai** supported?

- No, Systemlink and XLink Kai are not supported.

Can I host Xenia Web Services?

- Yes, [Xenia Web Services](https://github.com/AdrianCassar/Xenia-WebServices).

Is there a Netplay mousehook build?

- Yes, download it from [Netplay Mousehook](https://github.com/marinesciencedude/xenia-canary-mousehook/releases?q=Netplay).

Are games dependant on servers?

- Yes a lot of games are dependant on servers therefore will not work, unless a server is developed for that game. For example many games requires EA servers, without them netplay will not work. 

Can I use multiple instances for the same game?

- No, you cannot use multiple instances due to the first instance using up ports. You will need to use a VM.

Can I use multiple PCs on the same network?

- No, you currently cannot use different PCs on the same network to connect to a session hosted on the same network. However a VPN will get around this issue.

Where can I **download** the Canary Netplay build?

- You can download it from [releases](https://github.com/AdrianCassar/xenia-canary/releases).

## Config Setup

To connect to a **Xenia WebServices** server you can either privately host it yourself or connect to my server.

```toml
api_address = "https://xenia-netplay-2a0298c0e3f4.herokuapp.com/"
```

UPnP is disabled by default, you can enable it in the config.
```toml
upnp = true
```

## Supported Games

| Game | Notes | Patches/Plugins |
|---|---|---|
| CS:GO | Mousehook support |
| CS:GO Beta | Mousehook support |
| GTA V Beta | [Video 1](https://www.youtube.com/watch?v=nIjZ7sRGZlo), [Video 2](https://www.youtube.com/watch?v=YIBjy5ZJcq4) |
| GTA V TU 2-13 | Very unstable and will crash often |
| Gundam Operation Troy | [English patch](https://github.com/Eight-Mansions/MSGOT/releases)
| Halo 3 ODST v13.2 using [Sunrise Server](https://github.com/ahnewark/Sunrise-Halo3-WebServices) | [Video](https://www.youtube.com/watch?v=amS8OxH3exs), Mousehook support | [Halo 3 Patch](https://github.com/AdrianCassar/Xenia-WebServices/blob/main/patches/4D5307E6%20-%20Halo%203.patch.toml)
| Left 4 Dead 2 | Mousehook support |
| Left 4 Dead 2 Demo |
| Marvel Ultimate Alliance | |
| Marvel Ultimate Alliance 2 | |
| Portal 2 | Mousehook support |
| Saints Row 2 | [Video 1](https://www.youtube.com/watch?v=YTw84keeWfs), [Video 2](https://www.youtube.com/watch?v=nf7TDOtTEIE) |
| Saints Row the Third / The Full Package | Unplayable due to broken graphics. Requires [Online Pass](https://www.xbox.com/en-GB/games/store/online-pass/BS7JTR0MN356) + license_mask|
| Saints Row IV | Unplayable due to broken graphics. Requires Online Pass + license_mask|
| Team Fortress 2 | Mousehook support |
---

### Non-Supported Games

| Game  | Description  |
|---|---|
| Gears of War 3 | Connects online but cannot play with others.  |
| Grand Theft Auto 4 | Connects online but cannot play with others. |
| Minecraft | Requires friend lists to invite friends. |
| Quantum of a Solace | Crashes on start-up.  |
| Red Dead Redemption  | Connects online but cannot play with others. |
| Saints Row 1 | Unstable to find sessions to join. |

#### Requires Servers
- Activision Games
- EA Games
- Ubisoft Games

---

# Mousehook

This is also a fork of [emoose's Xenia build](https://github.com/emoose/xenia) as originally [ported to Canary by Marcelo20XX](https://www.reddit.com/r/emulation/comments/qppb6d/goldeneye_xbla_with_updated_xenia_canary_mousehook/).

## Supported Games

| Game  | Notes  |
|---|---|
| Orange Box | All Games TU0 |
| Portal Still Alive |
| CSGO | |
| CSGO Beta | |
| Left 4 Dead 2 | TU0 |
| Left 4 Dead | TU0 |
| Portal 2 |  TU0 |
| Team Fortress 2 | TU0 |
| GoldenEye XBLA | Nov 16th 2007, also renamed as 'Aug 25th 2007' |
| Perfect Dark XBLA | b33, b52 (TU0) & b102 |
| Halo 3 | TU0/TU3 & 08172 'delta' |
| Halo 3: ODST | |
| Halo Reach | TU0/TU1 |
| Halo 4 | TU0/TU8 |
| Crackdown 2 | TU0/TU5 |

---

<p align="center">
    <a href="https://github.com/xenia-project/xenia/tree/master/assets/icon">
        <img height="120px" src="https://raw.githubusercontent.com/xenia-project/xenia/master/assets/icon/128.png" />
    </a>
</p>

<h1 align="center">Xenia - Xbox 360 Emulator</h1>

Xenia is an experimental emulator for the Xbox 360. For more information, see the
[main Xenia wiki](https://github.com/xenia-project/xenia/wiki).

**Interested in supporting the core contributors?** Visit
[Xenia Project on Patreon](https://www.patreon.com/xenia_project).

Come chat with us about **emulator-related topics** on [Discord](https://discord.gg/Q9mxZf9).
For developer chat join `#dev` but stay on topic. Lurking is not only fine, but encouraged!
Please check the [FAQ](https://github.com/xenia-project/xenia/wiki/FAQ) page before asking questions.
We've got jobs/lives/etc, so don't expect instant answers.

Discussing illegal activities will get you banned.

## Status

Buildbot | Status | Releases
-------- | ------ | --------
[Windows](https://ci.appveyor.com/project/benvanik/xenia/branch/master) | [![Build status](https://ci.appveyor.com/api/projects/status/ftqiy86kdfawyx3a/branch/master?svg=true)](https://ci.appveyor.com/project/benvanik/xenia/branch/master) | [Latest](https://github.com/xenia-project/release-builds-windows/releases/latest) ◦ [All](https://github.com/xenia-project/release-builds-windows/releases)
[Linux](https://cloud.drone.io/xenia-project/xenia) | [![Build status](https://cloud.drone.io/api/badges/xenia-project/xenia/status.svg)](https://cloud.drone.io/xenia-project/xenia)

Quite a few real games run. Quite a few don't.
See the [Game compatibility list](https://github.com/xenia-project/game-compatibility/issues)
for currently tracked games, and feel free to contribute your own updates,
screenshots, and information there following the [existing conventions](https://github.com/xenia-project/game-compatibility/blob/master/README.md).

## Disclaimer

The goal of this project is to experiment, research, and educate on the topic
of emulation of modern devices and operating systems. **It is not for enabling
illegal activity**. All information is obtained via reverse engineering of
legally purchased devices and games and information made public on the internet
(you'd be surprised what's indexed on Google...).

## Quickstart

See the [Quickstart](https://github.com/xenia-project/xenia/wiki/Quickstart) page.

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

* Help work through [missing functionality/bugs in games](https://github.com/xenia-project/xenia/labels/compat)
* Reduce the size of Xenia's [huge log files](https://github.com/xenia-project/xenia/issues/1526)
* Skilled with Linux? A strong contributor is needed to [help with porting](https://github.com/xenia-project/xenia/labels/platform-linux)

See more projects [good for contributors](https://github.com/xenia-project/xenia/labels/good%20first%20issue). It's a good idea to ask on Discord and check the issues page before beginning work on
something.

## FAQ

See the [frequently asked questions](https://github.com/xenia-project/xenia/wiki/FAQ) page.
