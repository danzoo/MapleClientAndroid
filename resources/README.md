# resources/img_develop —— 客户端资源基准

这里存放客户端资源基准 `img_develop` 的版本管理副本。目录结构与实机
`/storage/emulated/0/Android/data/com.OpenMapleClient/files/Data` 一一对应
（`Base/`、`Character/`、`Map/`、`Sound/`、`UI/` …），所以同步到设备时整棵目录直接按相对路径推送即可。

## 唯一未入库的文件

`Sound/Skill.img`（134,412,532 字节 = 128.2 MiB）超过 GitHub 单文件 100 MiB 的硬限制，
因此没有提交进 git。原件在

```
F:\Code\MapleAndroid\Assets\v209\img_develop\Sound\Skill.img
```

换机器或新克隆时，这一个文件要从上面的路径单独拷贝，并单独 `adb push` 到设备，
否则技能音效缺失（客户端读不到节点时静默不播，日志里看不到）。

## 与源头基准的关系

工作基准（也是实机 `Data/` 必须逐文件一致的那份）仍然是

```
F:\Code\MapleAndroid\Assets\v209\img_develop
```

删减/补素材的工具都在 `F:\Code\MapleAndroid\Assets\`：`imgcut.py`（往归档里加子树）、
`mapdeps.py`（地图依赖闭包）、`imgprune.py`（按清单移动，`--restore` 还原）、
`imgtrim.py`（剪掉没人引用的子树）。改动流程：先改工作基准 → 同步实机 →
把同名文件拷进 `resources/img_develop/` 一并提交。

## 不会被打包进 APK

`app/build.gradle` 的 `sourceSets.main.assets.srcDirs` 固定为 `src/main/assets`，
仓库根下的 `resources/` 不属于任何 module 的资源目录，Gradle 不会把它打进 APK。
