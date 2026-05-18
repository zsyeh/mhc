/*******************************************************************************
 * Mihomo CLI Controller (mhc) - Defensively Hardened & Optimized Version
 * Copyright (c) 2026 Eh. All rights reserved.
 *
 * Infrastructure: Static Branch Prediction, Strict Boundary Enforcement,
 * Type-Safe Multicurl Engine & Level 3 Pipeline Optimization.
 * Dependency: libcurl, cJSON
 * Compile: gcc -O3 main.c -lcurl -lcjson -o mhc
 ******************************************************************************/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>
#include <errno.h>

/* --- Linux Kernel Style Compiler Optimization Macros --- */
#define likely(x)      __builtin_expect(!!(x), 1)
#define unlikely(x)    __builtin_expect(!!(x), 0)

/* --- Architectural Configuration Constants --- */
#define DEFAULT_API "http://127.0.0.1:9090"
#define DEFAULT_SECRET "bushieh"
#define TEST_URL "http://www.gstatic.com/generate_204"
#define CACHE_FILE "/tmp/mhc_rtt.cache"
#define CACHE_TTL 3600
#define MAX_CONCURRENT_PROBES 20  
#define TIMEOUT_MS 3000

/* --- Sentinel Constants to Replace Magic Numbers --- */
#define MHC_RTT_TIMEOUT 999999L
#define MATRIX_BOUNDS   256

/* --- Global Context Structure --- */
typedef struct {
    char api_url[256];
    char auth_header[512];
    char secret[128];
} MhcContext;

/* --- Forward Declarations of Uniform Command Interface --- */
static int do_status(MhcContext *ctx, int argc, char **argv);
static int do_mode(MhcContext *ctx, int argc, char **argv);
static int do_nodes(MhcContext *ctx, int argc, char **argv);
static int do_test(MhcContext *ctx, int argc, char **argv);
static int do_testall(MhcContext *ctx, int argc, char **argv);
static int do_use(MhcContext *ctx, int argc, char **argv);
static int do_clear(MhcContext *ctx, int argc, char **argv);
static int do_restart(MhcContext *ctx, int argc, char **argv);

/* --- Function Pointer Command Lookup Table (Kernel Matrix) --- */
typedef int (*CmdHandler)(MhcContext *ctx, int argc, char **argv);

typedef struct {
    const char *cmd_name;
    CmdHandler handler;
    const char *help_meta;
} CmdMapping;

static const CmdMapping cmd_table[] = {
    {"status",  do_status,  "Dump current kernel mode and active outbound routing"},
    {"mode",    do_mode,    "Mutate global routing state machine <direct|rule|global>"},
    {"nodes",   do_nodes,   "Parse and list all non-system physical outbounds"},
    {"test",    do_test,    "Parallel RTT test for matched nodes via keyword constraints"},
    {"testall", do_testall, "Full scale parallel RTT race matrix"},
    {"use",     do_use,     "Route topology matching keyword dynamically"},
    {"clear",   do_clear,   "Purge local latency cache file from file system"},
    {"restart", do_restart, "Force restart Mihomo systemd daemon service"}
};

#define CMD_TABLE_SIZE (sizeof(cmd_table) / sizeof(CmdMapping))

/* --- HTTP Storage Structure & Network Callbacks --- */
typedef struct {
    char *payload;
    size_t size;
} HttpBuffer;

typedef struct {
    char node_name[256];
    long delay;
} ProbeResult;

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    HttpBuffer *mem = (HttpBuffer *)userp;
    char *ptr = realloc(mem->payload, mem->size + realsize + 1);
    
    if (unlikely(!ptr)) return 0; 
    
    mem->payload = ptr;
    memcpy(&(mem->payload[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->payload[mem->size] = 0;
    return realsize;
}

static void init_context(MhcContext *ctx) {
    const char *env_api = getenv("MHC_API");
    const char *env_secret = getenv("MHC_SECRET");
    snprintf(ctx->api_url, sizeof(ctx->api_url), "%s", env_api ? env_api : DEFAULT_API);
    snprintf(ctx->secret, sizeof(ctx->secret), "%s", env_secret ? env_secret : DEFAULT_SECRET);
    snprintf(ctx->auth_header, sizeof(ctx->auth_header), "Authorization: Bearer %s", ctx->secret);
}

static char* perform_http_request(MhcContext *ctx, const char *endpoint, const char *method, const char *json_payload, long *out_status) {
    CURL *curl = curl_easy_init();
    if (unlikely(!curl)) return NULL;

    char url[512];
    snprintf(url, sizeof(url), "%s%s", ctx->api_url, endpoint);
    HttpBuffer response = { .payload = malloc(1), .size = 0 };

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, ctx->auth_header);
    if (json_payload) {
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_payload);
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 5000L);

    CURLcode res = curl_easy_perform(curl);
    if (likely(out_status)) curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, out_status);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (unlikely(res != CURLE_OK)) { free(response.payload); return NULL; }
    return response.payload;
}

static int is_visited(char **set, size_t count, const char *target) {
    for (size_t i = 0; i < count; i++) {
        if (strcmp(set[i], target) == 0) return 1;
    }
    return 0;
}

/* --- Unbounded BFS Upward Propagation Engine --- */
static void apply_route(MhcContext *ctx, const char *target_node) {
    long status = 0;
    char *raw_proxies = NULL;
    cJSON *root_json = NULL;
    char *queue[MATRIX_BOUNDS] = {0};
    size_t q_head = 0, q_tail = 0;
    char *visited[MATRIX_BOUNDS] = {0};
    size_t visited_count = 0;

    raw_proxies = perform_http_request(ctx, "/proxies", "GET", NULL, &status);
    if (unlikely(!raw_proxies || status != 200)) {
        fprintf(stderr, "\033[1;31m[ERROR]\033[0m Failed to fetch memory kernel routing graph.\n");
        goto out_cleanup;
    }

    root_json = cJSON_Parse(raw_proxies);
    if (unlikely(!root_json)) goto out_cleanup;

    cJSON *proxies = cJSON_GetObjectItemCaseSensitive(root_json, "proxies");
    if (unlikely(!proxies)) goto out_cleanup;

    queue[q_tail++] = strdup(target_node);
    int is_first_tier = 1;

    while (likely(q_head < q_tail)) {
        char *current = queue[q_head++];
        if (unlikely(is_visited(visited, visited_count, current))) {
            free(current);
            continue;
        }
        
        if (unlikely(visited_count >= MATRIX_BOUNDS)) {
            fprintf(stderr, "\033[1;31m[CRITICAL]\033[0m Visited nodes tracker overflow intercepted.\n");
            free(current);
            break;
        }
        visited[visited_count++] = current;

        cJSON *proxy_item = NULL;
        int parent_found = 0;

        cJSON_ArrayForEach(proxy_item, proxies) {
            cJSON *type_obj = cJSON_GetObjectItemCaseSensitive(proxy_item, "type");
            cJSON *all_obj = cJSON_GetObjectItemCaseSensitive(proxy_item, "all");
            if (!type_obj || !all_obj || strcmp(type_obj->valuestring, "Selector") != 0) continue;

            cJSON *child = NULL;
            int is_child = 0;
            cJSON_ArrayForEach(child, all_obj) {
                if (strcmp(child->valuestring, current) == 0) { is_child = 1; break; }
            }

            if (likely(is_child)) {
                parent_found = 1;
                char *group_name = proxy_item->string;
                char *escaped_group = curl_easy_escape(NULL, group_name, 0);
                char endpoint[512];
                snprintf(endpoint, sizeof(endpoint), "/proxies/%s", escaped_group);
                
                cJSON *mutation_payload = cJSON_CreateObject();
                cJSON_AddStringToObject(mutation_payload, "name", current);
                char *payload_str = cJSON_PrintUnformatted(mutation_payload);

                long put_status = 0;
                char *put_res = perform_http_request(ctx, endpoint, "PUT", payload_str, &put_status);
                if (likely(put_res)) free(put_res);

                if (likely(put_status == 204)) {
                    printf("\033[1;32m[SUCCESS]\033[0m Topology Mutation: [\033[1;36m%s\033[0m] -> [\033[1;32m%s\033[0m]\n", group_name, current);
                } else {
                    printf("\033[1;31m[ERROR]\033[0m Mutation Failed: [\033[1;36m%s\033[0m] (HTTP %ld)\n", group_name, put_status);
                }

                cJSON_Delete(mutation_payload);
                free(payload_str);
                curl_free(escaped_group);

                if (likely(strcmp(group_name, "GLOBAL") != 0)) {
                    if (unlikely(q_tail >= MATRIX_BOUNDS)) {
                        fprintf(stderr, "\033[1;31m[CRITICAL]\033[0m BFS Queue boundary overrun blocked.\n");
                        break;
                    }
                    queue[q_tail++] = strdup(group_name);
                }
            }
        }
        if (unlikely(!parent_found && is_first_tier)) {
            fprintf(stderr, "\033[1;31m[ERROR]\033[0m Topology Isolation: Leaf node '%s' contains no valid Selector parents.\n", current);
            break;
        }
        is_first_tier = 0;
    }

out_cleanup:
    if (raw_proxies) free(raw_proxies);
    if (root_json) cJSON_Delete(root_json);
    for (size_t i = 0; i < visited_count; i++) free(visited[i]);
    while (q_head < q_tail) free(queue[q_head++]);
}

/* --- Bounded Asynchronous libcurl Multi-Engine Matrix --- */
static void run_race(MhcContext *ctx, cJSON *nodes_array) {
    size_t total_nodes = cJSON_GetArraySize(nodes_array);
    if (unlikely(total_nodes == 0)) return;

    printf("\033[1;34m[INFO]\033[0m Triggering asynchronous parallel RTT matrix (Targets: \033[1;33m%zu\033[0m)...\n", total_nodes);
    CURLM *multi_handle = curl_multi_init();
    if (unlikely(!multi_handle)) return;
    curl_multi_setopt(multi_handle, CURLMOPT_MAX_TOTAL_CONNECTIONS, (long)MAX_CONCURRENT_PROBES);

    CURL **easy_handles = malloc(sizeof(CURL*) * total_nodes);
    HttpBuffer *buffers = malloc(sizeof(HttpBuffer) * total_nodes);
    char **node_names = malloc(sizeof(char*) * total_nodes);
    ProbeResult *results = malloc(sizeof(ProbeResult) * total_nodes);

    for (size_t i = 0; i < total_nodes; i++) {
        results[i].delay = MHC_RTT_TIMEOUT;
        cJSON *item = cJSON_GetArrayItem(nodes_array, i);
        node_names[i] = strdup(item->valuestring);
        buffers[i].payload = malloc(1);
        buffers[i].size = 0;

        easy_handles[i] = curl_easy_init();
        char *escaped = curl_easy_escape(easy_handles[i], node_names[i], 0);
        char url[1024];
        snprintf(url, sizeof(url), "%s/proxies/%s/delay?url=%s&timeout=%d", ctx->api_url, escaped, TEST_URL, TIMEOUT_MS);
        curl_free(escaped);

        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, ctx->auth_header);
        curl_easy_setopt(easy_handles[i], CURLOPT_URL, url);
        curl_easy_setopt(easy_handles[i], CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(easy_handles[i], CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(easy_handles[i], CURLOPT_WRITEDATA, &buffers[i]);
        
        /* Context Cast: Cast size_t index safely into the value of the generic pointer space */
        curl_easy_setopt(easy_handles[i], CURLOPT_PRIVATE, (void *)(size_t)i);
        curl_easy_setopt(easy_handles[i], CURLOPT_TIMEOUT_MS, (long)(TIMEOUT_MS + 500));
        curl_multi_add_handle(multi_handle, easy_handles[i]);
    }

    int still_running = 0;
    do {
        CURLMcode mc = curl_multi_perform(multi_handle, &still_running);
        if (still_running) mc = curl_multi_poll(multi_handle, NULL, 0, 1000, NULL);
        if (unlikely(mc != CURLM_OK)) break;
    } while (still_running);

    int msgs_left = 0;
    CURLMsg *msg = NULL;
    while ((msg = curl_multi_info_read(multi_handle, &msgs_left))) {
        if (unlikely(msg->msg != CURLMSG_DONE)) continue;
        
        /* Mandatory Fix: Read the token as an aligned pointer first, then unpack its address value */
        char *private_val = NULL;
        curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &private_val);
        size_t idx = (size_t)private_val;
        
        snprintf(results[idx].node_name, sizeof(results[idx].node_name), "%s", node_names[idx]);

        if (likely(msg->data.result == CURLE_OK)) {
            cJSON *res_json = cJSON_Parse(buffers[idx].payload);
            if (likely(res_json)) {
                cJSON *delay_obj = cJSON_GetObjectItemCaseSensitive(res_json, "delay");
                if (likely(delay_obj && cJSON_IsNumber(delay_obj))) results[idx].delay = delay_obj->valueint;
                cJSON_Delete(res_json);
            }
        }
    }

    FILE *fp = fopen(CACHE_FILE, "a+");
    if (likely(fp)) {
        int fd = fileno(fp);
        flock(fd, LOCK_EX);
        time_t now = time(NULL);
        printf("--- Latency Matrix Results ---\n");
        for (size_t i = 0; i < total_nodes; i++) {
            fprintf(fp, "%ld|%ld|%s\n", now, results[i].delay, results[i].node_name);
            if (unlikely(results[i].delay >= MHC_RTT_TIMEOUT)) printf("  - %s: \033[1;31mTimeout\033[0m\n", results[i].node_name);
            else printf("  - %s: \033[1;32m%ldms\033[0m\n", results[i].node_name, results[i].delay);
        }
        flock(fd, LOCK_UN);
        fclose(fp);
    }

    for (size_t i = 0; i < total_nodes; i++) {
        curl_multi_remove_handle(multi_handle, easy_handles[i]);
        curl_easy_cleanup(easy_handles[i]);
        free(buffers[i].payload);
        free(node_names[i]);
    }
    free(easy_handles); free(buffers); free(node_names); free(results);
    curl_multi_cleanup(multi_handle);
    printf("\033[1;32m[SUCCESS]\033[0m RTT metrics committed to local state cache securely.\n");
}

/* --- Core Unified Command Implementation Layer --- */
static int do_status(MhcContext *ctx, int argc, char **argv) {
    long status = 0;
    char *res = perform_http_request(ctx, "/configs", "GET", NULL, &status);
    if (unlikely(!res || status != 200)) { if (res) free(res); return -EIO; }
    
    cJSON *json = cJSON_Parse(res);
    free(res);
    if (unlikely(!json)) return -EBADMSG;

    cJSON *mode = cJSON_GetObjectItemCaseSensitive(json, "mode");
    printf("Current Kernel Mode: \033[1;36m%s\033[0m\n", mode ? mode->valuestring : "Unknown");
    cJSON_Delete(json);
    return 0;
}

static int do_mode(MhcContext *ctx, int argc, char **argv) {
    if (unlikely(argc < 3)) { fprintf(stderr, "Missing target mode token.\n"); return -EINVAL; }
    char payload[128];
    snprintf(payload, sizeof(payload), "{\"mode\": \"%s\"}", argv[2]);
    long status = 0;
    char *res = perform_http_request(ctx, "/configs", "PATCH", payload, &status);
    if (res) free(res);
    if (likely(status == 204 || status == 200)) printf("State Mutated: Mode switched to \033[1;32m%s\033[0m\n", argv[2]);
    return 0;
}

static int do_nodes(MhcContext *ctx, int argc, char **argv) {
    long status = 0;
    char *res = perform_http_request(ctx, "/proxies", "GET", NULL, &status);
    if (unlikely(!res || status != 200)) { if (res) free(res); return -EIO; }

    cJSON *json = cJSON_Parse(res);
    free(res);
    if (unlikely(!json)) return -EBADMSG;

    cJSON *proxies = cJSON_GetObjectItemCaseSensitive(json, "proxies");
    if (likely(proxies)) {
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, proxies) {
            char *name = item->string;
            if (strcmp(name, "DIRECT") == 0 || strcmp(name, "REJECT") == 0 ||
                strcmp(name, "REJECT-DROP") == 0 || strcmp(name, "PASS") == 0 ||
                strcmp(name, "GLOBAL") == 0 || strcmp(name, "COMPATIBLE") == 0) continue;
            printf("%s\n", name);
        }
    }
    cJSON_Delete(json);
    return 0;
}

static int execute_telemetry_workflow(MhcContext *ctx, const char *kw) {
    long status = 0;
    char *raw_proxies = perform_http_request(ctx, "/proxies", "GET", NULL, &status);
    if (unlikely(!raw_proxies || status != 200)) { if (raw_proxies) free(raw_proxies); return -EIO; }

    cJSON *root_json = cJSON_Parse(raw_proxies);
    free(raw_proxies);
    if (unlikely(!root_json)) return -EBADMSG;

    cJSON *proxies = cJSON_GetObjectItemCaseSensitive(root_json, "proxies");
    if (unlikely(!proxies)) { cJSON_Delete(root_json); return -ENODATA; }

    cJSON *filtered_nodes = cJSON_CreateArray();
    cJSON *proxy_item = NULL;
    cJSON_ArrayForEach(proxy_item, proxies) {
        char *node_name = proxy_item->string;
        if (strcmp(node_name, "DIRECT") == 0 || strcmp(node_name, "REJECT") == 0 ||
            strcmp(node_name, "REJECT-DROP") == 0 || strcmp(node_name, "PASS") == 0 ||
            strcmp(node_name, "COMPATIBLE") == 0 || strcmp(node_name, "GLOBAL") == 0) continue;

        if (kw && !strcasestr(node_name, kw)) continue;
        cJSON_AddItemToArray(filtered_nodes, cJSON_CreateString(node_name));
    }

    if (unlikely(cJSON_GetArraySize(filtered_nodes) == 0)) fprintf(stderr, "\033[1;31m[ERROR]\033[0m Target constraints matched 0 entities.\n");
    else run_race(ctx, filtered_nodes);

    cJSON_Delete(filtered_nodes); cJSON_Delete(root_json);
    return 0;
}

static int do_test(MhcContext *ctx, int argc, char **argv) {
    if (unlikely(argc < 3)) { fprintf(stderr, "\033[1;31m[ERROR]\033[0m Missing keyword constraint for 'test'.\n"); return -EINVAL; }
    return execute_telemetry_workflow(ctx, argv[2]);
}

static int do_testall(MhcContext *ctx, int argc, char **argv) {
    return execute_telemetry_workflow(ctx, NULL);
}

static int do_use(MhcContext *ctx, int argc, char **argv) {
    if (unlikely(argc < 3)) { fprintf(stderr, "Missing Keyword parameter.\n"); return -EINVAL; }
    const char *kw = argv[2];
    long status = 0;
    char *raw_proxies = perform_http_request(ctx, "/proxies", "GET", NULL, &status);
    if (unlikely(!raw_proxies || status != 200)) { if (raw_proxies) free(raw_proxies); return -EIO; }

    cJSON *root_json = cJSON_Parse(raw_proxies);
    free(raw_proxies);
    if (unlikely(!root_json)) return -EBADMSG;

    cJSON *proxies = cJSON_GetObjectItemCaseSensitive(root_json, "proxies");
    if (unlikely(!proxies)) { cJSON_Delete(root_json); return -ENODATA; }

    char *exact_match = NULL;
    char *fuzzy_matches[MATRIX_BOUNDS];
    size_t fuzzy_count = 0;

    cJSON *proxy_item = NULL;
    cJSON_ArrayForEach(proxy_item, proxies) {
        char *node_name = proxy_item->string;
        if (strcmp(node_name, "DIRECT") == 0 || strcmp(node_name, "REJECT") == 0 ||
            strcmp(node_name, "REJECT-DROP") == 0 || strcmp(node_name, "PASS") == 0 ||
            strcmp(node_name, "COMPATIBLE") == 0 || strcmp(node_name, "GLOBAL") == 0) continue;

        if (strcmp(node_name, kw) == 0) { exact_match = node_name; break; }
        
        if (strcasestr(node_name, kw)) {
            if (unlikely(fuzzy_count >= MATRIX_BOUNDS)) break; /* Safe Hard-Interception to prevent stack smash */
            fuzzy_matches[fuzzy_count++] = node_name;
        }
    }

    if (exact_match) { apply_route(ctx, exact_match); goto out_free; }
    if (fuzzy_count == 1) { apply_route(ctx, fuzzy_matches[0]); goto out_free; }
    if (unlikely(fuzzy_count == 0)) { fprintf(stderr, "\033[1;31m[ERROR]\033[0m Target keyword '%s' matched 0 entities.\n", kw); goto out_free; }

    printf("\033[1;33m[WARN]\033[0m Matched %zu nodes. Verifying local state cache...\n", fuzzy_count);
    FILE *cp = fopen(CACHE_FILE, "r");
    char best_node[256] = ""; long best_delay = MHC_RTT_TIMEOUT;
    
    if (likely(cp)) {
        char line[512]; time_t now = time(NULL);
        while (fgets(line, sizeof(line), cp)) {
            line[strcspn(line, "\n")] = 0;
            char *p1 = strchr(line, '|'); if (!p1) continue;
            char *p2 = strchr(p1 + 1, '|'); if (!p2) continue;
            *p1 = '\0'; *p2 = '\0';
            time_t ctime = (time_t)atol(line); long cdelay = atol(p1 + 1); char *cname = p2 + 1;

            for (size_t m = 0; m < fuzzy_count; m++) {
                if (strcmp(fuzzy_matches[m], cname) == 0 && now - ctime <= CACHE_TTL && cdelay < best_delay) {
                    best_delay = cdelay; 
                    snprintf(best_node, sizeof(best_node), "%s", cname); /* Implemented safe snprintf */
                }
            }
        }
        fclose(cp);
    }

    if (strlen(best_node) > 0 && best_delay < MHC_RTT_TIMEOUT) {
        printf("\033[1;32m[SUCCESS]\033[0m Cache Hit (Optimal): %s (%ldms)\n", best_node, best_delay);
        apply_route(ctx, best_node);
    } else {
        printf("\033[1;33m[WARN]\033[0m Cache Miss or Expired (>1 Hour). Manual intervention required.\n------------------------------------------------------------\n");
        for (size_t i = 0; i < fuzzy_count; i++) printf("%zu) %s\n", i + 1, fuzzy_matches[i]);
        printf("\033[1;36mSelect node index to route (Ctrl+C to abort): \033[0m");
        int choice = 0;
        if (likely(scanf("%d", &choice) == 1) && choice >= 1 && choice <= (int)fuzzy_count) {
            printf("\nYou selected: \033[1;32m%s\033[0m\n", fuzzy_matches[choice - 1]);
            apply_route(ctx, fuzzy_matches[choice - 1]);
        } else printf("\033[1;31mInvalid index.\033[0m\n");
    }

out_free:
    cJSON_Delete(root_json);
    return 0;
}

static int do_clear(MhcContext *ctx, int argc, char **argv) {
    unlink(CACHE_FILE);
    printf("\033[1;32m[SUCCESS]\033[0m Local latency cache purged from file system.\n");
    return 0;
}

static int do_restart(MhcContext *ctx, int argc, char **argv) {
    printf("\033[1;34m[INFO]\033[0m Dispatching systemctl restart clashtui_mihomo...\n");
    return system("sudo systemctl restart clashtui_mihomo");
}

/* --- Entry Point & Architectural Lookup Table Dispatcher --- */
int main(int argc, char **argv) {
    MhcContext ctx;
    init_context(&ctx);

    if (unlikely(argc < 2)) {
        printf("\033[1;36mMihomo CLI Controller (mhc) [Hardened Release]\033[0m\nUsage:\n");
        for (size_t i = 0; i < CMD_TABLE_SIZE; i++) printf("  mhc %-10s # %s\n", cmd_table[i].cmd_name, cmd_table[i].help_meta);
        return 1;
    }

    curl_global_init(CURL_GLOBAL_ALL);

    for (size_t i = 0; i < CMD_TABLE_SIZE; i++) {
        if (strcmp(argv[1], cmd_table[i].cmd_name) == 0) {
            int ret = cmd_table[i].handler(&ctx, argc, argv);
            curl_global_cleanup();
            return ret;
        }
    }

    fprintf(stderr, "\033[1;31m[ERROR]\033[0m Command '%s' not implemented or unsupported.\n", argv[1]);
    curl_global_cleanup();
    return -EINVAL;
}
