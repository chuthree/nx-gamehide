<div align="center">

<img src="assets/readme-banner.png" alt="NX-GameHide / 游戏隐藏" width="800">

[English](README.md) | 简体中文

通过 Tesla 隐藏和恢复 Nintendo Switch HOME 中的游戏。<br>
不删除游戏或存档。

[安装](#安装) · [使用](#使用) · [隐藏范围](#隐藏范围) · [卸载](#卸载) · [文档与反馈](#文档与反馈)

</div>

<br>

## 安装

需要 [Atmosphère](https://github.com/Atmosphere-NX/Atmosphere)、[Tesla Menu](https://github.com/WerWolv/Tesla-Menu) 和 [nx-ovlloader](https://github.com/WerWolv/nx-ovlloader)。

已测试环境：HOS 22.5.0 / Atmosphère 1.11.2。

1. 从 [Releases](https://github.com/chuthree/nx-gamehide/releases) 下载 `NX-GameHide-v<版本号>.zip`。
2. 解压到 SD 卡根目录。
3. 完整重启主机。

## 使用

操作前请先退出对应游戏。

1. 在 Tesla 菜单中打开 **GameHide**。
2. 选中游戏，按 **A** 切换隐藏或显示。

选择 **恢复全部游戏** 可清空隐藏名单，显示全部游戏。

![通过 Tesla 隐藏和恢复游戏](assets/demo.gif)

## 隐藏范围

以下为实机测试结果，完整重启后仍保持隐藏。

| 界面 | 隐藏后 |
| --- | --- |
| HOME 游戏列表 | 不显示 |
| 分组 | 不显示 |
| 设置 → 数据管理 → 软件的详情管理 | 不显示 |
| 设置 → 数据管理 → 删除保存数据 | 显示 |
| 其他启动器 | 尚未验证 |

隐藏期间若打开“分组”，恢复显示后需手动将游戏重新加入分组；未打开过则保留原分组归属。

## 卸载

删除 SD 卡上的以下文件和目录，然后完整重启主机，游戏会重新显示在 HOME 中。

- `switch/.overlays/nx-gamehide.ovl`
- `atmosphere/contents/00FF48474D000001`

保留 `config/nx-gamehide` 可保存隐藏名单；删除该目录可清除已保存的设置。

## 文档与反馈

- [故障排查（英文）](docs/troubleshooting.md)
- [构建说明（英文）](docs/build.md)
- [反馈问题](https://github.com/chuthree/nx-gamehide/issues)

## 许可证

[GPL-2.0-only](LICENSE)。第三方许可声明随安装包提供。源码和依赖获取方式见[构建说明（英文）](docs/build.md)。
