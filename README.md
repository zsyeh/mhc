# mhc: High-Performance Topologically-Aware Mihomo CLI Controller

`mhc` is a lightweight, stateless, and topologically-aware Command Line Interface (CLI) controller designed specifically for the Mihomo (Clash Meta) asynchronous core backend. Built upon native POSIX shell scripting and optimized JSON-stream processing, `mhc` provides deterministic outbound routing manipulation, parallel round-trip time (RTT) race matrix testing, and automated multi-tier cascade override capabilities directly within the terminal context.

---

## 1. Design Philosophy & Architectural Comparison

Modern network engineering demands maximum throughput, determinism, and zero resource overhead from control-plane instrumentation. `mhc` was engineered to adhere strictly to the Unix Philosophy: *Write programs that do one thing and do it well; write programs to work together.*

### Comparative Analysis: `mhc` vs. Monolithic TUI Solutions

When evaluating `mhc` against existing terminal user interfaces (such as `clashtui`), profound structural and architectural divergences emerge:

| Dimension | `mhc` Architecture (CLI Engine) | Legacy TUI Solutions (`clashtui` Paradigm) |
| :--- | :--- | :--- |
| **State Paradigm** | **100% Stateless.** Acts purely as a deterministic API consumer over the `9090` control loop. | **State-coupled Monolith.** Forces synchronization between UI state and local configuration files. |
| **Separation of Concerns** | **Absolute.** Completely decouples user-space interface rendering from underlying kernel parameters. | **Violated.** Mandates validation of local ingress configurations (`mixed-port`, YAML structures) prior to API initialization. |
| **Fault Isolation** | **Graceful Fallback.** Intercepts runtime topology constraints and routes down to interactive submenus. | **Fragile Propagation.** Uncaught abstract syntax tree (AST) mismatches trigger immediate thread `panic!` crashes. |
| **Concurrency Model** | **Asynchronous Subshell Forking.** Non-blocking parallel execution limits latency to the physical timeout ceiling. | **Synchronous UI Event Loop.** Network I/O metrics easily block or introduce latency into the terminal input loop. |
| **Pipeline Integration** | **Native.** Fully compatible with standard streams (`stdout`, `stderr`) and filters (`grep`, `awk`). | **Monolithic Sandbox.** Encapsulates all output within a visual buffer matrix, breaking pipeline cascading. |

#### Architectural Critique of the TUI Paradigm
Legacy tools like `clashtui` introduce unnecessary architectural friction by forcing a stateful, file-dependent interface onto a fundamentally stateless, API-driven backend. By binding UI instantiation to local profile mutations and filesystem structures, these monolithic implementations suffer from poor robustness; any out-of-bounds config variation yields a catastrophic application panic. `mhc` eliminates this computational bloat by communicating directly with the memory-resident routing dictionary of Mihomo via highly optimized, atomized HTTP operations.

---

## 2. Core Features

* **Native JQ Match Engine**: Bypasses unstable regex stream parsing by executing deep JSON-array evaluation entirely inside native `jq` memory spaces, guaranteeing absolute consistency for multi-byte Unicode strings and Emoji sequences.
* **Parallel Latency Race Matrix**: Spawns concurrent background subshells for asynchronous RTT probing. Scales effortlessly across dozens of endpoints with an exact execution ceiling bounded by the $3000\text{ms}$ socket deadline.
* **State Caching Layer**: Implements a non-volatile local state cache (`/tmp/mhc_rtt.cache`) featuring an exact 3600-second TTL eviction policy to prevent API-slamming and mitigate upstream rate-limiting.
* **Recursive Cascade Routing Override**: Automatically analyzes the kernel's active proxy-group topology tree. When a leaf node is selected, `mhc` traverses backward to find all matching `Selector` layers and sequentially flashes upstream pointers, eradicating the typical HTTP 400 bad request state.

---

## 3. Installation & Dependencies

`mhc` runs natively on any POSIX-compliant environment (tested extensively on Ubuntu 26.04). 

### Prerequisites
Ensure the system has standard network utilities and the C-optimized JSON processor installed:
```bash
sudo apt-get update && sudo apt-get install curl jq grep coreutils -y

```

### Deployment

1. Move the execution script into your binary search path:
```bash
sudo nano /usr/local/bin/mhc

```


2. Paste the raw script code, save, and apply executable bit permissions:
```bash
sudo chmod +x /usr/local/bin/mhc

```



---

## 4. Command Reference

```
Mihomo CLI Controller (mhc)
Usage:
  mhc status                  # Dump current kernel mode and active outbound routing
  mhc mode <direct|rule|global> # Mutate global routing state machine
  mhc nodes                   # Parse and list all non-system physical outbounds
  mhc use "<keyword>"         # Route topology (Prefers local cache, fallbacks to menu)
  mhc test "<keyword>"        # Parallel RTT test for matched nodes and update cache
  mhc testall                 # Full scale parallel RTT race matrix (High latency cost)
  mhc clear                   # Purge local latency cache file
  mhc restart                 # Force restart Mihomo systemd daemon service

```

### Examples of Operational Workflows

#### Inspect Active State

```bash
$ mhc status
Current Kernel Mode: global
GLOBAL Outbound Pointer: 🕹️ 手动降级指定

```

#### Low-Latency Automated Routing Switching

Search for optimal endpoints under the "Singapore" regional cluster. If cached data exists within the 1-hour window, the routing is modified instantly:

```bash
$ mhc use Singapore
[WARN] Matched 3 nodes. Verifying local state cache...
[SUCCESS] Cache Hit (Optimal): 🇸🇬 新加坡 S02 | IEPL | x2 (195ms)
[SUCCESS] L1 Routing Tree: [🕹️ 手动降级指定] -> [🇸🇬 新加坡 S02 | IEPL | x2]
[LINK] L2 Cascade Layer: [🚀 节点选择] -> [🕹️ 手动降级指定]
[GLOBAL] Core Gateway Direct: [🇸🇬 新加坡 S02 | IEPL | x2]

```

#### Cache Miss Fallback Behavior

If the cache is stale or uninitialized, `mhc` gracefully triggers the interactive menu layer instead of interrupting execution:

```bash
$ mhc use US
[WARN] Matched 2 nodes. Verifying local state cache...
[WARN] Cache Miss or Expired (>1 Hour). Manual intervention required.
------------------------------------------------------------
1) 🇺🇸 美国 S01 | IEPL | x1.5
2) 🇺🇸 美国 S02 | IEPL | x1.5
Select node index to route (Ctrl+C to abort): 2

You selected: 🇺🇸 美国 S02 | IEPL | x1.5
[SUCCESS] L1 Routing Tree: [🕹️ 手动降级指定] -> [🇺🇸 美国 S02 | IEPL | x1.5]
...

```

#### Trigger Asynchronous Network Probing Matrix

```bash
$ mhc test Japan
[INFO] Triggering parallel RTT race matrix (Targets: 18)...
--- Latency Matrix Results ---
  - 🇯🇵 日本 S02 | IEPL: 42ms
  - 🇯🇵 日本 S01 | IEPL: 45ms
  - 🇯🇵 日本 标准节点: 124ms
  - 🇯🇵 免费-日本1-Ver.7: Timeout
[SUCCESS] RTT metrics committed to local state cache (TTL: 1 Hour).

```

---

## 5. Troubleshooting & Exit Status

`mhc` returns standard POSIX exit codes to communicate run-time status back to the executing shell pipeline:

* **`0`**: Successful execution; target mutated or telemetry successfully retrieved.
* **`1`**: General operational error (e.g., API unreachable, invalid command syntax, keyword returned zero matching entities, or structural topology constraints blocked the execution).

```


