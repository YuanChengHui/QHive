# QHive

基于 **Qt6 / C++17** 的跨平台 HTTP/HTTPS 下载工具，支持多任务并发、多线程分片、断点续传、任务持久化、全局并发控制。

## ✨ 核心特性

- 🚀 **多任务并发** – 同时管理多个下载任务，互不阻塞。
- ⚡ **多线程分片** – 按文件大小自动切分 1~16 个分片，并行下载。
- 🔁 **断点续传** – 支持暂停/恢复，程序重启后从中断处继续。
- 🛡️ **智能降级** – 服务器不支持 `Accept-Ranges` 时自动切换单请求模式。
- 🔄 **失败重试** – 指数退避重试，最多 3 次。
- 💾 **任务持久化** – SQLite 存储任务状态与分片进度，重启自动恢复。
- 🎯 **全局并发控制** – QSemaphore 限制全局并发连接数，避免触发限流。
- 🧵 **统一网络线程池** – 所有网络 IO 走固定数量的 NetworkContext，避免线程爆炸。
- 📂 **网络/磁盘 IO 解耦** – 网络线程只收数据，写盘由独立 FileWriterPool 负责。
- 🎨 **现代化 GUI** – 任务列表支持多选、批量暂停/恢复/删除，实时显示速度与进度。

## 🛠️ 技术栈

| 模块 | 技术 |
| ---- | ---- |
| 语言 | C++17 |
| 框架 | Qt6 (Core, Gui, Widgets, Network, Concurrent, Sql) |
| 构建 | CMake 3.20+ |
| 数据库 | SQLite (Qt SQL) |
| 并发 | QThread + moveToThread + 事件驱动 + 全局线程池 |
| 协议 | HTTP/1.1 (HEAD, GET, Range) |


## 📂 目录结构

```
src/
├── core/          # HttpClient, DownloadTask, DownloadTypes
├── network/       # NetworkRequest, HeadRequest, DownloadRequest
│                  # NetworkContext, NetworkContextPool
├── utils/         # FileWriter, FileWriterPool, GlobalConcurrency
├── database/      # DatabaseManager, TaskRepository
└── ui/            # QHive, DownloadDialog, DownloadTaskItemWidget
```

## 🧩 核心设计

**NetworkContextPool（全局网络线程池）**
固定 4~8 个 `NetworkContext`，每个跑在独立线程，内部持有自己的 `QNetworkAccessManager` 与事件循环。所有网络请求（HEAD + 分片 GET）统一提交到池中轮询分配。
- 线程数与 QNAM 数从 `O(任务数 × 分片数)` 降到 `O(1)`
- 连接池复用率提升，keep-alive 命中率更高
- 通过 `QSemaphore` 全局限制并发连接数（默认 16）

**网络 IO 与磁盘 IO 解耦**
`DownloadRequest` 收到数据后通过信号发给 `FileWriterPool`，由独立的写入线程落盘。合并前用 `closeFileBlocking` 确保数据全部写入。

**DownloadTask（任务状态机）**
维护 pending / successful / failed 三个分片队列，负责调度、重试、暂停恢复、合并触发、数据库持久化。分片启动前用临时文件实际大小作为已下载量，避免缓存不一致。

**合并放子线程**
`mergeChunksAsync()` 用 `QtConcurrent::run` 执行合并，避免大文件阻塞 UI；完成后通过 `QMetaObject::invokeMethod` 回主线程更新 UI 与数据库。

## 📦 依赖

- Qt 6.x（Core, Gui, Widgets, Network, Concurrent, Sql）
- CMake 3.20+
- C++17 编译器（MSVC 2019+ / GCC 9+ / Clang 10+）

## 🔨 构建

```bash
# Windows (MSVC + Qt6)
cmake -B build -G "Visual Studio 17 2022" -DCMAKE_PREFIX_PATH="C:/Qt/6.8.3/msvc2022_64"
cmake --build build --config Release

# Linux / macOS
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

## 🗺️ 后续规划

- [ ] 全局限速（令牌桶）
- [ ] 文件完整性校验（SHA-256）
- [ ] 代理支持（HTTP / SOCKS5）
- [ ] 多源下载（镜像加速）
- [ ] 独立数据库线程

## 📜 协议

[MIT License](https://opensource.org/licenses/MIT)

## 👤 作者

**YuanChengHui** – [GitHub](https://github.com/YuanChengHui) | yuanchenghui_dev@163.com

*如果这个项目对你有帮助，欢迎 Star ⭐ 支持！*