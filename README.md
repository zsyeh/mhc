# mhc

`mhc` 是一个使用 C、libcurl 和 cJSON 编写的 Mihomo REST API 命令行控制器。它可以查看和修改运行模式、列出实际代理节点、测量延迟，并通过 Selector 切换路由。

## 构建与安装

需要 Linux、C11 编译器、libcurl 开发头文件、cJSON 开发头文件、Python 3（测试用）。例如 Debian/Ubuntu 可安装 `build-essential libcurl4-openssl-dev libcjson-dev python3`。

```sh
make build
./build/mhc status
sudo make install
sudo make uninstall
make clean
```

`PREFIX` 默认为 `/usr/local`；安装支持 `DESTDIR`。仓库中的旧 ELF 文件不是构建目标，建议从 Git 跟踪中移除，后续通过 GitHub Release 发布可下载产物。

## 配置

- `MHC_API`：控制器地址，默认 `http://127.0.0.1:9090`，支持 HTTP 和 HTTPS。
- `MHC_SECRET`：Bearer token。未设置或为空时不发送 Authorization 头；没有内置凭据。
- `XDG_CACHE_HOME`：缓存目录的绝对路径，默认 `$HOME/.cache`。程序在其下创建私有 `mhc` 目录。

建议仅在本机回环地址开放未加密的 HTTP 控制器；远程使用时在 Mihomo 端配置 HTTPS 和认证。`MHC_SECRET` 不写入缓存，也不输出到终端。缓存按 API URL 分文件，但同一 URL 使用不同凭据时共享延迟数据；如需彻底隔离，请设置不同的 `XDG_CACHE_HOME`。

## 命令

```text
mhc status
mhc mode direct|rule|global
mhc nodes
mhc test <keyword>
mhc testall
mhc use <keyword> [--group NAME | --all-parents]
mhc clear
mhc restart
```

`nodes`、`test`、`testall` 和 `use` 从 `/proxies` 与 `/providers/proxies` 读取真实代理，不包括 Selector、URLTest、Fallback、LoadBalance 及系统内置代理。订阅节点的延迟使用 provider healthcheck 接口测试。`test` 和 `use` 的关键词不区分大小写；精确名称优先。多个匹配项有新鲜、唯一的最小延迟缓存时，`use` 采用该项；否则在终端列出选项，无交互终端返回错误。`testall` 中任一测试失败则退出码非零，但仍显示并缓存成功结果。例如：

```text
$ mhc status
Mode: rule
$ mhc testall
日本 空格/#: 42 ms
美 国: error
```

上述延迟为示例，实际输出取决于 Mihomo。`clear` 清除当前 API 实例缓存。`restart` 执行 `sudo systemctl restart clashtui_mihomo`，须自行配置相应服务和权限。

## 路由切换

默认 `use` 从目标节点沿反向 Selector 关系找到根。仅有唯一根和唯一可选路径时，从内层到外层逐组修改，并在每次 `PUT` 后通过 `GET` 检查 `now`。多父节点、路径汇合或循环会拒绝默认操作。`--group NAME` 只修改目标节点的一个直接 Selector 父组；`--all-parents` 显式修改反向搜索树上的所有 Selector 父组。批量操作可能改变其他流量的路由，请先检查拓扑。

一次切换涉及多次 REST 请求，Mihomo API 不提供跨组事务；若后续请求失败，已成功的组不会自动回滚，命令返回非零并报告部分成功。并发修改远端配置时，即使逐组读回成功，也无法保证切换后的整体状态保持不变。

## 错误与测试

成功退出码为 0；用法错误通常为 2；网络、JSON、缓存和远端错误为非零。错误写入 stderr。HTTP 401/403、连接失败、超时及其他状态码分别报告。延迟请求的单次超时为 4 秒，最多 20 个并发连接；总耗时随节点数量和服务器行为变化。

```sh
make test
make sanitize
```

测试使用本地 Mock HTTP 服务，不需要运行 Mihomo。缓存采用私有目录、稳定锁文件、临时文件与原子替换；TTL 为一小时。若缓存损坏，过期或不存在，多匹配节点需要交互选择或使用精确名称。

## 维护说明

Mihomo API 的当前字段和请求方法参见 [官方 API 文档](https://wiki.metacubex.one/en/api/)。当前仓库没有明确的开源许可证；维护者应在确认版权和分发意图后选择并添加许可证。
