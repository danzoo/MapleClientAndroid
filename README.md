# OpenMapleClient
OpenMapleClient is a custom, made-from-scratch game client.

# Supported versions
- The client is currently compatible with version 83 servers.
- The client has only been tested with [Cosmic] on PC.
- The client has only been tested with [MapleServerAndroid] on Android.

# How To Play
1. Download required NX files from [NXFiles].
2. Install Client APK on your Android phone.
3. Copy all nxfiles to location /storage/emulated/0/Android/data/com.OpenMapleClient/files using file manager app like Root Explorer.
4. Launch A Server either on PC or on Android using servers mentioned Above.
5. Launch Client and specify hostip to connect to server.
6. Play!

# Server address (`hostip.txt`)

Create a plain-text file named `hostip.txt` in `/storage/emulated/0/Android/data/com.OpenMapleClient/files`.
It contains a single line - the address of the login server. IPv4, IPv6 and hostnames all work:

```
192.168.1.10
192.168.1.10:8484
maple.example.com
2001:db8::1
[2001:db8::1]
[2001:db8::1]:8484
```

A port may be appended after a colon. IPv6 literals **must** be bracketed when a port is appended,
because their own colons would otherwise be ambiguous. If no port is given, the `ServerPort` value
from the `Settings` file is used (default `8484`).

The server also sends a channel address when you select a character or change channel. The bundled
`MapleServerAndroid` uses a backwards-compatible extension of the v83 `SERVER_IP` / `CHANGE_CHANNEL`
packet to carry IPv6 addresses; IPv4 deployments keep the original 4-byte wire format and remain
compatible with stock v83 clients. A stock v83 client cannot follow an IPv6 channel redirect.

The server picks that address by the address family of the connection you made: IPv4 clients get the
`HOST` / `LANHOST` / `LOCALHOST` values (original 4-byte packet), IPv6 clients get `HOST6` /
`LANHOST6` / `LOCALHOST6` (IPv6 extension packet). So one server can serve both at once - just fill
in both sets in `config.yaml`. If the `*6` keys are left empty, IPv6 clients fall back to the IPv4
address and need IPv4 connectivity to enter a channel.

# Controller Support
A connected gamepad replaces the touch overlay while it stays in use.

| Input | Action |
| --- | --- |
| Left stick / D-pad | Move, climb ladders, take portals |
| A | Jump, confirm an open dialog |
| B | Attack (hold to keep attacking), close an open dialog |
| X | Talk to the nearest NPC |
| Y | Pick up the nearest drop |
| LB / RB | Items / Skills |
| LT / RT | Stats / Quick slots |
| L3 / R3 | World map / Mini map |
| Select | Close the focused window |
| Start | Main menu |

The windowing layer is [glfm], which the client has to carry changes in: it has no `AKEYCODE_BUTTON_*`
mapping and routes controller axes to the touch callback, and its render loop runs as fast as the
buffer queue allows rather than at the frame rate asked for. So `thirdparty/glfm` is a copy of the
upstream sources in this repository, built by `add_subdirectory` rather than fetched — no submodule
to initialise and no clone step that can quietly drop those edits. The copy is upstream
`607fc6ff8c89d58641c79adfb00ac9ced3652adc`; see [thirdparty/CMakeLists.txt](app/src/main/cpp/thirdparty/CMakeLists.txt)
for what is ours.

[glfm]: https://github.com/brackeen/glfm

# Bug
1. Graphic issues.
2. Map issues.
3. Quests are not implemented.

[Cosmic]:              https://github.com/P0nk/Cosmic/tree/master
[MapleServerAndroid]:  https://github.com/speedyHKjournalist/MapleServerAndroid/tree/main
[NXFiles]: https://drive.google.com/drive/folders/1vloXiDOAQOUPfSO3aCjRBJDbtQ1PZTuU?usp=share_link