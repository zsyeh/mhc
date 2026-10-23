# Code review (baseline `df74dbb9de59e24a42809d752d40503b307c2b90`)

Scope: tracked `main.c`, `README.md`, ELF `mhc`; the worktree also contained a local `main.c` proxy bypass change and an untracked `mhc.new`. The source change was retained. The existing local binaries were not overwritten.

## Data flow and lifetime

Baseline: `main` initializes fixed buffers for URL and bearer header, dispatches commands, and calls a synchronous helper for `/configs` or `/proxies`. `test`/`testall` feed names from `/proxies` to a libcurl multi batch, then serialize results to `/tmp/mhc_rtt.cache`; `use` resolves a name using that cache and passes it to `apply_route`. The topology routine reads `/proxies`, scans every Selector for each BFS node, and issues PUTs while traversing. cJSON owns parsed response objects; request buffers and curl handles have separate lifetimes. Several baseline errors returned zero or negative errno values at the shell boundary.

Current implementation uses heap allocated URL/header and response buffers, a bounded response size, a graph snapshot with reverse Selector adjacency, a private per-user cache directory, and explicit cleanup at command exit. Every operation returns 0 for success or a positive nonzero code for failure.

## Findings

| Priority | Baseline location | Trigger and impact | Resolution |
| --- | --- | --- | --- |
| P0 | `main.c:191-324`, `apply_route` | A proxy with multiple Selector ancestors caused PUTs to every reachable Selector, including unrelated groups; a failed PUT still allowed success output/exit. This follows directly from the loop and status handling; no real-service exploit was attempted. | Default single unambiguous path; direct `--group` and explicit `--all-parents`; each PUT is read back, partial changes reported. |
| P0 | `main.c:506-790`, `do_mode`, `do_use`, `execute_telemetry_workflow`, `main` | Invalid mode, zero matches, API errors, or route failures could exit 0; negative errno values wrapped into shell statuses. | Validate mode, propagate positive nonzero status, send errors to stderr. |
| P0 | `main.c:32,123-130` | A nonempty literal default secret was compiled into publicly tracked source and ELF; fixed buffers silently truncated environment values. | Removed default secret, dynamic strings, input validation. The previously exposed token should be rotated by its owner; rewriting repository history requires a separate maintainer decision. |
| P1 | `main.c:437-495,681-722` | `/tmp` cache was opened with `w` before locking, permitting truncated reads and lost updates; all API instances shared it. | User private XDG cache, per-URL file, stable lock, atomic rename, fsync, TTL and separate states. |
| P1 | `main.c:349-435` | `curl_multi_add_handle` result and HTTP statuses were ignored; fixed 256 byte names and 2048 byte URLs truncated; partial success was hidden by unconditional SUCCESS. | Checked setup and HTTP status, dynamic names/URLs, bounded 20-handle batches and response size, partial results retained with nonzero exit. |
| P1 | `main.c:552-633` | Filtering by a short list of names included policy groups in node listing, testing and matching; local Mihomo returned provider nodes only from `/providers/proxies`. | Classification uses `type` and group `all` array; provider nodes are merged and tested through provider healthcheck. |
| P1 | `main.c:201,357,626` | `MATRIX_BOUNDS=256` silently stopped traversal or matching. | Dynamic graph and probe arrays with explicit 10,000 entity/1 MiB response limits. |
| P2 | `README.md:3-65,87-345` | README documented a Bash program, "Unbounded BFS", and a strict 3-second race deadline absent from C implementation. | Rewritten for the actual C CLI and its limits. |
| P2 | repository root | No repeatable build/test workflow; tracked ELF could embed a credential and become stale. | Makefile, Mock HTTP tests, CI; ELF removed from Git index but retained on disk. |

## API and remaining limits

The request methods and response fields were checked against the [official Mihomo API reference](https://wiki.metacubex.one/en/api/): `GET /configs`, `PATCH /configs`, `GET /proxies`, `GET /providers/proxies`, `PUT /proxies/{name}`, `GET /proxies/{name}/delay`, and `GET /providers/proxies/{provider}/{name}/healthcheck`. A 204 PUT response is followed by a `GET` of the group and a `now` check. The API offers no cross-group transaction, so rollback after a partial change is not attempted: another client might have changed a group in the meantime. `--all-parents` applies the BFS tree's chosen parent edge when paths reconverge; callers should inspect topology first. The 10,000 entity limit and linear name lookup mean graph construction can take quadratic time in a large configuration; reverse traversal itself uses the index. Cache partitions by URL, not by secret; separate cache homes are needed for credentials with different visibility at the same URL. Test coverage uses a mock server and a local Mihomo instance.

## Verification

`make test` and `make sanitize` run the Mock HTTP suite. Cases cover 401, 404, 500, refused connection, timeout, empty/malformed JSON, multi-level and multi-parent routes, cycles, URL escaped names, partial RTT and route failures, cache expiry/corruption/concurrent writers, noninteractive ambiguity, and more than 256 nodes.
