# 外部皮肤 manifest（TOML）

每个皮肤目录需要 `skin.toml`，`id` 必须与文件夹名一致。`schema_version` 当前为 `1`。

候选框由 D2D 绘制，颜色写在 `[candidate.dark]` / `[candidate.light]`，不要再提供 `cand.css`。

可选字段：

- `toolbar_stylesheet`：悬浮工具栏仍是 WebView，可继续用 CSS
- `preview`：设置页装饰图，相对皮肤目录
- `[candidate.*.]`：`accent`、`selected`、`hover`、`surface`、`border`、`text`、`number`、`show_selected_bar`

颜色支持 `#rgb` / `#rrggbb` / `#rrggbbaa` 和 `rgb()` / `rgba()`。
