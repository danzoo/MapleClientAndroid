# resources/ —— 客户端资源基准

这个目录本身就是客户端资源基准（版本管理副本）。资源全部放在
`resources/Data/` 下面，这一层 `Data` 对应实机的

```
/storage/emulated/0/Android/data/com.OpenMapleClient/files/Data
```

也就是说 `resources/Data/` 的目录树和设备的 `Data/` 逐层一致
（`Base/`、`Character/`、`Map/`、`Sound/`、`UI/` …），推送时把
`resources/Data/` 整个目录推到 `files/` 下即可，不用再手工拼路径。

本文件 `README.md` 有意留在 `resources/` 这一层、不在 `Data/` 里面，
所以按目录同步时不会被推到设备的 `Data/` 里，破坏「设备 Data 与基准逐文件一致」。

## 走 git-lfs 的那一个文件

`Data/Sound/Skill.img`（134,412,532 字节 = 128.2 MiB）超过 GitHub 单文件 100 MiB 的硬限制，
普通提交会被服务端拒收，所以它由仓库根的 `.gitattributes` 单独交给 git-lfs 跟踪：

```
resources/Data/Sound/Skill.img filter=lfs diff=lfs merge=lfs -text
```

后果是**克隆前必须先装 git-lfs**（`git lfs install`），否则 checkout 出来的
`Skill.img` 只是一个 134 字节的指针文本，客户端读不到任何音效节点、技能音效静默缺失
（`Music::play()` 拿不到节点就直接返回，logcat 里不会有任何报错）。
用 `git lfs ls-files` 能看到它，用 `git lfs pull` 可以补下真文件。

其余 7600 个归档都是普通 blob，不走 LFS。

## 与源头基准的关系

工作基准（也是实机 `Data/` 必须逐文件一致的那份）仍然是

```
F:\Code\MapleAndroid\Assets\v209\img_develop
```

删减/补素材的工具都在 `F:\Code\MapleAndroid\Assets\`：`imgcut.py`（往归档里加子树）、
`mapdeps.py`（地图依赖闭包）、`imgprune.py`（按清单移动，`--restore` 还原）、
`imgtrim.py`（剪掉没人引用的子树）。改动流程：先改工作基准 → 同步实机 →
把同名文件按相对路径拷进 `resources/Data/` 一并提交。

## 不会被打包进 APK

`app/build.gradle` 的 `sourceSets.main.assets.srcDirs` 固定为 `src/main/assets`，
仓库根下的 `resources/` 不属于任何 module 的资源目录，Gradle 不会把它打进 APK。
