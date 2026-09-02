# Metasequoia IME skin example

这是 Metasequoia IME 外部候选框皮肤的最小示例。候选框由 D2D 绘制，皮肤用 `skin.toml` 声明名称、装饰几何和配色；悬浮工具栏仍可用可选 CSS。

## 快速开始

1. 用自己的透明 PNG 替换 `skins/niya-demo/assets/character.png`。
2. 打开 `preview.html`，同时检查横向和纵向候选框。
3. 修改 `skins/niya-demo/skin.toml` 中的名称、作者、装饰尺寸和 `[candidate.*]` 颜色。
4. 将整个皮肤文件夹复制到 `%LOCALAPPDATA%\metasequoiaime\skins\`，打开设置的“皮肤”页面（会自动扫描目录），也可以点击“刷新皮肤”或运行 `./install.ps1`。

安装脚本会将皮肤复制到：

```text
%LOCALAPPDATA%\metasequoiaime\skins\niya-demo\
```

默认还会将 `config.toml` 中的 `appearance.candidate_skin` 设置为 `niya-demo`；只想复制、不立即启用时使用 `./install.ps1 -NoActivate`。

设置页面会读取每个一级子目录中的 `skin.toml`。目录名必须与 manifest 的 `id` 一致；字段不合法或工具栏 CSS 缺失的目录不会进入可选列表，并会显示在页面底部的诊断信息中。

## 仓库与皮肤包结构

```text
metasequoia-ime-skin-example/
├─ skins/
│  └─ niya-demo/             # 一个皮肤一个文件夹
│     ├─ skin.toml           # 名称、继承关系、能力、几何、候选配色
│     ├─ toolbar.css         # 可选：悬浮工具栏视觉
│     └─ assets/
│        └─ character.png    # 本地静态资源
├─ schema/                   # 字段说明
├─ preview.html              # 仓库级预览入口
└─ install.ps1               # demo 安装工具
```

仓库中的 `preview.html`、`schema/` 和安装脚本是制作工具，不会复制进已安装的皮肤目录。

## 最常修改的参数

```toml
[candidate_window]
min_width_dip = 176

[candidate_window.decoration]
top_inset_dip = 88
width_dip = 136

[candidate.dark]
accent = "#e08aa8"
selected = "rgba(224, 138, 168, 0.28)"
hover = "rgba(224, 138, 168, 0.16)"
```

- `top_inset_dip`：候选框上方为图片预留的高度。
- `width_dip`：顶部装饰宽度。
- `min_width_dip`：候选框最小宽度，通常不应小于 `width_dip`。
- `accent` / `selected` / `hover`：光标与胶囊条、高亮行、悬停行。

示例图片高 `118px`，其中下方约 `30px` 被候选框遮住，所以只露出头部。

## 制作约束

- 皮肤使用 `skin.toml`、可选 `toolbar.css` 和本地图片，不加载自定义 JavaScript。
- 候选框不要再写 `cand.css`。
- ID 只能包含小写字母、数字、点、下划线和连字符，最长 64 个字符。
- 图片建议控制在 500 KB 内。

## 授权提示

皮肤代码和图片资源可以使用不同的许可。发布前请在 `skin.toml` 的 `[license]` 中分别写明 `code` 与 `assets`。
