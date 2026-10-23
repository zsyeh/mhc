#define _GNU_SOURCE
#include <cjson/cJSON.h>
#include <curl/curl.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_BODY (1024u * 1024u)
#define MAX_ENTITIES 10000u
#define MAX_PROBES 20u
#define PROBE_TIMEOUT 3000L
#define CACHE_TTL 3600
#define TEST_URL "http://www.gstatic.com/generate_204"

typedef struct { char *data; size_t len; } Buffer;
typedef struct { char *api; char *auth; } Context;
typedef struct { char *name; cJSON *json; const char *provider; int *parents; size_t nparents; } Entity;
typedef struct { Entity *v; size_t n; cJSON *root, *providers_root; } Graph;
typedef enum { RTT_VALID, RTT_TIMEOUT, RTT_ERROR } RttState;
typedef struct { const char *name; const char *provider; long delay; RttState state; } Probe;

static void error_http(const char *operation, CURLcode code, long status) {
    if (code == CURLE_OPERATION_TIMEDOUT) fprintf(stderr, "%s: timeout\n", operation);
    else if (code == CURLE_COULDNT_CONNECT) fprintf(stderr, "%s: connection refused or unreachable\n", operation);
    else if (code != CURLE_OK) fprintf(stderr, "%s: %s\n", operation, curl_easy_strerror(code));
    else if (status == 401 || status == 403) fprintf(stderr, "%s: HTTP %ld authentication/permission denied\n", operation, status);
    else fprintf(stderr, "%s: HTTP %ld\n", operation, status);
}
static size_t receive(void *ptr, size_t size, size_t count, void *arg) {
    Buffer *b = arg;
    if (size && count > SIZE_MAX / size) return 0;
    size_t n = size * count;
    if (n > MAX_BODY - b->len) return 0;
    char *p = realloc(b->data, b->len + n + 1);
    if (!p) return 0;
    b->data = p;
    memcpy(p + b->len, ptr, n);
    b->len += n;
    p[b->len] = 0;
    return n;
}
static char *format(const char *fmt, const char *a, const char *b) {
    char *out = NULL;
    if (asprintf(&out, fmt, a, b) < 0) return NULL;
    return out;
}
static int setup(CURL *c, Context *ctx, const char *url, Buffer *b, struct curl_slist *headers) {
    return curl_easy_setopt(c, CURLOPT_URL, url) == CURLE_OK &&
        curl_easy_setopt(c, CURLOPT_PROXY, "") == CURLE_OK &&
        curl_easy_setopt(c, CURLOPT_NOPROXY, "*") == CURLE_OK &&
        curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers) == CURLE_OK &&
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, receive) == CURLE_OK &&
        curl_easy_setopt(c, CURLOPT_WRITEDATA, b) == CURLE_OK &&
        curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, 1500L) == CURLE_OK &&
        curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, 5000L) == CURLE_OK &&
        curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 0L) == CURLE_OK &&
        curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "http,https") == CURLE_OK && ctx;
}
static int request(Context *ctx, const char *path, const char *method, const char *payload, Buffer *b, long *status) {
    int ok = 0;
    CURL *c = curl_easy_init();
    char *url = format("%s%s", ctx->api, path);
    struct curl_slist *headers = NULL;
    if (!c || !url) goto done;
    if (ctx->auth) { headers = curl_slist_append(headers, ctx->auth); if (!headers) goto done; }
    if (payload) { struct curl_slist *h = curl_slist_append(headers, "Content-Type: application/json"); if (!h) goto done; headers = h; }
    if (!setup(c, ctx, url, b, headers) || curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, method) != CURLE_OK ||
        (payload && curl_easy_setopt(c, CURLOPT_POSTFIELDS, payload) != CURLE_OK)) goto done;
    CURLcode code = curl_easy_perform(c);
    *status = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, status);
    if (code != CURLE_OK || (*status != 200 && *status != 204)) error_http(path, code, *status);
    else ok = 1;
done:
    if (!ok && (!c || !url)) fprintf(stderr, "request: out of memory\n");
    curl_slist_free_all(headers); curl_easy_cleanup(c); free(url);
    return ok ? 0 : 1;
}
static int json_request(Context *ctx, const char *path, cJSON **out) {
    Buffer b = {0}; long status = 0;
    if (request(ctx, path, "GET", NULL, &b, &status)) { free(b.data); return 1; }
    *out = cJSON_Parse(b.data ? b.data : ""); free(b.data);
    if (!*out) { fprintf(stderr, "%s: invalid or empty JSON\n", path); return 1; }
    return 0;
}
static int entity_index(Graph *g, const char *name) {
    for (size_t i = 0; i < g->n; i++) if (!strcmp(g->v[i].name, name)) return (int)i;
    return -1;
}
static bool selector(Entity *e) {
    cJSON *t = cJSON_GetObjectItemCaseSensitive(e->json, "type");
    return cJSON_IsString(t) && !strcmp(t->valuestring, "Selector");
}
static bool real_proxy(Entity *e) {
    cJSON *t = cJSON_GetObjectItemCaseSensitive(e->json, "type");
    if (!cJSON_IsString(t)) return false;
    if (cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(e->json, "all"))) return false;
    const char *s = t->valuestring;
    return strcasecmp(s, "Direct") && strcasecmp(s, "Reject") && strcasecmp(s, "RejectDrop") &&
        strcasecmp(s, "Reject-Drop") &&
        strcasecmp(s, "Pass") && strcasecmp(s, "PassRule") && strcasecmp(s, "Compatible") &&
        strcasecmp(s, "Selector") && strcasecmp(s, "URLTest") &&
        strcasecmp(s, "Fallback") && strcasecmp(s, "LoadBalance") &&
        strcmp(e->name, "GLOBAL");
}
static void graph_free(Graph *g) {
    for (size_t i = 0; i < g->n; i++) free(g->v[i].parents);
    free(g->v); cJSON_Delete(g->root); cJSON_Delete(g->providers_root); memset(g, 0, sizeof(*g));
}
static int graph_load(Context *ctx, Graph *g) {
    if (json_request(ctx, "/proxies", &g->root)) return 1;
    cJSON *obj = cJSON_GetObjectItemCaseSensitive(g->root, "proxies");
    if (!cJSON_IsObject(obj)) goto invalid;
    if (json_request(ctx, "/providers/proxies", &g->providers_root)) goto invalid;
    cJSON *providers=cJSON_GetObjectItemCaseSensitive(g->providers_root,"providers");
    if (!cJSON_IsObject(providers)) goto invalid;
    size_t n = (size_t)cJSON_GetArraySize(obj);
    if(n>MAX_ENTITIES)goto invalid;
    cJSON *provider=NULL;
    cJSON_ArrayForEach(provider,providers) {
        cJSON *list=cJSON_GetObjectItemCaseSensitive(provider,"proxies");
        if(!cJSON_IsArray(list) || (size_t)cJSON_GetArraySize(list)>MAX_ENTITIES-n) goto invalid;
        n+=(size_t)cJSON_GetArraySize(list);
    }
    if (!n || n > MAX_ENTITIES || n > INT_MAX) goto invalid;
    g->v = calloc(n, sizeof(*g->v)); if (!g->v) goto invalid;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, obj) {
        if (!item->string || !cJSON_IsObject(item) || !cJSON_IsString(cJSON_GetObjectItemCaseSensitive(item, "type"))) goto invalid;
        g->v[g->n++] = (Entity){.name=item->string, .json=item};
    }
    cJSON_ArrayForEach(provider,providers) {
        cJSON *list=cJSON_GetObjectItemCaseSensitive(provider,"proxies");
        cJSON_ArrayForEach(item,list) {
            cJSON *name=cJSON_GetObjectItemCaseSensitive(item,"name");
            cJSON *type=cJSON_GetObjectItemCaseSensitive(item,"type");
            if(!cJSON_IsString(name)||!cJSON_IsString(type)) goto invalid;
            if(entity_index(g,name->valuestring)>=0)continue;
            g->v[g->n++]=(Entity){.name=name->valuestring,.json=item,.provider=provider->string};
        }
    }
    /* Reverse adjacency index: child -> Selector parents. */
    for (size_t p = 0; p < g->n; p++) {
        if (!selector(&g->v[p])) continue;
        cJSON *all = cJSON_GetObjectItemCaseSensitive(g->v[p].json, "all");
        if (!cJSON_IsArray(all)) goto invalid;
        cJSON *child = NULL;
        cJSON_ArrayForEach(child, all) {
            if (!cJSON_IsString(child)) goto invalid;
            int k = entity_index(g, child->valuestring);
            if (k < 0) continue; /* A provider may change between snapshots. */
            Entity *e = &g->v[k];
            int *next = reallocarray(e->parents, e->nparents + 1, sizeof(int));
            if (!next) goto invalid;
            e->parents = next; e->parents[e->nparents++] = (int)p;
        }
    }
    return 0;
invalid:
    fprintf(stderr, "/proxies: invalid or oversized topology\n"); graph_free(g); return 1;
}
static char *proxy_path(const char *name, const char *suffix) {
    char *escaped = curl_easy_escape(NULL, name, 0);
    if (!escaped) return NULL;
    char *path = format("/proxies/%s%s", escaped, suffix);
    curl_free(escaped); return path;
}
static char *delay_path(const Probe *p) {
    const char *query="/healthcheck?url=http%3A%2F%2Fwww.gstatic.com%2Fgenerate_204&timeout=3000";
    if(!p->provider) return proxy_path(p->name,"/delay?url=http%3A%2F%2Fwww.gstatic.com%2Fgenerate_204&timeout=3000");
    char *provider=curl_easy_escape(NULL,p->provider,0);
    char *name=curl_easy_escape(NULL,p->name,0);
    char *path=NULL;
    if(provider&&name&&asprintf(&path,"/providers/proxies/%s/%s%s",provider,name,query)<0)path=NULL;
    curl_free(provider);curl_free(name);return path;
}
static int verify_group(Context *ctx, const char *group, const char *expected) {
    char *path = proxy_path(group, ""); cJSON *j = NULL;
    if (!path) return 1;
    int bad = json_request(ctx, path, &j);
    if (!bad) {
        cJSON *now = cJSON_GetObjectItemCaseSensitive(j, "now");
        bad = !cJSON_IsString(now) || strcmp(now->valuestring, expected);
        if (bad) fprintf(stderr, "%s: remote selection differs from requested '%s'\n", group, expected);
    }
    cJSON_Delete(j); free(path); return bad;
}
static int mutate(Context *ctx, const char *group, const char *child) {
    cJSON *j = cJSON_CreateObject(); Buffer b = {0}; long status = 0;
    char *path = proxy_path(group, "");
    if (!j || !path || !cJSON_AddStringToObject(j, "name", child)) { cJSON_Delete(j); free(path); return 1; }
    char *payload = cJSON_PrintUnformatted(j); cJSON_Delete(j);
    int bad = !payload || request(ctx, path, "PUT", payload, &b, &status) || status != 204;
    if (!bad && verify_group(ctx, group, child)) {
        fprintf(stderr, "route mutation unverified: %s -> %s; remote state may have changed\n", group, child);
        bad = 1;
    }
    if (bad) fprintf(stderr, "route mutation failed: %s -> %s\n", group, child);
    free(payload); free(path); free(b.data); return bad;
}
static int route(Context *ctx, Graph *g, int target, const char *group, bool all) {
    if (!real_proxy(&g->v[target])) { fprintf(stderr, "target is not a physical proxy\n"); return 1; }
    if (group) {
        int p = entity_index(g, group); bool direct = false;
        for (size_t i=0; i<g->v[target].nparents; i++) if (g->v[target].parents[i] == p) direct = true;
        if (p < 0 || !selector(&g->v[p]) || !direct) { fprintf(stderr, "--group must name a direct Selector parent\n"); return 1; }
        int result=mutate(ctx, group, g->v[target].name);
        if(!result)printf("Route verified: %s (%s)\n",g->v[target].name,group);
        return result;
    }
    int *queue = calloc(g->n, sizeof(int)), *prev = malloc(g->n * sizeof(int));
    int *depth = malloc(g->n * sizeof(int));
    if (!queue || !prev || !depth) { free(queue); free(prev); free(depth); return 1; }
    for (size_t i=0;i<g->n;i++) { prev[i]=-1; depth[i]=-1; }
    size_t head=0, tail=0; queue[tail++]=target; depth[target]=0;
    int chosen=-1, candidates=0, global=entity_index(g, "GLOBAL");
    bool ambiguous=false,cycle=false;
    while (head<tail) {
        int k=queue[head++]; Entity *e=&g->v[k];
        if (k != target && (k == global || e->nparents == 0)) {
            if (chosen < 0 || (k == global && chosen != global)) chosen=k;
            candidates++;
        }
        for (size_t i=0;i<e->nparents;i++) {
            int p=e->parents[i];
            if (depth[p] >= 0) { ambiguous=true; if(depth[p]<=depth[k])cycle=true; fprintf(stderr, "route: cycle or reconverging path at %s\n", g->v[p].name); continue; }
            depth[p]=depth[k]+1; prev[p]=k; queue[tail++]=p;
        }
    }
    int failed=0, changed=0;
    if (cycle) { fprintf(stderr,"route: cycle detected; no mutation performed\n"); failed=1; }
    else if (ambiguous && !all) { fprintf(stderr,"route: multiple paths require explicit --group or --all-parents\n"); failed=1; }
    else if (all) {
        for (size_t i=1;i<tail;i++) {
            int p=queue[i]; int child=prev[p];
            if (child < 0 || mutate(ctx,g->v[p].name,g->v[child].name)) failed=1;
            else changed++;
        }
    } else if (chosen < 0 || candidates != 1) {
        fprintf(stderr, "route: %s; specify --group NAME or --all-parents\n", chosen < 0 ? "unreachable Selector root" : "ambiguous paths"); failed=1;
    } else {
        int *path=malloc(g->n*sizeof(int));size_t count=0;
        if(!path) failed=1;
        else {
            for(int k=chosen;k!=target&&k>=0;k=prev[k])path[count++]=k;
            for(size_t i=count;i>0;i--){int p=path[i-1],child=prev[p];
                if(child<0||mutate(ctx,g->v[p].name,g->v[child].name)){failed=1;break;}
                changed++;
            }
            free(path);
        }
    }
    if (failed && changed) fprintf(stderr, "route: partial change (%d groups verified); inspect remote state before retry\n", changed);
    if (!failed) printf("Route verified: %s (%d Selector groups)\n", g->v[target].name, changed);
    free(queue); free(prev); free(depth); return failed;
}
/* Cache files are opened relative to a private directory; the stable lock inode
 * serializes readers, writers and clear across atomic cache renames. */
typedef struct { int dir, lock; char key[32]; } Cache;
static uint64_t hash_api(const char *s) { uint64_t h=UINT64_C(1469598103934665603); for (;*s;s++) {h^=(unsigned char)*s;h*=UINT64_C(1099511628211);} return h; }
static int cache_open(Context *ctx, Cache *c) {
    c->dir=c->lock=-1;
    const char *base=getenv("XDG_CACHE_HOME"); char *fallback=NULL;
    if (!base || !*base || base[0]!='/') {
        const char *home=getenv("HOME"); if (!home || home[0]!='/') return 1;
        fallback=format("%s%s",home,"/.cache"); base=fallback;
    }
    if (mkdir(base,0700) && errno!=EEXIST) goto fail;
    int parent=open(base,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
    if (parent<0) goto fail;
    struct stat st;
    if (fstat(parent,&st) || st.st_uid!=getuid() || (st.st_mode&0022)) { close(parent); goto fail; }
    if (mkdirat(parent,"mhc",0700) && errno!=EEXIST) { close(parent); goto fail; }
    c->dir=openat(parent,"mhc",O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW); close(parent);
    if (c->dir<0 || fstat(c->dir,&st) || st.st_uid!=getuid() || (st.st_mode&0077)) goto fail;
    c->lock=openat(c->dir,"cache.lock",O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);
    if (c->lock<0 || fstat(c->lock,&st) || !S_ISREG(st.st_mode) || st.st_uid!=getuid() || (st.st_mode&0077)) goto fail;
    snprintf(c->key,sizeof(c->key),"%016" PRIx64 ".json",hash_api(ctx->api));
    free(fallback); return 0;
fail:
    fprintf(stderr,"cache: unsafe path or filesystem error: %s\n",strerror(errno));
    if(c->lock>=0)close(c->lock);
    if(c->dir>=0)close(c->dir);
    free(fallback); return 1;
}
static void cache_close(Cache *c) { close(c->lock);close(c->dir); }
static cJSON *cache_read_locked(Cache *c) {
    int fd=openat(c->dir,c->key,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
    if (fd<0) return cJSON_CreateObject();
    struct stat st; if (fstat(fd,&st) || !S_ISREG(st.st_mode) || st.st_uid!=getuid() || st.st_size>MAX_BODY || st.st_size<0) {close(fd);return NULL;}
    Buffer b={0}; char tmp[4096]; ssize_t n;
    while ((n=read(fd,tmp,sizeof(tmp)))>0) if(receive(tmp,1,(size_t)n,&b)!=(size_t)n) break;
    close(fd);
    cJSON *j=n==0 && b.data?cJSON_Parse(b.data):NULL; free(b.data);
    if (!cJSON_IsObject(j)) { cJSON_Delete(j); return cJSON_CreateObject(); }
    return j;
}
static int cache_write_locked(Cache *c,cJSON *j) {
    char *data=cJSON_PrintUnformatted(j); if(!data)return 1;
    char tmp[64]; snprintf(tmp,sizeof(tmp),".tmp.%ld.%ld",(long)getpid(),(long)time(NULL));
    int fd=openat(c->dir,tmp,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
    if(fd<0){free(data);return 1;}
    size_t len=strlen(data),off=0; int bad=len>MAX_BODY;
    while(!bad && off<len) {ssize_t n=write(fd,data+off,len-off);if(n<=0){bad=1;break;}off+=(size_t)n;}
    if(!bad && fsync(fd))bad=1;
    if(close(fd))bad=1;
    if(!bad && renameat(c->dir,tmp,c->dir,c->key))bad=1;
    if(!bad && fsync(c->dir))bad=1;
    if(bad)unlinkat(c->dir,tmp,0);
    free(data); return bad;
}
static int cache_get(Context *ctx,const char *name,long *delay) {
    Cache c;if(cache_open(ctx,&c))return 1;
    int bad=flock(c.lock,LOCK_SH); cJSON *j=bad?NULL:cache_read_locked(&c);
    if(!bad && j){cJSON *e=cJSON_GetObjectItemCaseSensitive(j,name);
        cJSON *ts=cJSON_GetObjectItemCaseSensitive(e,"time"),*s=cJSON_GetObjectItemCaseSensitive(e,"state"),*d=cJSON_GetObjectItemCaseSensitive(e,"delay");
        time_t now=time(NULL);
        bad=!(cJSON_IsNumber(ts)&&ts->valuedouble<=now&&now-ts->valuedouble<=CACHE_TTL&&cJSON_IsString(s)&&!strcmp(s->valuestring,"valid")&&cJSON_IsNumber(d)&&d->valuedouble>=0&&d->valuedouble<=LONG_MAX);
        if(!bad)*delay=(long)d->valuedouble;
    } else bad=1;
    cJSON_Delete(j);flock(c.lock,LOCK_UN);cache_close(&c);return bad;
}
static int cache_put(Context *ctx,Probe *probes,size_t n) {
    Cache c;if(cache_open(ctx,&c))return 1;
    if(flock(c.lock,LOCK_EX)){cache_close(&c);return 1;}
    cJSON *j=cache_read_locked(&c);int bad=!j;time_t now=time(NULL);
    if(j){cJSON *e=j->child,*next; while(e) {next=e->next;
        cJSON *ts=cJSON_GetObjectItemCaseSensitive(e,"time");
        if(!cJSON_IsNumber(ts)||ts->valuedouble>now||now-ts->valuedouble>CACHE_TTL)cJSON_DeleteItemFromObjectCaseSensitive(j,e->string);
        e=next;
    }}
    for(size_t i=0;!bad&&i<n;i++) {cJSON *v=cJSON_CreateObject();if(!v){bad=1;break;}
        cJSON_AddNumberToObject(v,"time",(double)now);
        cJSON_AddStringToObject(v,"state",probes[i].state==RTT_VALID?"valid":probes[i].state==RTT_TIMEOUT?"timeout":"error");
        if(probes[i].state==RTT_VALID)cJSON_AddNumberToObject(v,"delay",probes[i].delay);
        cJSON_DeleteItemFromObjectCaseSensitive(j,probes[i].name);
        if(!cJSON_AddItemToObject(j,probes[i].name,v)){cJSON_Delete(v);bad=1;}
    }
    if(!bad)bad=cache_write_locked(&c,j);
    cJSON_Delete(j);flock(c.lock,LOCK_UN);cache_close(&c);
    if(bad)fprintf(stderr,"cache: update failed\n");
    return bad;
}
static int cache_clear(Context *ctx) {
    Cache c;if(cache_open(ctx,&c))return 1;
    int bad=flock(c.lock,LOCK_EX);
    if(!bad && unlinkat(c.dir,c.key,0) && errno!=ENOENT)bad=1;
    if(!bad && fsync(c.dir))bad=1;
    flock(c.lock,LOCK_UN);cache_close(&c);
    if(bad)fprintf(stderr,"cache: clear failed\n");
    return bad;
}

static int probe_batch(Context *ctx,Probe *p,size_t n) {
    CURLM *m=curl_multi_init(); if(!m)return 1;
    CURL *easy[MAX_PROBES]={0};Buffer b[MAX_PROBES]={0};struct curl_slist *h[MAX_PROBES]={0};char *urls[MAX_PROBES]={0};bool added[MAX_PROBES]={0};
    int bad=0,active=0;
    if(curl_multi_setopt(m,CURLMOPT_MAX_TOTAL_CONNECTIONS,(long)MAX_PROBES)!=CURLM_OK)bad=1;
    for(size_t i=0;!bad&&i<n;i++) {
        easy[i]=curl_easy_init();char *path=delay_path(&p[i]);
        if(path)urls[i]=format("%s%s",ctx->api,path);
        free(path);
        if(ctx->auth)h[i]=curl_slist_append(NULL,ctx->auth);
        if(!easy[i]||!urls[i]||(ctx->auth&&!h[i])||!setup(easy[i],ctx,urls[i],&b[i],h[i])||
            curl_easy_setopt(easy[i],CURLOPT_TIMEOUT_MS,PROBE_TIMEOUT+1000)!=CURLE_OK||
            curl_easy_setopt(easy[i],CURLOPT_PRIVATE,(void *)(uintptr_t)(i+1))!=CURLE_OK||
            curl_multi_add_handle(m,easy[i])!=CURLM_OK){bad=1;break;}
        added[i]=true;
    }
    if(!bad){CURLMcode mc;do{mc=curl_multi_perform(m,&active);if(mc!=CURLM_OK)break;if(active)mc=curl_multi_poll(m,NULL,0,1000,NULL);}while(mc==CURLM_OK&&active);
        if(mc!=CURLM_OK)bad=1;
    }
    int remaining;CURLMsg *msg;
    while((msg=curl_multi_info_read(m,&remaining)))if(msg->msg==CURLMSG_DONE){
        void *id=NULL;curl_easy_getinfo(msg->easy_handle,CURLINFO_PRIVATE,&id);size_t i=(size_t)(uintptr_t)id;
        if(!i||i>n)continue;
        i--;long status=0;curl_easy_getinfo(msg->easy_handle,CURLINFO_RESPONSE_CODE,&status);
        if(msg->data.result==CURLE_OPERATION_TIMEDOUT)p[i].state=RTT_TIMEOUT;
        else if(msg->data.result==CURLE_OK&&status==200){cJSON *j=cJSON_Parse(b[i].data?b[i].data:"");cJSON *d=cJSON_GetObjectItemCaseSensitive(j,"delay");
            if(cJSON_IsNumber(d)&&d->valuedouble>=0&&d->valuedouble<=LONG_MAX){p[i].delay=(long)d->valuedouble;p[i].state=RTT_VALID;}else p[i].state=RTT_ERROR;cJSON_Delete(j);
        }else {p[i].state=RTT_ERROR;error_http(p[i].name,msg->data.result,status);}
    }
    for(size_t i=0;i<n;i++){if(added[i])curl_multi_remove_handle(m,easy[i]);curl_easy_cleanup(easy[i]);curl_slist_free_all(h[i]);free(b[i].data);free(urls[i]);}
    curl_multi_cleanup(m);return bad;
}
static int test_nodes(Context *ctx,Graph *g,const char *keyword) {
    Probe *p=calloc(g->n,sizeof(*p));if(!p)return 1;size_t n=0;
    for(size_t i=0;i<g->n;i++)if(real_proxy(&g->v[i])&&(!keyword||strcasestr(g->v[i].name,keyword)))p[n++]=(Probe){.name=g->v[i].name,.provider=g->v[i].provider,.state=RTT_ERROR};
    if(!n){fprintf(stderr,"no physical proxies matched\n");free(p);return 1;}
    int bad=0;for(size_t i=0;i<n;i+=MAX_PROBES){size_t count=n-i<MAX_PROBES?n-i:MAX_PROBES;if(probe_batch(ctx,p+i,count)){bad=1;break;}}
    for(size_t i=0;i<n;i++){if(p[i].state==RTT_VALID)printf("%s: %ld ms\n",p[i].name,p[i].delay);else{printf("%s: %s\n",p[i].name,p[i].state==RTT_TIMEOUT?"timeout":"error");bad=1;}}
    if(cache_put(ctx,p,n))bad=1;
    free(p);return bad;
}
static void usage(FILE *f) {
    fprintf(f,"Usage: mhc status | mode <direct|rule|global> | nodes | test <keyword> | testall\n"
        "       mhc use <keyword> [--group NAME | --all-parents] | clear | restart\n");
}
static int choose_target(Context *ctx,Graph *g,const char *kw,int *target) {
    if(!*kw){fprintf(stderr,"empty keyword\n");return 1;}
    int exact=-1;size_t matches=0;int only=-1;
    for(size_t i=0;i<g->n;i++)if(real_proxy(&g->v[i])){
        if(!strcmp(kw,g->v[i].name))exact=(int)i;
        if(strcasestr(g->v[i].name,kw)){matches++;only=(int)i;}
    }
    if(exact>=0){*target=exact;return 0;}
    if(matches==1){*target=only;return 0;}
    if(!matches){fprintf(stderr,"no physical proxy matched '%s'\n",kw);return 1;}
    long best=LONG_MAX;int best_idx=-1,ties=0;
    for(size_t i=0;i<g->n;i++)if(real_proxy(&g->v[i])&&strcasestr(g->v[i].name,kw)){
        long delay;if(!cache_get(ctx,g->v[i].name,&delay)){
            if(delay<best){best=delay;best_idx=(int)i;ties=1;}
            else if(delay==best)ties++;
        }
    }
    if(best_idx>=0&&ties==1){*target=best_idx;return 0;}
    if(!isatty(STDIN_FILENO)){fprintf(stderr,"ambiguous keyword; specify an exact name (no interactive terminal)\n");return 1;}
    fprintf(stderr,"Matched %zu proxies; enter index:\n",matches);size_t pos=0;
    for(size_t i=0;i<g->n;i++)if(real_proxy(&g->v[i])&&strcasestr(g->v[i].name,kw))fprintf(stderr,"%zu) %s\n",++pos,g->v[i].name);
    char line[64];if(!fgets(line,sizeof(line),stdin))return 1;
    char *end;errno=0;unsigned long selected=strtoul(line,&end,10);
    if(errno||end==line||(*end!='\n'&&*end!='\0')||selected<1||selected>matches){fprintf(stderr,"invalid selection\n");return 1;}
    pos=0;for(size_t i=0;i<g->n;i++)if(real_proxy(&g->v[i])&&strcasestr(g->v[i].name,kw)&&++pos==selected){*target=(int)i;return 0;}
    return 1;
}
int main(int argc,char **argv) {
    if(argc<2){usage(stderr);return 2;}
    const char *api=getenv("MHC_API"),*secret=getenv("MHC_SECRET");
    if(!api||!*api)api="http://127.0.0.1:9090";
    if(strncmp(api,"http://",7)&&strncmp(api,"https://",8)){fprintf(stderr,"MHC_API must use http:// or https://\n");return 2;}
    const char *authority=api+(api[4]=='s'?8:7);
    const char *authority_end=strchr(authority,'/');
    size_t authority_len=authority_end?(size_t)(authority_end-authority):strlen(authority);
    if(!authority_len||memchr(authority,'@',authority_len)||strpbrk(api,"\r\n#?")){
        fprintf(stderr,"MHC_API contains an invalid authority, credential, query, or fragment\n");return 2;
    }
    size_t len=strlen(api);if(len>4096){fprintf(stderr,"MHC_API is too long\n");return 2;}
    char *api_copy=strdup(api);if(!api_copy)return 1;
    while(len>8&&api_copy[len-1]=='/')api_copy[--len]=0;
    Context ctx={.api=api_copy,.auth=NULL};
    if(secret&&*secret){if(strpbrk(secret,"\r\n")||strlen(secret)>4096){fprintf(stderr,"invalid MHC_SECRET\n");free(api_copy);return 2;}
        ctx.auth=format("Authorization: Bearer %s%s",secret,"");if(!ctx.auth){free(api_copy);return 1;}}
    if(curl_global_init(CURL_GLOBAL_DEFAULT)!=CURLE_OK){free(ctx.auth);free(api_copy);return 1;}
    int rc=1;Graph g={0};
    if(!strcmp(argv[1],"clear")&&argc==2){rc=cache_clear(&ctx);if(!rc)puts("Cache cleared");goto done;}
    if(!strcmp(argv[1],"restart")&&argc==2){int status=system("sudo systemctl restart clashtui_mihomo");rc=status!=-1&&WIFEXITED(status)&&WEXITSTATUS(status)==0?0:1;if(rc)fprintf(stderr,"restart failed\n");goto done;}
    if(!strcmp(argv[1],"status")&&argc==2){cJSON *j=NULL;if(json_request(&ctx,"/configs",&j))goto done;cJSON *mode=cJSON_GetObjectItemCaseSensitive(j,"mode");
        if(cJSON_IsString(mode)){printf("Mode: %s\n",mode->valuestring);rc=0;}else fprintf(stderr,"/configs: missing mode\n");cJSON_Delete(j);goto done;}
    if(!strcmp(argv[1],"mode")&&argc==3){const char *m=argv[2];if(strcasecmp(m,"direct")&&strcasecmp(m,"rule")&&strcasecmp(m,"global")){fprintf(stderr,"unsupported mode\n");rc=2;goto done;}
        cJSON *j=cJSON_CreateObject();if(!j||!cJSON_AddStringToObject(j,"mode",m)){cJSON_Delete(j);goto done;}
        char *payload=cJSON_PrintUnformatted(j);cJSON_Delete(j);Buffer b={0};long status=0;
        if(payload&&!request(&ctx,"/configs","PATCH",payload,&b,&status)&&(status==200||status==204)){puts("Mode updated");rc=0;}
        free(payload);free(b.data);goto done;}
    if(!strcmp(argv[1],"nodes")&&argc==2){if(graph_load(&ctx,&g))goto done;for(size_t i=0;i<g.n;i++)if(real_proxy(&g.v[i]))puts(g.v[i].name);rc=0;goto done;}
    if((!strcmp(argv[1],"test")&&argc==3)||(!strcmp(argv[1],"testall")&&argc==2)){
        if(graph_load(&ctx,&g))goto done;
        rc=test_nodes(&ctx,&g,!strcmp(argv[1],"test")?argv[2]:NULL);goto done;}
    if(!strcmp(argv[1],"use")&&argc>=3&&argc<=5){const char *group=NULL;bool all=false;
        if(argc>3){if(argc==5&&!strcmp(argv[3],"--group"))group=argv[4];else if(argc==4&&!strcmp(argv[3],"--all-parents"))all=true;else{usage(stderr);rc=2;goto done;}}
        if(graph_load(&ctx,&g))goto done;
        int target=-1;if(choose_target(&ctx,&g,argv[2],&target))goto done;
        rc=route(&ctx,&g,target,group,all);goto done;}
    usage(stderr);rc=2;
done:
    graph_free(&g);curl_global_cleanup();free(ctx.auth);free(api_copy);return rc;
}
