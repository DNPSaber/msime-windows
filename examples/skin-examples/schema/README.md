# 外部皮肤 manifest（TOML）

每个皮肤目录需要 `skin.toml`，`id` 必须与文件夹名一致。`schema_version` 当前为 `1`。

候选框和悬浮工具栏都由 `skin.toml` 声明，不要再提供 `cand.css` 或工具栏 CSS。所有图片路径都相对于皮肤目录，且不能指向目录之外。

## 顶层字段

- 必填：`schema_version`、`id`、`name`、`version`、`base`（继承的内置皮肤：`fluent`、`wechat`、`graphite`、`willow_green`、`autumn_osmanthus`）
- 可选：`author`、`description`
- `[supports]`：`layouts`（`horizontal` / `vertical`）、`themes`（`dark` / `light`）
- `[license]`：`code`、`assets`

## `[candidate_window]`

- `min_width_dip`：卡片最小宽度；实际最小宽度还会被装饰图的 `width_dip` 撑大
- `corner_radius_dip`：卡片圆角，`0`–`32`；不写则沿用 `base`

### `[candidate_window.decoration]`（可选）

卡片上方的装饰图。写了这张表就必须同时给出 `image`、`top_inset_dip`、`width_dip`。

- `image`：图片路径
- `top_inset_dip` / `width_dip`：装饰框的高和宽，框贴在卡片上方，不与卡片重叠；图片在框内等比缩放
- `align`：`left` | `center` | `right`，相对卡片

### `[candidate_window.background]`（可选）

卡片背景图，画在底色之上、文字之下，按圆角裁剪，对深浅两套主题都生效。

- `image`：图片路径
- `fit`：`cover`（铺满裁切）| `contain`（完整显示）| `stretch`（拉伸）
- `opacity`：`0`–`1`

## `[candidate.dark]` / `[candidate.light]`

`accent`、`selected`、`hover`、`surface`、`border`、`text`、`number`、`show_selected_bar`

## `[toolbar]`

悬浮工具栏样式，D2D 与 WebView2 两个渲染器都生效。所有键都可省略，不写的沿用 `base`。

- `corner_radius_dip`：`0`–`32`
- `[toolbar.dark]` / `[toolbar.light]`：`background`、`border`、`handle`（左侧拖动条）、`divider`、`icon`、`hover`

## 颜色格式

支持 `#rgb` / `#rrggbb` / `#rrggbbaa` 和 `rgb()` / `rgba()`。
