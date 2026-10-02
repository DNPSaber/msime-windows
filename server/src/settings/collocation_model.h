#pragma once

// 万象语法模型（octagram .gram）的按需下载与状态查询。
//
// 390MB 的模型不进安装包：设置页提供下载按钮，落到
// <DataDir>/models/wanxiang-lts-zh-hans/wanxiang-lts-zh-hans.gram。
//
// 完整性靠格式，不靠摘要。上游 LTS 会被原地重传（tag 不变、字节变）且没有不可变的
// 资产 URL，把摘要钉死就意味着每次重训都要改代码、发一个签名安装包，否则所有用户的
// 下载一起失败——这个成本消不掉。而重训只换权重，「Rime::Grammar/」魔数与双数组布局
// 不变，所以这里只要求字节能被引擎读取端打开（魔数 + 边界检查，
// engine/ngram/octagram/octagram_gram.cpp 的 GramDb::open）；读取端不按文件自带的长度
// 做堆分配，未知权重的字节是安全输入。传输完整性交给 HTTPS 加 Content-Length 比对。
//
// kReviewedSha256 仍然留着，但只作对照、不参与放行：摘要对不上说明上游重训过，状态
// 报 reviewed=false，设置页明示「上游已更新，未经评测」，照常落位。实际字节的摘要写进
// 旁挂的 .sha256 与 NOTICE.md，事后可追溯。
//
// 状态以磁盘为准（模型文件存在 = ready，落位发生在格式校验之后），下载态是各进程
// 内存里的 runtime 状态：独立设置进程与 Server 各自能看到自己的下载进度。

#include <string>

namespace collocation
{

// 内置模型包 id，也是下载器的落位目录名。解析侧在配置未指定 id 时回退到它，
// 所以这个键可以一直留空：模型下载到位后自然生效，不依赖谁去写回配置。
inline constexpr char kDefaultModelId[] = "wanxiang-lts-zh-hans";

struct ModelStatus
{
    std::string state;     // "absent" | "downloading" | "ready" | "error"
    int progress = 0;      // 下载中 0-100；其余状态无意义
    std::string error;     // state == "error" 时的人类可读原因
    bool reviewed = false; // ready 时：这份字节是否就是评测过的那份
};

// 磁盘现状 + 本进程下载态。轻量，可随配置快照频繁调用。
ModelStatus GetModelStatus();

// 幂等启动后台下载：已在下载或已就绪时返回 true 不重复启动；磁盘空间不足、
// 无法创建目录等同步失败返回 false 并把原因记入状态。完成/失败经状态查询可见。
bool StartDownload();

} // namespace collocation
