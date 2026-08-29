# Metasequoia IME skin example

这是 Metasequoia IME 外部候选框皮肤的最小示例。皮肤继承输入法内置候选框模板，只覆盖视觉样式和声明额外的窗口装饰空间，不接管候选生成、点击、翻页或 WebView 消息逻辑。

## 快速开始

1. 用自己的透明 PNG 替换 `skins/niya-demo/assets/character.png`。
2. 打开 `preview.html`，同时检查横向和纵向候选框。
3. 修改 `skins/niya-demo/skin.json` 中的名称、作者和装饰尺寸。
4. 将整个皮肤文件夹复制到 `%LOCALAPPDATA%\metasequoiaime\skins\`，打开设置的“皮肤”页面（会自动扫描目录），也可以点击“刷新皮肤”或运行 `./install.ps1`。

安装脚本会将皮肤复制到：

```text
%LOCALAPPDATA%\metasequoiaime\skins\niya-demo\
```

默认还会将 `config.toml` 中的 `appearance.candidate_skin` 设置为 `niya-demo`；只想复制、不立即启用时使用 `./install.ps1 -NoActivate`。

设置页面会读取每个一级子目录中的 `skin.json`。目录名必须与 manifest 的 `id` 一致；缺少 CSS/预览资源、路径越界或字段不合法的目录不会进入可选列表，并会显示在页面底部的诊断信息中。

## 仓库与皮肤包结构

```text
metasequoia-ime-skin-example/
├─ skins/
│  └─ niya-demo/             # 一个皮肤一个文件夹
│     ├─ skin.json           # 名称、继承关系、能力和宿主几何
│     ├─ cand.css            # 候选框视觉，不放业务 JavaScript
│     ├─ toolbar.css         # 可选：悬浮工具栏视觉
│     └─ assets/
│        └─ character.png    # 本地静态资源
├─ schema/                   # 皮肤 manifest Schema
├─ preview.html              # 仓库级预览入口
└─ install.ps1               # demo 安装工具
```

仓库中的 `preview.html`、`schema/` 和安装脚本是制作工具，不会复制进已安装的皮肤目录。

`skin.json` 中可选的 `preview` 字段用于设置页缩略图，例如 `"preview": "assets/character.png"`。可选的 `toolbarStylesheet` 指向悬浮工具栏 CSS，设置页会把它叠在内置工具栏预览上。

## 最常修改的参数

`skins/niya-demo/skin.json`：

```json
"candidateWindow": {
  "minWidthDip": 176,
  "decoration": {
    "topInsetDip": 88,
    "widthDip": 136
  }
}
```

- `topInsetDip`：候选框上方为图片预留的高度；宿主会向上扩展同样距离，候选框本身仍停在原来的 caret/preedit 锚点。
- `widthDip`：原生窗口顶部允许绘制和接收命中的右侧装饰宽度。
- `minWidthDip`：候选框最小宽度，通常不应小于 `widthDip`。

`cand.css` 中的图片显示尺寸应与 manifest 协调。示例图片高 `118px`，其中下方 `30px` 被候选框遮住，所以只露出头部。

## 制作约束

- 皮肤只能使用 `skin.json`、CSS 和本地资源，不加载自定义 JavaScript。
- 不复制或修改候选项 DOM；使用内置的 `.containerParent`、`.container` 等稳定选择器。
- 图片装饰必须设置 `pointer-events: none`，避免拦截候选点击。
- 所有资源使用相对路径，例如 `url("./assets/character.png")`。
- ID 只能包含小写字母、数字、点、下划线和连字符，最长 64 个字符。
- 图片建议控制在 500 KB 内，并按实际显示尺寸准备 2x 或 3x 分辨率。

## 明暗模式

这个示例同时声明 `dark` 和 `light`。设置页始终可以预览另一种配色；实际输入法窗口会按当前主题选用对应覆盖。CSS 中请同时写宿主属性和设置页 class：

```css
html[data-candidate-theme="light"] { /* 浅色覆盖 */ }
html[data-candidate-theme="dark"]  { /* 深色覆盖 */ }
.theme-light { /* 设置页预览 */ }
.theme-dark  { /* 设置页预览 */ }
```

## 授权提示

皮肤代码和图片资源可以使用不同的许可。发布前请在 `skin.json` 中分别写明 `license.code` 与 `license.assets`。没有明确再分发许可的图片不应提交到公开仓库；可以保留本地文件并换用具有明确许可的演示素材。

## 与候选框模板的关系

皮肤不携带候选框或工具栏 HTML。输入法内部维护共享模板，`base` 选择一套内置基础 CSS，当前皮肤的 `cand.css` / 可选 `toolbar.css` 最后加载并覆盖它。这样替换皮肤不会复制或分叉候选生成、测量、点击和翻页脚本。
