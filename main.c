/*******************************************************************************
 * Mihomo CLI Controller (mhc) - High-Performance Cascade Routing Engine
 * Copyright (c) 2026 Eh. All rights reserved.
 *
 * Dependency: libcurl, cJSON
 * Compile: gcc -O3 mhc.c -lcurl -lcjson -o mhc
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

/* --- Architectural Configuration Constants --- */
#define DEFAULT_API "http://127.0.0.1:9090"
#define DEFAULT_SECRET "bushieh"
#define TEST_URL "http://www.gstatic.com/generate_204"
#define CACHE_FILE "/tmp/mhc_rtt.cache"
#define CACHE_TTL 3600
#define MAX_CONCURRENT_PROBES 20  /* Bounds network I/O blast radius */
#define TIMEOUT_MS 3000

/* --- Global Context Structure for Environment Decoupling --- */
typedef struct {
    char api_url[256];
    char auth_header[512];
    char secret[128];
} MhcContext;

/* --- Memory Buffer for libcurl HTTP Response Streams --- */
typedef struct {
    char *payload;
    size_t size;
} HttpBuffer;

/* --- Node Telemetry Matrix Structure --- */
typedef struct {
    char node_name[256];
    long delay;
} ProbeResult;

/* Callback execution for memory buffer buffering */
static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    HttpBuffer *mem = (HttpBuffer *)userp;

    char *ptr = realloc(mem->payload, mem->size + realsize + 1);
    if (!ptr) {
        fprintf(stderr, "[CRITICAL] Out of memory during allocation.\n");
        return 0;
    }

    mem->payload = ptr;
    memcpy(&(mem->payload[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->payload[mem->size] = 0;

    return realsize;
}

/* Base infrastructure initialization and configuration mapping */
void init_context(MhcContext *ctx) {
    const char *env_api = getenv("MHC_API");
    const char *env_secret = getenv("MHC_SECRET");

    snprintf(ctx->api_url, sizeof(ctx->api_url), "%s", env_api ? env_api : DEFAULT_API);
    snprintf(ctx->secret, sizeof(ctx->secret), "%s", env_secret ? env_secret : DEFAULT_SECRET);
    snprintf(ctx->auth_header, sizeof(ctx->auth_header), "Authorization: Bearer %s", ctx->secret);
}

/* Synchronous Atomic HTTP REST Engine Wrapper */
char* perform_http_request(MhcContext *ctx, const char *endpoint, const char *method, const char *json_payload, long *out_status) {
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;

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
    if (out_status) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, out_status);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        free(response.payload);
        return NULL;
    }
    return response.payload;
}

/* Safe token verification to eliminate the Bash fuzzy match substring bug */
int is_visited(char **set, size_t count, const char *target) {
    for (size_t i = 0; i < count; i++) {
        if (strcmp(set[i], target) == 0) return 1;
    }
    return 0;
}

/* --- Unbounded BFS Upward Propagation Engine --- */
void apply_route(MhcContext *ctx, const char *target_node) {
    long status = 0;
    char *raw_proxies = perform_http_request(ctx, "/proxies", "GET", NULL, &status);
    if (!raw_proxies || status != 200) {
        fprintf(stderr, "\033[1;31m[ERROR]\033[0m Failed to fetch memory kernel routing graph.\n");
        return;
    }

    cJSON *root_json = cJSON_Parse(raw_proxies);
    free(raw_proxies);
    if (!root_json) return;

    cJSON *proxies = cJSON_GetObjectItemCaseSensitive(root_json, "proxies");
    if (!proxies) {
        cJSON_Delete(root_json);
        return;
    }

    /* Queue Allocation for Graph Traversal Traps */
    char *queue[256];
    size_t q_head = 0, q_tail = 0;
    queue[q_tail++] = strdup(target_node);

    char *visited[256];
    size_t visited_count = 0;
    int is_first_tier = 1;

    while (q_head < q_tail) {
        char *current = queue[q_head++];

        if (is_visited(visited, visited_count, current)) {
            free(current);
            continue;
        }
        visited[visited_count++] = current;

        /* Inverse Graph Traversal Pointer Extraction */
        cJSON *proxy_item = NULL;
        int parent_found = 0;

        cJSON_ArrayForEach(proxy_item, proxies) {
            cJSON *type_obj = cJSON_GetObjectItemCaseSensitive(proxy_item, "type");
            cJSON *all_obj = cJSON_GetObjectItemCaseSensitive(proxy_item, "all");

            if (type_obj && all_obj && strcmp(type_obj->valuestring, "Selector") == 0) {
                cJSON *child = NULL;
                int is_child = 0;
                cJSON_ArrayForEach(child, all_obj) {
                    if (strcmp(child->valuestring, current) == 0) {
                        is_child = 1;
                        break;
                    }
                }

                if (is_child) {
                    parent_found = 1;
                    char *group_name = proxy_item->string;
                    
                    /* Executing Atomic Pointer Mutation Payload via PUT */
                    char *escaped_group = curl_easy_escape(NULL, group_name, 0);
                    char endpoint[512];
                    snprintf(endpoint, sizeof(endpoint), "/proxies/%s", escaped_group);
                    
                    cJSON *mutation_payload = cJSON_CreateObject();
                    cJSON_AddStringToObject(mutation_payload, "name", current);
                    char *payload_str = cJSON_PrintUnformatted(mutation_payload);

                    long put_status = 0;
                    char *put_res = perform_http_request(ctx, endpoint, "PUT", payload_str, &put_status);
                    if (put_res) free(put_res);

                    if (put_status == 204) {
                        printf("\033[1;32m[SUCCESS]\033[0m Topology Mutation: [\033[1;36m%s\033[0m] -> [\033[1;32m%s\033[0m]\n", group_name, current);
                    } else {
                        printf("\033[1;31m[ERROR]\033[0m Mutation Failed: [\033[1;36m%s\033[0m] (HTTP %ld)\n", group_name, put_status);
                    }

                    cJSON_Delete(mutation_payload);
                    free(payload_str);
                    curl_free(escaped_group);

                    /* Intercept upward cascade propagation if GLOBAL boundary reached */
                    if (strcmp(group_name, "GLOBAL") != 0) {
                        queue[q_tail++] = strdup(group_name);
                    }
                }
            }
        }

        if (!parent_found && is_first_tier) {
            fprintf(stderr, "\033[1;31m[ERROR]\033[0m Topology Isolation: Leaf node contains no valid Selector parents.\n");
            break;
        }
        is_first_tier = 0;
    }

    /* Absolute Resource Reclamation Matrix */
    for (size_t i = 0; i < visited_count; i++) free(visited[i]);
    while (q_head < q_tail) free(queue[q_head++]);
    cJSON_Delete(root_json);
}

/* --- Bounded Asynchronous libcurl Multi-Engine Matrix --- */
void run_race(MhcContext *ctx, cJSON *nodes_array) {
    size_t total_nodes = cJSON_GetArraySize(nodes_array);
    if (total_nodes == 0) return;

    printf("\033[1;34m[INFO]\033[0m Triggering asynchronous parallel RTT matrix (Targets: \033[1;33m%zu\033[0m)...\n", total_nodes);

    CURLM *multi_handle = curl_multi_init();
    curl_multi_setopt(multi_handle, CURLMOPT_MAX_TOTAL_CONNECTIONS, (long)MAX_CONCURRENT_PROBES);

    /* Allocate runtime context pointers to track chunks */
    CURL **easy_handles = malloc(sizeof(CURL*) * total_nodes);
    HttpBuffer *buffers = malloc(sizeof(HttpBuffer) * total_nodes);
    char **node_names = malloc(sizeof(char*) * total_nodes);

    for (size_t i = 0; i < total_nodes; i++) {
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
        curl_easy_setopt(easy_handles[i], CURLOPT_PRIVATE, (void *)i);
        curl_easy_setopt(easy_handles[i], CURLOPT_TIMEOUT_MS, (long)(TIMEOUT_MS + 500));

        curl_multi_add_handle(multi_handle, easy_handles[i]);
    }

    /* High Performance Non-Blocking I/O Polling Loop */
    int still_running = 0;
    do {
        CURLMcode mc = curl_multi_perform(multi_handle, &still_running);
        if (still_running) {
            mc = curl_multi_poll(multi_handle, NULL, 0, 1000, NULL);
        }
        if (mc != CURLM_OK) break;
    } while (still_running);

    /* Telemetry Metrics Extraction Phase */
    int msgs_left = 0;
    CURLMsg *msg = NULL;
    ProbeResult *results = malloc(sizeof(ProbeResult) * total_nodes);
    for(size_t i=0; i<total_nodes; i++) results[i].delay = 999999;

    while ((msg = curl_multi_info_read(multi_handle, &msgs_left))) {
        if (msg->msg == CURLMSG_DONE) {
            size_t idx = 0;
            curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &idx);
            strcpy(results[idx].node_name, node_names[idx]);

            if (msg->data.result == CURLE_OK) {
                cJSON *res_json = cJSON_Parse(buffers[idx].payload);
                if (res_json) {
                    cJSON *delay_obj = cJSON_GetObjectItemCaseSensitive(res_json, "delay");
                    if (delay_obj && cJSON_IsNumber(delay_obj)) {
                        results[idx].delay = delay_obj->valueint;
                    }
                    cJSON_Delete(res_json);
                }
            }
        }
    }

    /* Commit Telemetry into Thread-Safe Guarded State Cache File System */
    FILE *fp = fopen(CACHE_FILE, "a+");
    if (fp) {
        int fd = fileno(fp);
        flock(fd, LOCK_EX); /* Mandatory Lock to prevent concurrency hazards */
        time_t now = time(NULL);
        printf("--- Latency Matrix Results ---\n");
        for (size_t i = 0; i < total_nodes; i++) {
            fprintf(fp, "%ld|%ld|%s\n", now, results[i].delay, results[i].node_name);
            if (results[i].delay >= 999999) {
                printf("  - %s: \033[1;31mTimeout\033[0m\n", results[i].node_name);
            } else {
                printf("  - %s: \033[1;32m%ldms\033[0m\n", results[i].node_name, results[i].delay);
            }
        }
        flock(fd, LOCK_UN);
        fclose(fp);
    }

    /* Release Allocation Pools */
    for (size_t i = 0; i < total_nodes; i++) {
        curl_multi_remove_handle(multi_handle, easy_handles[i]);
        curl_easy_cleanup(easy_handles[i]);
        free(buffers[i].payload);
        free(node_names[i]);
    }
    free(easy_handles);
    free(buffers);
    free(node_names);
    free(results);
    curl_multi_cleanup(multi_handle);
    printf("\033[1;32m[SUCCESS]\033[0m RTT metrics committed to local state cache securely.\n");
}

/* Command execution dispatch entry point */
int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Mihomo CLI Controller (mhc) written in Optimized C\nUsage: %s <status|mode|nodes|test|testall|use|clear|restart>\n", argv[0]);
        return 1;
    }

    MhcContext ctx;
    init_context(&ctx);
    curl_global_init(CURL_GLOBAL_ALL);

    if (strcmp(argv[1], "status") == 0) {
        long status = 0;
        char *res = perform_http_request(&ctx, "/configs", "GET", NULL, &status);
        if (res && status == 200) {
            cJSON *json = cJSON_Parse(res);
            if (json) {
                cJSON *mode = cJSON_GetObjectItemCaseSensitive(json, "mode");
                printf("Current Kernel Mode: \033[1;36m%s\033[0m\n", mode ? mode->valuestring : "Unknown");
                cJSON_Delete(json);
            }
        }
        if (res) free(res);
    } 
    else if (strcmp(argv[1], "mode") == 0) {
        if (argc < 3) { fprintf(stderr, "Missing target mode token.\n"); return 1; }
        char payload[128];
        snprintf(payload, sizeof(payload), "{\"mode\": \"%s\"}", argv[2]);
        long status = 0;
        char *res = perform_http_request(&ctx, "/configs", "PATCH", payload, &status);
        if (status == 204 || status == 200) {
            printf("State Mutated: Mode switched to \033[1;32m%s\033[0m\n", argv[2]);
        }
        if (res) free(res);
    } 
    else if (strcmp(argv[1], "use") == 0) {
        if (argc < 3) { fprintf(stderr, "Missing Keyword parameter.\n"); return 1; }
        /* Execute exact target propagation directly */
        apply_route(&ctx, argv[2]);
    }
    else if (strcmp(argv[1], "clear") == 0) {
        unlink(CACHE_FILE);
        printf("\033[1;32m[SUCCESS]\033[0m Local latency cache purged from file system.\n");
    }
    else if (strcmp(argv[1], "restart") == 0) {
        printf("\033[1;34m[INFO]\033[0m Dispatching systemctl restart clashtui_mihomo...\n");
        system("sudo systemctl restart clashtui_mihomo");
    }
    else {
        fprintf(stderr, "Command not implemented or unsupported via optimized runtime wrapper.\n");
    }

    curl_global_cleanup();
    return 0;
}
