#pragma once

// 万象语法模型（octagram .gram）的按需下载与状态查询。
//
// 390MB 的模型不进安装包：设置页提供下载按钮，落到
// <DataDir>/models/wanxiang-lts-zh-hans/wanxiang-lts-zh-hans.gram，
// SHA256 与字节数在本文件钉死。上游 LTS release 会被原地重传（tag 不变、字节变），
// 所以这里的摘要就是防漂移的锁；上游更新模型时必须显式改这里的常量并复跑
// docs/research 的离线评测，而不是让旧字节静默失效。
//
// 状态以磁盘为准（存在且字节数吻合 = ready），下载态是各进程内存里的
// runtime 状态：独立设置进程与 Server 各自能看到自己的下载进度。

#include <string>

namespace collocation
{

struct ModelStatus
{
    std::string state; // "absent" | "downloading" | "ready" | "error"
    int progress = 0;  // 下载中 0-100；其余状态无意义
    std::string error; // state == "error" 时的人类可读原因
};

// 磁盘现状 + 本进程下载态。轻量，可随配置快照频繁调用。
ModelStatus GetModelStatus();

// 幂等启动后台下载：已在下载或已就绪时返回 true 不重复启动；磁盘空间不足、
// 无法创建目录等同步失败返回 false 并把原因记入状态。完成/失败经状态查询可见。
bool StartDownload();

} // namespace collocation
