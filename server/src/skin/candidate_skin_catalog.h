#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace CandidateSkinCatalog
{
struct CandidateColors
{
    std::string accent;
    std::string selected;
    std::string hover;
    std::string surface;
    std::string border;
    std::string text;
    std::string number;
    // 候选后的翻译文本。留空时沿用候选文字色并降到 62% 不透明度；设置后按原值绘制，选中行也不改。
    std::string translation;
    std::optional<bool> showSelectedBar;
    // 以下各项留空时的回退：candidate_text / preedit_text 用 text；preedit_caret / selected_bar 用 accent；
    // selected_text / selected_number 用基础皮肤的选中行配色；selected_translation 用 translation，
    // 再不行就是选中行文字色 × 0.62；preedit_background 透明；preedit_divider 不画。
    std::string candidateText;
    std::string preeditText;
    std::string preeditCaret;
    std::string selectedText;
    std::string selectedNumber;
    std::string selectedTranslation;
    std::string selectedBar;
    std::string preeditBackground;
    std::string preeditDivider;
    // 候选框右键菜单；留空的项沿用基础皮肤。
    std::string menuBackground;
    std::string menuBorder;
    std::string menuText;
    std::string menuHover;
};

// 悬浮工具栏的配色覆盖，D2D 与 WebView2 两个渲染器都读这一份。留空的项沿用基础皮肤。
struct ToolbarColors
{
    std::string background;
    std::string border;
    std::string handle;
    std::string divider;
    std::string icon;
    std::string hover;
};

struct Package
{
    std::string id;
    std::string name;
    std::string version;
    std::string author;
    std::string description;
    std::string base = "fluent";
    std::vector<std::string> layouts;
    std::vector<std::string> themes;
    double minWidthDip = 0.0;
    // 覆盖基础皮肤的候选框外框圆角；未设置时沿用基础皮肤。
    std::optional<double> cornerRadiusDip;
    // 候选框外框线宽（0–4）与候选项高亮圆角（0–16）；未设置时沿用基础皮肤。
    std::optional<double> borderWidthDip;
    std::optional<double> itemCornerRadiusDip;
    // 候选框卡片阴影：none / soft / strong；为空表示沿用基础皮肤。不影响右键菜单的阴影。
    std::string shadow;
    // 候选字体族，排在用户配置的字体之前，用户字体作为回退；为空表示不改。
    std::string fontFamily;
    // 候选框卡片内的背景图，绘制在底色之上、候选文字之下，按外框圆角裁剪。
    std::string backgroundImage;
    std::string backgroundFit = "cover"; // cover / contain / stretch
    double backgroundOpacity = 1.0;
    double decorationTopDip = 0.0;
    double decorationWidthDip = 0.0;
    // 卡片上方的装饰图；为空表示皮肤没有装饰。
    std::string decorationImage;
    std::string decorationAlign = "right"; // left / center / right，相对卡片
    CandidateColors dark;
    CandidateColors light;
    // 覆盖基础皮肤的悬浮工具栏外框圆角；未设置时沿用基础皮肤。
    std::optional<double> toolbarCornerRadiusDip;
    ToolbarColors toolbarDark;
    ToolbarColors toolbarLight;
};

struct Issue
{
    std::string folder;
    std::string reason;
};

struct ScanResult
{
    std::vector<Package> packages;
    std::vector<Issue> issues;
};

bool IsBuiltIn(const std::string &id);
bool IsSafeId(const std::string &id);
bool Supports(const Package &package, const std::string &layout, const std::string &theme);
std::optional<Package> Load(const std::filesystem::path &skinsRoot, const std::string &id,
                            std::string *error = nullptr);
ScanResult Scan(const std::filesystem::path &skinsRoot);
} // namespace CandidateSkinCatalog
