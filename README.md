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

glfm has no `AKEYCODE_BUTTON_*` mapping and routes controller axes to the touch callback, so the
build carries a local patch in
[thirdparty/glfm-gamepad-keys.patch](app/src/main/cpp/thirdparty/glfm-gamepad-keys.patch). CMake
stops with an error if `thirdparty/glfm` is re-fetched without it; re-apply with

```
git -C app/src/main/cpp/thirdparty/glfm apply ../glfm-gamepad-keys.patch
```

# Bug
1. Graphic issues.
2. Map issues.
3. Quests are not implemented.

[Cosmic]:              https://github.com/P0nk/Cosmic/tree/master
[MapleServerAndroid]:  https://github.com/speedyHKjournalist/MapleServerAndroid/tree/main
[NXFiles]: https://drive.google.com/drive/folders/1vloXiDOAQOUPfSO3aCjRBJDbtQ1PZTuU?usp=share_link