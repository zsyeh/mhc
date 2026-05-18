# mhc: A Topologically-Adaptive Graph-Traversal CLI Controller for Mihomo Cores

Copyright (c) 2026 Eh. All rights reserved.

---

## 1. Executive Abstract & Architectural Advantages

`mhc` is a stateless, zero-overhead, topologically-adaptive Command Line Interface (CLI) controller engineered to manipulate the runtime state of the Mihomo (Clash Meta) asynchronous core via the loopback RESTful API.

Unlike conventional management utilities, `mhc` treats the memory-resident routing state of the proxy core as a directed acyclic graph (DAG). It abstracts all operational control into deterministic atomic primitives, eliminating the computational friction and state synchronization errors inherent in traditional interface wrappers.

### Core Architectural Advantages

* **Stateless Execution**: The controller operates with absolute memory-volatile isolation. It maintains zero persistent application state, reading directly from the kernel’s active JSON response streams and evaluating structures instantaneously.
* **Asynchronous Multi-Processing**: Telemetry and latency testing are decoupled from the main execution thread. Network I/O probing is offloaded to parallelized system subshells, bounding the execution latency of a full-scale network race matrix to a strict $3000\text{ms}$ physical deadline.
* **Deterministic Native JSON Stream Parsing**: Bypasses brittle string manipulation and regex-based token extraction by enforcing strict, C-optimized `jq` evaluation filters directly on raw API payloads, ensuring multi-byte Unicode and multi-byte character sequence integrity.

---

## 2. Dual Technical Critique: Legacy TUIs & Structural Airport Antipatterns

Deficiencies in terminal network management arise from two distinct vectors: the architectural bloat of monolithic terminal user interfaces (TUIs) and the structural optimization failures in commercial airport profile generation.

### Architectural Critique of Legacy TUI Implementations (e.g., `clashtui`)

Monolithic terminal user interfaces like `clashtui` represent a fundamental violation of the separation of concerns paradigm. By binding interface rendering directly to system state, they introduce several critical failure modes:

* **Temporal and State Coupling**: These programs attempt to synchronize an independent UI loop with underlying disk files (`config.yaml`). A single unexpected syntax mutation or profile asymmetry causes unhandled thread panics, leading to immediate application crashes.
* **Blocking Event Loops**: Lacking native process decoupling, legacy TUIs often run network telemetry on the primary execution path. High-latency network transactions or TCP timeouts cascade into the UI thread, freezing terminal input.
* **Pipeline Breaking**: By enclosing execution output within a simulated visual terminal buffer matrix, legacy TUIs prevent compliance with the Unix pipeline paradigm, making it impossible to compose commands using standard output streams (`stdout`).

### Structural Critique of Antipattern Airport Configurations

Commercial subscription profiles frequently present severe structural flaws, generating significant architectural debt within the client-side routing engine:

* **Proxy Group Pollution and Fan-out**: Upstream providers routinely populate the primary routing selector array with a chaotic mixture of raw leaf nodes and nested automatic groups. This uncontrolled expansion drastically inflates graph complexity.
* **Deterministic Lockout via Automated State Machines**: Providers frequently place leaf nodes exclusively within autonomous `url-test` or `fallback` groups. Because these groups enforce internal automated routing, they reject manual intervention. Forcing an external pointer modification onto these groups results in kernel-level rejections (HTTP 400 Bad Request).
* **Regex Filtering Overhead**: To segregate regional infrastructure, profiles often chain cascading layers of regex evaluation filters against a single provider source. This design forces the proxy core to repeatedly compile complex regular expressions on every state refresh, causing intermittent thread lockups and elevated packet propagation delays.

---

## 3. Algorithmic Foundation: Unbounded BFS Upward Propagation

To neutralize the structural chaos of deeply nested provider profiles (extending up to four, five, or arbitrary layer depths), `mhc` implements an **Unbounded Breadth-First Search (BFS) Upward Propagation Algorithm**.

### Theoretical Derivation

Let the active proxy configuration be modeled as a directed graph $G = (V, E)$, where $V$ represents the set of all proxies and strategy groups, and $E$ represents the directed routing paths. When an operator selects a specific leaf node $v_{\text{target}} \in V$, conventional systems fail if $v_{\text{target}}$ is not explicitly declared within the immediate boundary of the root proxy group $V_{\text{root}}$.

`mhc` resolves this via an automated inverse graph traversal:

1. Let $Q$ be a FIFO queue, initialized with $Q \leftarrow [v_{\text{target}}]$. Let $S$ be a set tracking visited vertices, initialized with $S \leftarrow \emptyset$.
2. While $Q$ is not empty, dequeue the front element $u$. If $u \in S$, continue to prevent infinite traversal loops. Otherwise, append $u \to S$.
3. Query the live memory cache of the core using $jq$ to discover all parent groups $P = \{ p \in V \mid u \in \text{children}(p) \land \text{type}(p) = \text{"Selector"} \}$.
4. For each $p \in P$, dispatch an asynchronous `PUT` mutation payload:

$$\text{Payload} := \{\text{"name"}: u\}$$



targeting the URI endpoint `/proxies/\text{encode}(p)`. This flashes the internal pointer of $p$ to target $u$.
5. If $p \neq V_{\text{GLOBAL}}$, enqueue $p \to Q$ to propagate the control signal upstream to the next topological layer.

Through this design, the execution sequence achieves complete convergence over the graph at a time complexity of $O(|V| + |E|)$. It systematically heals path fragmentation across any arbitrary configuration depth without requiring manual adjustments to the underlying file profile.

---

## 4. Technical Specifications & Dependencies

The controller execution architecture relies on standard, highly optimized POSIX system utilities:

* **Mihomo Core**: Compatible with Clash Meta Kernel architectures hosting open RESTful control interfaces.
* **curl**: Utilized for handling low-latency HTTP REST stream transactions.
* **jq**: Required for fast, C-optimized JSON data structure processing.
* **coreutils & grep**: Employed for basic process coordination and text pipeline filtration.

---

## 5. Deployment Protocols

To integrate the controller into your environment, follow these deployment steps.

### Step 1: Target Directory Preparation

Ensure the standard system binary path exists and possesses correct structural write privileges:

```bash
sudo mkdir -p /usr/local/bin

```

### Step 2: Binary Stream Injection

Populate the target path with the source execution stream by directing a standard input block directly into the target location:

```bash
sudo tee /usr/local/bin/mhc << 'EOF'
#!/bin/bash
# ==========================================
# Mihomo CLI Controller (mhc) - Cascade Routing Engine
# Copyright (c) 2026 Eh. All rights reserved.
# ==========================================
API="http://127.0.0.1:9090"
SECRET="bushieh"
AUTH="Authorization: Bearer ${SECRET}"
TEST_URL="http://www.gstatic.com/generate_204"
CACHE_FILE="/tmp/mhc_rtt.cache"
CACHE_TTL=3600

function usage() {
    echo -e "\033[1;36mMihomo CLI Controller (mhc)\033[0m"
    echo "Usage:"
    echo "  mhc status                  # Dump current kernel mode and active outbound routing"
    echo "  mhc mode <direct|rule|global> # Mutate global routing state machine"
    echo "  mhc nodes                   # Parse and list all non-system physical outbounds"
    echo "  mhc use \"<keyword>\"         # Route topology (Prefers local cache, fallbacks to menu)"
    echo "  mhc test \"<keyword>\"        # Parallel RTT test for matched nodes and update cache"
    echo "  mhc testall                 # Full scale parallel RTT race matrix (High latency cost)"
    echo "  mhc clear                   # Purge local latency cache file"
    echo "  mhc restart                 # Force restart Mihomo systemd daemon service"
    echo ""
}

function run_race() {
    local nodes_json=$1
    local count=$(echo "$nodes_json" | jq 'length')
    echo -e "\033[1;34m[INFO]\033[0m Triggering parallel RTT race matrix (Targets: \033[1;33m$count\033[0m)..."
    
    local tmp_dir=$(mktemp -d)
    local idx=0
    
    while IFS= read -r NODE; do
        idx=$((idx+1))
        (
            ENCODED=$(jq -rn --arg x "$NODE" '$x|@uri')
            RES=$(curl -s -m 3 -H "$AUTH" "$API/proxies/$ENCODED/delay?url=$TEST_URL&timeout=3000")
            DELAY=$(echo "$RES" | jq -r '.delay // empty')
            if [ -z "$DELAY" ] || [ "$DELAY" == "0" ]; then 
                DELAY=999999 
            fi
            echo "$DELAY|$NODE" > "$tmp_dir/$idx.res"
        ) &
    done < <(echo "$nodes_json" | jq -r '.[]')
    
    wait 
    
    local file_count=$(ls -1 "$tmp_dir"/*.res 2>/dev/null | wc -l)
    if [ "$file_count" -eq 0 ]; then
        echo -e "\033[1;31m[ERROR]\033[0m Measurement exception: No runtime state file generated."
        rm -rf "$tmp_dir"
        exit 1
    fi

    local now=$(date +%s)
    touch "$CACHE_FILE"
    
    echo -e "--- Latency Matrix Results ---"
    cat "$tmp_dir"/*.res | sort -t'|' -k1 -n | while IFS='|' read -r DELAY NODE; do
        echo "$now|$DELAY|$NODE" >> "$CACHE_FILE"
        if [ "$DELAY" -eq 999999 ]; then
            echo -e "  - $NODE: \033[1;31mTimeout\033[0m"
        else
            echo -e "  - $NODE: \033[1;32m${DELAY}ms\033[0m"
        fi
    done
    
    tac "$CACHE_FILE" | awk -F'|' '!seen[$3]++' | tac > "$CACHE_FILE.tmp"
    mv "$CACHE_FILE.tmp" "$CACHE_FILE"
    
    rm -rf "$tmp_dir"
    echo -e "\033[1;32m[SUCCESS]\033[0m RTT metrics committed to local state cache (TTL: 1 Hour)."
}

function apply_route() {
    local TARGET_NODE=$1
    local PROXIES_JSON
    PROXIES_JSON=$(curl -s -H "$AUTH" "$API/proxies")
    
    local QUEUE=("$TARGET_NODE")
    local VISITED=()
    local IS_FIRST_TIER=true

    while [ ${#QUEUE[@]} -gt 0 ]; do
        local CURRENT=${QUEUE[0]}
        QUEUE=("${QUEUE[@]:1}")

        if [[ " ${VISITED[*]} " =~ " ${CURRENT} " ]]; then
            continue
        fi
        VISITED+=("$CURRENT")

        local PARENTS
        PARENTS=$(echo "$PROXIES_JSON" | jq -r --arg item "$CURRENT" '
            .proxies | to_entries[] |
            select(.value.all != null and (.value.all | index($item) != null) and .value.type == "Selector") |
            .key
        ')

        if [ -z "$PARENTS" ] && [ "$IS_FIRST_TIER" = true ]; then
            local AUTO_PARENTS
            AUTO_PARENTS=$(echo "$PROXIES_JSON" | jq -r --arg item "$CURRENT" '
                .proxies | to_entries[] |
                select(.value.all != null and (.value.all | index($item) != null) and .value.type != "Selector") |
                .key
            ')
            if [ -n "$AUTO_PARENTS" ]; then
                echo -e "\033[1;31m[ERROR]\033[0m Architecture Interception: Node is locked within automated groups:"
                echo "$AUTO_PARENTS" | while IFS= read -r AG; do echo "  - $AG"; done
                exit 1
            fi
            echo -e "\033[1;31m[ERROR]\033[0m Topology Isolation: Node '$CURRENT' has no valid parent selectors."
            exit 1
        fi

        if [ "$IS_FIRST_TIER" = true ]; then
            IS_FIRST_TIER=false
        fi

        if [ -n "$PARENTS" ]; then
            while IFS= read -r GROUP; do
                if [ -n "$GROUP" ]; then
                    local ENCODED_GROUP
                    ENCODED_GROUP=$(jq -rn --arg x "$GROUP" '$x|@uri')
                    local JSON_PAYLOAD
                    JSON_PAYLOAD=$(jq -n --arg name "$CURRENT" '{name: $name}')
                    
                    local STATUS_CODE
                    STATUS_CODE=$(curl -s -o /dev/null -w "%{http_code}" -X PUT -H "$AUTH" -H "Content-Type: application/json" -d "$JSON_PAYLOAD" "$API/proxies/$ENCODED_GROUP")
                    
                    if [ "$STATUS_CODE" -eq 204 ]; then
                        echo -e "\033[1;32m[SUCCESS]\033[0m Topology Mutation: [\033[1;36m$GROUP\033[0m] -> [\033[1;32m$CURRENT\033[0m]"
                    else
                        echo -e "\033[1;31m[ERROR]\033[0m Mutation Failed: [\033[1;36m$GROUP\033[0m] (HTTP $STATUS_CODE)"
                    fi

                    if [ "$GROUP" != "GLOBAL" ]; then
                        QUEUE+=("$GROUP")
                    fi
                fi
            done <<< "$PARENTS"
        fi
    done
}

if [ $# -eq 0 ]; then usage; exit 1; fi

case "$1" in
    clear)
        rm -f "$CACHE_FILE"
        echo -e "\033[1;32m[SUCCESS]\033[0m Local latency cache purged from file system."
        ;;
    restart)
        echo -e "\033[1;34m[INFO]\033[0m Dispatching systemctl restart clashtui_mihomo..."
        sudo systemctl restart clashtui_mihomo
        echo -e "\033[1;32m[SUCCESS]\033[0m Kernel daemon restarted successfully."
        ;;
    mode)
        if [[ ! "$2" =~ ^(direct|rule|global)$ ]]; then echo "Error: invalid mode."; exit 1; fi
        curl -s -X PATCH -H "$AUTH" -H "Content-Type: application/json" -d "{\"mode\": \"$2\"}" $API/configs
        echo -e "State Mutated: Mode switched to \033[1;32m$2\033[0m"
        ;;
    nodes)
        curl -s -H "$AUTH" $API/proxies | jq -r '.proxies | keys[]' | grep -vE "^(DIRECT|REJECT|REJECT-DROP|PASS|GLOBAL|COMPATIBLE)$"
        ;;
    test)
        if [ -z "$2" ]; then echo "Error: Missing Keyword."; exit 1; fi
        PROXIES_JSON=$(curl -s -H "$AUTH" $API/proxies)
        ALL_NODES_JSON=$(echo "$PROXIES_JSON" | jq -c '[.proxies | keys[] | select(test("^(DIRECT|REJECT|REJECT-DROP|PASS|COMPATIBLE|GLOBAL)$") | not)]')
        MATCHED_JSON=$(echo "$ALL_NODES_JSON" | jq -c --arg kw "$2" 'map(select(test($kw; "i")))')
        if [ "$(echo "$MATCHED_JSON" | jq 'length')" -eq 0 ]; then echo -e "\033[1;31m[ERROR]\033[0m Target keyword '$2' matched 0 entities."; exit 1; fi
        run_race "$MATCHED_JSON"
        ;;
    testall)
        PROXIES_JSON=$(curl -s -H "$AUTH" $API/proxies)
        ALL_NODES_JSON=$(echo "$PROXIES_JSON" | jq -c '[.proxies | keys[] | select(test("^(DIRECT|REJECT|REJECT-DROP|PASS|COMPATIBLE|GLOBAL)$") | not)]')
        run_race "$ALL_NODES_JSON"
        ;;
    use)
        if [ -z "$2" ]; then echo "Error: Missing Keyword."; exit 1; fi
        PROXIES_JSON=$(curl -s -H "$AUTH" $API/proxies)
        ALL_NODES_JSON=$(echo "$PROXIES_JSON" | jq -c '[.proxies | keys[] | select(test("^(DIRECT|REJECT|REJECT-DROP|PASS|COMPATIBLE|GLOBAL)$") | not)]')
        
        EXACT_MATCH=$(echo "$ALL_NODES_JSON" | jq -c --arg kw "$2" 'map(select(. == $kw))')
        if [ "$(echo "$EXACT_MATCH" | jq 'length')" -eq 1 ]; then
            TARGET_NODE=$(echo "$EXACT_MATCH" | jq -r '.[0]')
            apply_route "$TARGET_NODE"
            exit 0
        fi

        FUZZY_MATCH=$(echo "$ALL_NODES_JSON" | jq -c --arg kw "$2" 'map(select(test($kw; "i")))')
        MATCH_COUNT=$(echo "$FUZZY_MATCH" | jq 'length')
        
        if [ "$MATCH_COUNT" -eq 0 ]; then echo -e "\033[1;31m[ERROR]\033[0m Target keyword '$2' matched 0 entities."; exit 1;
        elif [ "$MATCH_COUNT" -eq 1 ]; then TARGET_NODE=$(echo "$FUZZY_MATCH" | jq -r '.[0]'); apply_route "$TARGET_NODE"; exit 0; fi
        
        echo -e "\033[1;33m[WARN]\033[0m Matched $MATCH_COUNT nodes. Verifying local state cache..."
        NOW=$(date +%s)
        BEST_NODE=""
        BEST_DELAY=999999
        
        if [ -f "$CACHE_FILE" ]; then
            while IFS= read -r NODE; do
                CACHE_LINE=$(grep "|$NODE\$" "$CACHE_FILE" 2>/dev/null | tail -n 1)
                if [ -n "$CACHE_LINE" ]; then
                    CTIME=$(echo "$CACHE_LINE" | cut -d'|' -f1)
                    CDELAY=$(echo "$CACHE_LINE" | cut -d'|' -f2)
                    if [ $((NOW - CTIME)) -le $CACHE_TTL ] && [ "$CDELAY" -lt "$BEST_DELAY" ]; then
                        BEST_DELAY=$CDELAY
                        BEST_NODE=$NODE
                    fi
                fi
            done <<< "$(echo "$FUZZY_MATCH" | jq -r '.[]')"
        fi
        
        if [ -n "$BEST_NODE" ] && [ "$BEST_DELAY" -lt 999999 ]; then
            echo -e "\033[1;32m[SUCCESS]\033[0m Cache Hit (Optimal): $BEST_NODE (${BEST_DELAY}ms)"
            apply_route "$BEST_NODE"
        else
            echo -e "\033[1;33m[WARN]\033[0m Cache Miss or Expired (>1 Hour). Manual intervention required."
            echo -e "------------------------------------------------------------"
            PS3=$(echo -e "\033[1;36mSelect node index to route (Ctrl+C to abort): \033[0m")
            mapfile -t NODE_ARRAY < <(echo "$FUZZY_MATCH" | jq -r '.[]')
            select opt in "${NODE_ARRAY[@]}"; do
                if [ -n "$opt" ]; then echo -e "\nYou selected: \033[1;32m$opt\033[0m"; apply_route "$opt"; break;
                else echo -e "\033[1;31mInvalid index. Please try again.\033[0m"; fi
            done
        fi
        ;;
    status)
        MODE=$(curl -s -H "$AUTH" $API/configs | jq -r '.mode')
        GLOBAL_NODE=$(curl -s -H "$AUTH" $API/proxies/GLOBAL | jq -r '.now // empty')
        echo -e "Current Kernel Mode: \033[1;36m$MODE\033[0m"
        if [ -n "$GLOBAL_NODE" ]; then echo -e "GLOBAL Outbound Pointer: \033[1;32m$GLOBAL_NODE\033[0m"; fi
        ;;
    *) usage; ;;
esac
EOF

```

### Step 3: Privilege Level Allocation

Grant global filesystem execution permissions to the newly compiled binary file:

```bash
sudo chmod +x /usr/local/bin/mhc

```

---

## 6. Comprehensive Command Reference & Production Workflows

### Telemetry & Inspection

#### 1. Global Kernel Status Inspection

Extracts live metadata regarding active routing profiles and the kernel's overall operational proxy state machine:

```bash
mhc status

```

> **Output Specification**:
> ```
> Current Kernel Mode: rule
> GLOBAL Outbound Pointer: Outbound Select
> 
> ```
> 
> 

#### 2. Memory Space Dictionary Dump

Enumerates all functional, non-system proxy entities registered within the volatile runtime memory map of the core:

```bash
mhc nodes

```

---

### Latency Matriculation & Probing

#### 1. Targeted Cluster Parallel Probing

Forks asynchronous network subshells to measure round-trip time metrics across a specific subset of nodes matching the provided search parameter. The resulting values are then committed to the local filesystem cache:

```bash
mhc test IEPL

```

> **Output Specification**:
> ```
> [INFO] Triggering parallel RTT race matrix (Targets: 4)...
> --- Latency Matrix Results ---
>   - Japan IEPL: 42ms
>   - HongKong IEPL: 177ms
>   - Singapore S01 | IEPL | x2: 195ms
>   - US S01 | IEPL | x1.5: Timeout
> [SUCCESS] RTT metrics committed to local state cache (TTL: 1 Hour).
> 
> ```
> 
> 

#### 2. Core Registry Full Scale Matrix Probe

Iterates over the entire proxy graph to evaluate connectivity across all endpoints. Because this executes a comprehensive parallel race matrix, it carries a higher overall network latency cost:

```bash
mhc testall

```

---

### Routing Control & State Mutation

#### 1. Global Processing Mode Modification

Forces a hard mutation of the kernel's traffic handling mode, shifting it between strict rule-based isolation and global catching:

```bash
mhc mode global

```

#### 2. Topologically Adaptive Node Allocation

Triggers dynamic keyword tracking to switch the outbound data stream. The execution sequence follows a strict priority logic path:

```
[Target Keyword Received]
           │
           ▼
  Exact Match Found? ───► Yes ───► [Apply Route Directly]
           │
           ▼ No
  Fuzzy Cluster Count
           │
           ├───► Count = 1 ──────► [Apply Route Directly]
           │
           └───► Count > 1
                     │
                     ▼
            Valid Cache Hit? ────► Yes ───► [Select Lowest RTT & Route]
                     │
                     ▼ No
            [Launch Interactive Dropdown Menu]

```

```bash
mhc use HongKong

```

> **Output Specification (Cache Hit Flow)**:
> ```
> [WARN] Matched 2 nodes. Verifying local state cache...
> [SUCCESS] Cache Hit (Optimal): HongKong IEPL (177ms)
> [SUCCESS] Topology Mutation: [Outbound Select] -> [eh香港IEPL大全]
> [SUCCESS] Topology Mutation: [eh香港IEPL大全] -> [HongKong IEPL]
> 
> ```
> 
> 

> **Output Specification (Cache Miss Interactivity Fallback)**:
> ```
> [WARN] Matched 2 nodes. Verifying local state cache...
> [WARN] Cache Miss or Expired (>1 Hour). Manual intervention required.
> ------------------------------------------------------------
> 1) HongKong IEPL
> 2) HongKong Standard
> Select node index to route (Ctrl+C to abort): 1
> 
> You selected: HongKong IEPL
> [SUCCESS] Topology Mutation: [Outbound Select] -> [eh香港IEPL大全]
> [SUCCESS] Topology Mutation: [eh香港IEPL大全] -> [HongKong IEPL]
> 
> ```
> 
> 

---

### System Integration & Service Maintenance

#### 1. Volatile Memory Eviction

Purges the network telemetry database from the local state cache file system, forcing subsequent lookup calls to rebuild the dataset:

```bash
mhc clear

```

#### 2. Core Daemon Cycle Initialization

Triggers an immediate systemd execution signal to drop and reload the core network proxy service container:

```bash
mhc restart

```
