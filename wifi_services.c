/**
 * @file    wifi_services.c
 * @brief   基于 connmanctl 的 Wi-Fi 服务层实现
 *
 * - 后台线程执行 connmanctl, 不阻塞 LVGL;
 * - 只在关键事件(命令执行、列表变化)时输出日志, 避免刷屏;
 * - 兼容旧版 connman(不支持 services --properties), 通过逐个
 *   `connmanctl services <svc>` 拿到信号强度, 并做 2.5s 缓存。
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "wifi_services.h"

#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>

/*---------------------------------------------------------------------------
 * 可配置项
 *--------------------------------------------------------------------------*/
#define WIFI_SHELL_PROMPT        "root@atk-imx6u:~# "
#define WIFI_LOG_PASSWORD        1
#define WIFI_SCAN_POLL_TIMES     6
#define WIFI_SCAN_POLL_MS        1000
#define WIFI_CONNECT_POLL_TIMES  12
#define WIFI_CONNECT_POLL_MS     1000
#define WIFI_ONLY_2G4            1
#define WIFI_JOB_QUEUE_LEN       8
#define WIFI_CMD_OUT_MAX         4096
#define WIFI_LOG                 "[wifi] "

/* 逐个服务查询属性的缓存有效期(ms) */
#define WIFI_DETAIL_CACHE_MS     2500

/*---------------------------------------------------------------------------
 * 内部类型
 *--------------------------------------------------------------------------*/
typedef enum {
    JOB_NONE = 0,
    JOB_ENABLE,
    JOB_DISABLE,
    JOB_SCAN,
    JOB_CONNECT,
    JOB_DISCONNECT
} wifi_job_id_t;

typedef struct {
    wifi_job_id_t id;
    char          service[WIFI_SERVICE_PATH_LEN];
    char          pass[WIFI_PASSPHRASE_MAX_LEN];
} wifi_job_t;

typedef struct {
    wifi_ap_t ap;
    bool      has_strength;
    bool      has_freq;
    bool      is_online;
    bool      is_hidden;
} wifi_item_t;

/*---------------------------------------------------------------------------
 * 内部状态
 *--------------------------------------------------------------------------*/
static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  s_cond = PTHREAD_COND_INITIALIZER;
static pthread_t       s_thread;
static bool            s_thread_run = false;
static bool            s_inited     = false;

static wifi_job_t      s_queue[WIFI_JOB_QUEUE_LEN];
static int             s_q_head = 0, s_q_tail = 0, s_q_count = 0;

static wifi_ap_t       s_aps[WIFI_AP_MAX_COUNT];
static int             s_ap_count   = 0;
static int             s_ap_version = 0;

static wifi_state_t    s_state = WIFI_STATE_IDLE;
static char            s_status[WIFI_STATUS_TEXT_LEN] = "";

static bool            s_enabled  = false;
static bool            s_need_pwd = false;
static char            s_need_pwd_service[WIFI_SERVICE_PATH_LEN] = "";

static char            s_conn_ssid[WIFI_SSID_MAX_LEN] = "";
static char            s_conn_service[WIFI_SERVICE_PATH_LEN] = "";

/*---------------------------------------------------------------------------
 * 工具函数
 *--------------------------------------------------------------------------*/
static void msleep(unsigned int ms)
{
    struct timespec ts;
    ts.tv_sec  = (time_t)(ms / 1000);
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static bool strieq(const char* a, const char* b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        a++; b++;
    }
    return (*a == '\0' && *b == '\0');
}

static char* str_trim(char* s)
{
    if (!s) return s;
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    size_t n = strlen(s);
    while (n > 0) {
        char c = s[n - 1];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') s[--n] = '\0';
        else break;
    }
    return s;
}

/* 把字符串转成 shell 单引号安全形式: abc -> 'abc', a'b -> 'a'\''b' */
static void shell_quote(const char* in, char* out, size_t out_sz)
{
    size_t oi = 0;
    if (!in) in = "";
    if (out_sz < 4) { if (out_sz) out[0] = '\0'; return; }
    out[oi++] = '\'';
    for (const char* p = in; *p; p++) {
        if (*p == '\'') {
            if (oi + 5 >= out_sz) break;
            memcpy(out + oi, "'\\''", 4);
            oi += 4;
        } else {
            if (oi + 2 >= out_sz) break;
            out[oi++] = *p;
        }
    }
    out[oi++] = '\'';
    out[oi]   = '\0';
}

static int hex_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* 从 service 路径解出 SSID: wifi_<mac>_<ssid hex>_<type>_<sec> */
static void ssid_from_service_path(const char* path, char* ssid, size_t sz)
{
    const char* p;
    size_t oi = 0;
    if (sz == 0) return;
    ssid[0] = '\0';
    if (!path || strncmp(path, "wifi_", 5) != 0) return;

    p = path + 5;
    p = strchr(p, '_');
    if (!p) return;
    p++;

    while (*p && *p != '_' && oi + 1 < sz) {
        int hi = hex_val(p[0]);
        int lo = p[1] ? hex_val(p[1]) : -1;
        if (hi < 0 || lo < 0) break;
        char c = (char)((hi << 4) | lo);
        if (c == '\0') break;
        ssid[oi++] = c;
        p += 2;
    }
    ssid[oi] = '\0';
}

static uint8_t security_from_service_path(const char* path)
{
    size_t n;
    if (!path) return WIFI_SEC_UNKNOWN;
    n = strlen(path);
    if (n >= 5  && strcmp(path + n - 5,  "_none") == 0)      return WIFI_SEC_NONE;
    if (n >= 4  && strcmp(path + n - 4,  "_psk") == 0)       return WIFI_SEC_PSK;
    if (n >= 4  && strcmp(path + n - 4,  "_wep") == 0)       return WIFI_SEC_WEP;
    if (n >= 10 && strcmp(path + n - 10, "_ieee8021x") == 0) return WIFI_SEC_8021X;
    return WIFI_SEC_UNKNOWN;
}

static uint8_t security_from_string(const char* s)
{
    if (!s) return WIFI_SEC_UNKNOWN;
    if (strieq(s, "none"))      return WIFI_SEC_NONE;
    if (strieq(s, "psk"))       return WIFI_SEC_PSK;
    if (strieq(s, "wep"))       return WIFI_SEC_WEP;
    if (strieq(s, "ieee8021x")) return WIFI_SEC_8021X;
    if (strieq(s, "wps"))       return WIFI_SEC_PSK;
    return WIFI_SEC_UNKNOWN;
}

static bool is_wifi_service(const char* s)
{
    return (s && strncmp(s, "wifi_", 5) == 0);
}

/*---------------------------------------------------------------------------
 * 命令执行
 *--------------------------------------------------------------------------*/
static int wifi_exec(const char* cmd, const char* display,
                     char* out, size_t out_sz, bool echo_out)
{
    FILE*  fp;
    size_t used = 0;
    char   buf[256];

    if (echo_out) {
        printf(WIFI_SHELL_PROMPT "%s\n", (display && display[0]) ? display : cmd);
        fflush(stdout);
    }

    fp = popen(cmd, "r");
    if (!fp) {
        if (echo_out) {
            printf(WIFI_LOG "popen 失败: %s\n", strerror(errno));
            fflush(stdout);
        }
        return -1;
    }
    if (out && out_sz) out[0] = '\0';

    while (fgets(buf, sizeof(buf), fp) != NULL) {
        if (echo_out) {
            fputs(buf, stdout);
            fflush(stdout);
        }
        if (out && used + 1 < out_sz) {
            size_t l = strlen(buf);
            if (l > out_sz - 1 - used) l = out_sz - 1 - used;
            memcpy(out + used, buf, l);
            used += l;
            out[used] = '\0';
        }
    }

    int st = pclose(fp);
    if (st == -1) return -1;
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

/*---------------------------------------------------------------------------
 * 状态读写
 *--------------------------------------------------------------------------*/
static void wifi_set_state(wifi_state_t st, const char* text)
{
    pthread_mutex_lock(&s_lock);
    s_state = st;
    if (text) {
        strncpy(s_status, text, sizeof(s_status) - 1);
        s_status[sizeof(s_status) - 1] = '\0';
    } else if (st == WIFI_STATE_IDLE) {
        s_status[0] = '\0';
    }
    pthread_mutex_unlock(&s_lock);
}

static void wifi_set_enabled(bool on)
{
    pthread_mutex_lock(&s_lock);
    s_enabled = on;
    pthread_mutex_unlock(&s_lock);
}

static int wifi_ap_count_snapshot(void)
{
    int n;
    pthread_mutex_lock(&s_lock);
    n = s_ap_count;
    pthread_mutex_unlock(&s_lock);
    return n;
}

static void wifi_set_need_password(const char* service)
{
    pthread_mutex_lock(&s_lock);
    if (service && service[0]) {
        s_need_pwd = true;
        strncpy(s_need_pwd_service, service, sizeof(s_need_pwd_service) - 1);
        s_need_pwd_service[sizeof(s_need_pwd_service) - 1] = '\0';
    } else {
        s_need_pwd = false;
        s_need_pwd_service[0] = '\0';
    }
    pthread_mutex_unlock(&s_lock);
}

/*---------------------------------------------------------------------------
 * 解析 connmanctl services [--properties] 输出
 *--------------------------------------------------------------------------*/
static bool is_flag_char(char c)
{
    return (c == '*' || c == 'A' || c == 'R' || c == 'C' || c == 'a' ||
            c == 'D' || c == 'I' || c == 'F' || c == 'O' || c == ' ');
}

/* 去掉服务行开头的标志位, 并提取 favorite / online */
static void strip_flags(char* s, bool* favorite, bool* online)
{
    if (favorite) *favorite = false;
    if (online)   *online   = false;
    if (!s || !s[0]) return;

    char* p = s;
    int   seen_flags = 0;
    while (seen_flags < 3 && is_flag_char(*p)) {
        if (*p == '*') { if (favorite) *favorite = true; }
        if (*p == 'R' || *p == 'O') { if (online) *online = true; }
        p++; seen_flags++;
    }
    while (*p == ' ' || *p == '\t') p++;

    if (p != s) memmove(s, p, strlen(p) + 1);
}

static void apply_property(wifi_item_t* it, const char* key, const char* val)
{
    if (!it || !key || !val) return;

    if (strieq(key, "Name")) {
        snprintf(it->ap.ssid, sizeof(it->ap.ssid), "%s", val);
    } else if (strieq(key, "Strength")) {
        it->ap.strength = (int8_t)atoi(val);
        if (it->ap.strength < 0) it->ap.strength = 0;
        if (it->ap.strength > 100) it->ap.strength = 100;
        it->has_strength = true;
    } else if (strieq(key, "Frequency")) {
        it->ap.frequency = (uint16_t)atoi(val);
        it->has_freq = true;
    } else if (strieq(key, "Security")) {
        it->ap.security = security_from_string(val);
    } else if (strieq(key, "Favorite")) {
        it->ap.saved = strieq(val, "true");
    } else if (strieq(key, "AutoConnect")) {
        if (strieq(val, "true")) it->ap.saved = true;
    } else if (strieq(key, "State")) {
        it->is_online = (strieq(val, "ready") || strieq(val, "online"));
    }
}

/* 旧版 connman 不支持 `services --properties`, 用 `services <service>`
 * 逐个查询, 输出形如 "    Strength = 72", 复用 apply_property 解析。 */
static void wifi_fetch_service_properties(const char* service, wifi_item_t* item)
{
    char cmd[WIFI_SERVICE_PATH_LEN + 32];
    char out[2048];

    snprintf(cmd, sizeof(cmd), "connmanctl services %s 2>&1", service);
    out[0] = '\0';
    wifi_exec(cmd, NULL, out, sizeof(out), false);

    const char* p = out;
    while (*p) {
        const char* eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        char line[256];
        char* l;
        char* eq;
        char* key;
        char* val;
        size_t klen;

        if (len >= sizeof(line)) len = sizeof(line) - 1;
        memcpy(line, p, len);
        line[len] = '\0';
        p = eol ? (eol + 1) : (p + strlen(p));

        l = str_trim(line);
        eq = strchr(l, '=');
        if (!eq) continue;

        *eq = '\0';
        key = str_trim(l);
        val = str_trim(eq + 1);
        klen = strlen(key);
        if (klen >= 2 && key[0] == '{' && key[klen - 1] == '}') {
            key[klen - 1] = '\0';
            key++;
        }
        apply_property(item, key, val);
    }
}

static int wifi_parse_services(const char* text, wifi_item_t* items, int max,
                               bool* has_props)
{
    const char* p = text ? text : "";
    int  count = 0;
    wifi_item_t* cur = NULL;
    bool props_seen = false;

    while (*p) {
        const char* eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        char   line[512];
        char*  l;
        char*  eq;
        char*  last;
        char*  sp;

        if (len >= sizeof(line)) len = sizeof(line) - 1;
        memcpy(line, p, len);
        line[len] = '\0';
        p = eol ? (eol + 1) : (p + strlen(p));

        l = str_trim(line);
        if (l[0] == '\0') continue;

        /* 属性行 */
        eq = strchr(l, '=');
        if (eq && cur) {
            char key[64];
            char* val;
            size_t klen;

            *eq = '\0';
            val  = str_trim(eq + 1);
            l    = str_trim(l);
            klen = strlen(l);
            if (klen >= 2 && l[0] == '{' && l[klen - 1] == '}') {
                l[klen - 1] = '\0';
                l++;
            }
            snprintf(key, sizeof(key), "%s", str_trim(l));
            apply_property(cur, key, val);
            props_seen = true;
            continue;
        }

        /* 服务行: 最后一个空白分隔的单词是 service 路径 */
        last = NULL;
        for (sp = l; *sp; sp++) {
            if (*sp == ' ' || *sp == '\t') {
                char* n = sp;
                while (*n == ' ' || *n == '\t') n++;
                if (*n) last = n;
            }
        }
        if (!last || !is_wifi_service(last)) {
            cur = NULL;
            continue;
        }

        if (count >= max) { cur = NULL; continue; }

        cur = &items[count];
        memset(cur, 0, sizeof(*cur));
        cur->ap.strength = -1;
        snprintf(cur->ap.service, sizeof(cur->ap.service), "%s", last);
        cur->ap.security = security_from_service_path(cur->ap.service);
        if (strstr(cur->ap.service, "_hidden_") != NULL) cur->is_hidden = true;
        ssid_from_service_path(cur->ap.service, cur->ap.ssid, sizeof(cur->ap.ssid));

        *last = '\0';
        {
            char* name = str_trim(l);
            bool  fav = false, onl = false;
            strip_flags(name, &fav, &onl);
            name = str_trim(name);

            if (name[0] != '\0') {
                snprintf(cur->ap.ssid, sizeof(cur->ap.ssid), "%s", name);
            }
            if (strcmp(cur->ap.ssid, "hidden") == 0) cur->is_hidden = true;
            cur->ap.saved  = fav;
            cur->is_online = cur->is_online || onl;
        }

        count++;
    }

    if (has_props) *has_props = props_seen;
    return count;
}

/*---------------------------------------------------------------------------
 * 列表整理
 *--------------------------------------------------------------------------*/
static int ap_score(const wifi_ap_t* ap)
{
    int s = 0;
    if (ap->connected) s += 1000;
    if (ap->saved)     s += 500;
    s += (ap->strength < 0) ? 0 : ap->strength;
    return s;
}

static bool ap_list_equal(const wifi_ap_t* a, int an, const wifi_ap_t* b, int bn)
{
    if (an != bn) return false;
    for (int i = 0; i < an; i++) {
        if (strcmp(a[i].ssid, b[i].ssid) != 0) return false;
        if (strcmp(a[i].service, b[i].service) != 0) return false;
        if (a[i].strength != b[i].strength) return false;
        if (a[i].connected != b[i].connected) return false;
        if (a[i].saved != b[i].saved) return false;
        if (a[i].security != b[i].security) return false;
    }
    return true;
}

static void wifi_store_aps(const wifi_item_t* items, int n, bool has_props)
{
    wifi_ap_t list[WIFI_AP_MAX_COUNT];
    int cnt = 0;

    for (int i = 0; i < n; i++) {
        const wifi_item_t* it = &items[i];
        int found = -1;

        if (it->is_hidden || it->ap.ssid[0] == '\0') continue;

        /* 带属性时: 无 {Strength} 且非连接/收藏状态, 视为不在范围内 */
        if (has_props && !it->has_strength && !it->is_online && !it->ap.saved) continue;

#if WIFI_ONLY_2G4
        if (it->has_freq && it->ap.frequency > 2500) continue;
#endif

        for (int k = 0; k < cnt; k++) {
            if (strcmp(list[k].ssid, it->ap.ssid) == 0) { found = k; break; }
        }
        if (found >= 0) {
            wifi_ap_t merged = list[found];
            if (it->ap.strength > merged.strength) {
                merged.strength  = it->ap.strength;
                merged.frequency = it->ap.frequency;
                snprintf(merged.service, sizeof(merged.service), "%s", it->ap.service);
            }
            if (it->is_online) merged.connected = true;
            if (it->ap.saved)  merged.saved = true;
            list[found] = merged;
            continue;
        }

        if (cnt < WIFI_AP_MAX_COUNT) {
            list[cnt] = it->ap;
            list[cnt].connected = it->is_online;
            cnt++;
        }
    }

    /* 已连接 > 已保存 > 其它, 同级按信号强度 */
    for (int i = 1; i < cnt; i++) {
        wifi_ap_t key = list[i];
        int j = i - 1;
        while (j >= 0 && ap_score(&list[j]) < ap_score(&key)) {
            list[j + 1] = list[j];
            j--;
        }
        list[j + 1] = key;
    }

    pthread_mutex_lock(&s_lock);
    bool changed = !ap_list_equal(s_aps, s_ap_count, list, cnt);
    if (changed) {
        memcpy(s_aps, list, sizeof(wifi_ap_t) * (size_t)cnt);
        s_ap_count = cnt;
        s_ap_version++;

        s_conn_ssid[0] = '\0';
        s_conn_service[0] = '\0';
        for (int i = 0; i < cnt; i++) {
            if (list[i].connected) {
                snprintf(s_conn_ssid, sizeof(s_conn_ssid), "%s", list[i].ssid);
                snprintf(s_conn_service, sizeof(s_conn_service), "%s", list[i].service);
                break;
            }
        }
    }
    pthread_mutex_unlock(&s_lock);

    if (changed) {
        printf(WIFI_LOG "网络列表更新: %d 个网络\n", cnt);
        fflush(stdout);
    }
}

/*---------------------------------------------------------------------------
 * 列表刷新: 首次拿列表 + 逐个查询属性(带缓存)
 *--------------------------------------------------------------------------*/
static struct timespec s_last_detail_fetch = {0, 0};
static bool s_detail_cache_valid = false;

static void wifi_refresh_services(bool echo_out)
{
    char out[WIFI_CMD_OUT_MAX];
    wifi_item_t items[WIFI_AP_MAX_COUNT * 2];
    bool has_props = false;
    int  n;
    int  st;

    out[0] = '\0';
    st = wifi_exec("connmanctl services --properties 2>&1", NULL,
                   out, sizeof(out), false);

    if (st != 0 || out[0] == '\0' || strchr(out, '=') == NULL) {
        /* 旧版 connman 不支持 --properties, 回退到普通 services 列表 */
        out[0] = '\0';
        wifi_exec("connmanctl services 2>&1", NULL, out, sizeof(out), echo_out);
        has_props = false;
    }

    n = wifi_parse_services(out, items,
                            (int)(sizeof(items) / sizeof(items[0])), &has_props);

    if (!has_props && n > 0) {
        /* 逐个查询服务详情以拿到信号强度, 短时间内复用缓存 */
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed_ms = 0;
        if (s_detail_cache_valid) {
            elapsed_ms = (now.tv_sec  - s_last_detail_fetch.tv_sec ) * 1000L
                       + (now.tv_nsec - s_last_detail_fetch.tv_nsec) / 1000000L;
        }

        if (!s_detail_cache_valid || elapsed_ms >= WIFI_DETAIL_CACHE_MS) {
            for (int i = 0; i < n; i++) {
                wifi_fetch_service_properties(items[i].ap.service, &items[i]);
            }
            s_last_detail_fetch  = now;
            s_detail_cache_valid = true;
        } else {
            /* 缓存有效: 从上次结果里拷贝 strength */
            pthread_mutex_lock(&s_lock);
            for (int i = 0; i < n; i++) {
                for (int k = 0; k < s_ap_count; k++) {
                    if (strcmp(s_aps[k].service, items[i].ap.service) == 0) {
                        items[i].ap.strength = s_aps[k].strength;
                        items[i].has_strength = (s_aps[k].strength >= 0);
                        break;
                    }
                }
            }
            pthread_mutex_unlock(&s_lock);
        }

        /* 只有当确实拿到了强度信息时才启用"范围过滤" */
        bool any_strength = false;
        for (int i = 0; i < n; i++) {
            if (items[i].has_strength) { any_strength = true; break; }
        }
        has_props = any_strength;
    }

    wifi_store_aps(items, n, has_props);
}

/*---------------------------------------------------------------------------
 * 各项任务
 *--------------------------------------------------------------------------*/
static int wifi_do_enable(void)
{
    char out[1024];

    wifi_set_state(WIFI_STATE_ENABLING, "正在打开 Wi-Fi ...");

    out[0] = '\0';
    wifi_exec("connmanctl enable wifi 2>&1", NULL, out, sizeof(out), true);
    if (!strstr(out, "already enabled") &&
        !strstr(out, "Enabled")        &&
        !strstr(out, "enabled")        &&
        !strstr(out, "Success")) {
        /* 兜底: rfkill / ifconfig */
        wifi_exec("rfkill unblock wifi 2>/dev/null; "
                  "ifconfig wlan0 up 2>/dev/null; "
                  "connmanctl enable wifi 2>&1",
                  NULL, NULL, 0, true);
    }

    wifi_set_enabled(true);
    wifi_set_state(WIFI_STATE_IDLE, "Wi-Fi 已打开");
    return 0;
}

static void wifi_job_enable(void)
{
    wifi_do_enable();
}

static void wifi_job_disable(void)
{
    wifi_set_state(WIFI_STATE_DISABLING, "正在关闭 Wi-Fi ...");
    wifi_exec("connmanctl disable wifi 2>&1", NULL, NULL, 0, true);
    wifi_set_enabled(false);
    wifi_set_need_password(NULL);

    pthread_mutex_lock(&s_lock);
    if (s_ap_count > 0) {
        s_ap_count = 0;
        s_ap_version++;
    }
    s_conn_ssid[0] = '\0';
    s_conn_service[0] = '\0';
    pthread_mutex_unlock(&s_lock);

    s_detail_cache_valid = false;
    wifi_set_state(WIFI_STATE_IDLE, NULL);
}

static void wifi_job_scan(void)
{
    char txt[WIFI_STATUS_TEXT_LEN];

    if (!s_enabled) {
        if (wifi_do_enable() != 0) return;
    }

    wifi_set_need_password(NULL);
    wifi_set_state(WIFI_STATE_SCANNING, "正在搜索附近的网络 ...");

    wifi_exec("connmanctl scan wifi 2>&1", NULL, NULL, 0, true);

    for (int i = 0; i < WIFI_SCAN_POLL_TIMES; i++) {
        msleep(WIFI_SCAN_POLL_MS);
        wifi_refresh_services(false);
    }

    snprintf(txt, sizeof(txt), "已找到 %d 个可连接的网络", wifi_ap_count_snapshot());

    pthread_mutex_lock(&s_lock);
    bool connected = (s_conn_ssid[0] != '\0');
    pthread_mutex_unlock(&s_lock);

    wifi_set_state(connected ? WIFI_STATE_CONNECTED : WIFI_STATE_IDLE, txt);
}

static void wifi_job_disconnect(const char* service)
{
    char cmd[WIFI_SERVICE_PATH_LEN + 64];

    wifi_set_state(WIFI_STATE_IDLE, "正在断开 ...");
    snprintf(cmd, sizeof(cmd), "connmanctl disconnect %s 2>&1", service);
    wifi_exec(cmd, NULL, NULL, 0, true);

    pthread_mutex_lock(&s_lock);
    s_conn_ssid[0] = '\0';
    s_conn_service[0] = '\0';
    pthread_mutex_unlock(&s_lock);

    /* 断开是异步的, 多刷几次等 connman 把 R 标志摘掉 */
    for (int i = 0; i < 4; i++) {
        msleep(700);
        wifi_refresh_services(false);
    }

    pthread_mutex_lock(&s_lock);
    bool still_connected = (s_conn_ssid[0] != '\0');
    pthread_mutex_unlock(&s_lock);

    if (still_connected) {
        /* connman 可能因为 AutoConnect 又连上了 */
        wifi_set_state(WIFI_STATE_CONNECTED, "已重新连接");
    } else {
        wifi_set_state(WIFI_STATE_IDLE, "已断开连接");
    }
}

static bool wifi_service_is_ready(const char* service, char* ssid_out, size_t sz)
{
    char out[WIFI_CMD_OUT_MAX];
    wifi_item_t items[WIFI_AP_MAX_COUNT * 2];
    bool has_props = false;
    int  n;

    out[0] = '\0';
    wifi_exec("connmanctl services --properties 2>&1", NULL,
              out, sizeof(out), false);
    if (out[0] == '\0' || strchr(out, '=') == NULL) {
        out[0] = '\0';
        wifi_exec("connmanctl services 2>&1", NULL, out, sizeof(out), false);
        has_props = false;
    }

    n = wifi_parse_services(out, items,
                            (int)(sizeof(items) / sizeof(items[0])), &has_props);
    for (int i = 0; i < n; i++) {
        if (strcmp(items[i].ap.service, service) == 0) {
            if (ssid_out && sz) snprintf(ssid_out, sz, "%s", items[i].ap.ssid);
            return items[i].is_online;
        }
    }
    return false;
}

static void wifi_job_connect(const char* service, const char* pass)
{
    char cmd[WIFI_SERVICE_PATH_LEN + 320];
    char disp[WIFI_SERVICE_PATH_LEN + 320];
    char out[WIFI_CMD_OUT_MAX];
    char q1[32];
    char q2[WIFI_SERVICE_PATH_LEN + 32];
    char q3[WIFI_PASSPHRASE_MAX_LEN * 4 + 8];
    char connect_args[WIFI_SERVICE_PATH_LEN + 32];
    char ssid[WIFI_SSID_MAX_LEN] = "";
    char txt[WIFI_STATUS_TEXT_LEN];
    bool ok = false;

    wifi_set_need_password(NULL);
    wifi_set_state(WIFI_STATE_CONNECTING, "正在连接 ...");

    snprintf(connect_args, sizeof(connect_args), "connect %s", service);
    shell_quote("agent on", q1, sizeof(q1));
    shell_quote(connect_args, q2, sizeof(q2));

    if (pass && pass[0]) {
        shell_quote(pass, q3, sizeof(q3));
        snprintf(cmd, sizeof(cmd), "printf '%%s\\n' %s %s %s | connmanctl 2>&1",
                 q1, q2, q3);
#if WIFI_LOG_PASSWORD
        snprintf(disp, sizeof(disp), "%s", cmd);
#else
        snprintf(disp, sizeof(disp),
                 "printf '%%s\\n' %s %s '******' | connmanctl 2>&1", q1, q2);
#endif
    } else {
        snprintf(cmd,  sizeof(cmd),  "printf '%%s\\n' %s %s | connmanctl 2>&1", q1, q2);
        snprintf(disp, sizeof(disp), "%s", cmd);
    }

    out[0] = '\0';
    wifi_exec(cmd, disp, out, sizeof(out), true);

    if (!ok && (strstr(out, "Passphrase") || strstr(out, "RequestInput") ||
                strstr(out, "mandatory"))) {
        if (!pass || !pass[0]) {
            printf(WIFI_LOG "该网络需要密码, 等待用户输入\n");
            fflush(stdout);
            wifi_set_need_password(service);
            wifi_set_state(WIFI_STATE_ERROR, "该网络需要密码");
            return;
        }
    }

    if (strstr(out, "Error") || strstr(out, "error") ||
        strstr(out, "Failed") || strstr(out, "failed") ||
        strstr(out, "Invalid") || strstr(out, "invalid")) {
        printf(WIFI_LOG "connect 命令返回了错误信息\n");
        fflush(stdout);
    }

    for (int i = 0; i < WIFI_CONNECT_POLL_TIMES; i++) {
        msleep(WIFI_CONNECT_POLL_MS);
        if (wifi_service_is_ready(service, ssid, sizeof(ssid))) {
            ok = true;
            break;
        }
    }

    /* 让 UI 拿到最新的列表(含 connected 状态) */
    s_detail_cache_valid = false;
    wifi_refresh_services(false);

    if (ok) {
        snprintf(txt, sizeof(txt), "已连接 %s", ssid[0] ? ssid : service);
        wifi_set_state(WIFI_STATE_CONNECTED, txt);
    } else {
        wifi_set_state(WIFI_STATE_ERROR, "连接失败, 请检查密码后重试");
    }
}

/*---------------------------------------------------------------------------
 * 工作线程
 *--------------------------------------------------------------------------*/
static void* wifi_worker(void* arg)
{
    (void)arg;

    printf(WIFI_LOG "Wi-Fi 服务工作线程已启动 (backend: connmanctl)\n");
    fflush(stdout);

    while (1) {
        wifi_job_t job;

        pthread_mutex_lock(&s_lock);
        while (s_q_count == 0 && s_thread_run) {
            pthread_cond_wait(&s_cond, &s_lock);
        }
        if (!s_thread_run && s_q_count == 0) {
            pthread_mutex_unlock(&s_lock);
            break;
        }
        job = s_queue[s_q_head];
        s_q_head = (s_q_head + 1) % WIFI_JOB_QUEUE_LEN;
        s_q_count--;
        pthread_mutex_unlock(&s_lock);

        switch (job.id) {
        case JOB_ENABLE:     wifi_job_enable(); break;
        case JOB_DISABLE:    wifi_job_disable(); break;
        case JOB_SCAN:       wifi_job_scan(); break;
        case JOB_CONNECT:    wifi_job_connect(job.service, job.pass); break;
        case JOB_DISCONNECT: wifi_job_disconnect(job.service); break;
        default: break;
        }
    }

    printf(WIFI_LOG "Wi-Fi 服务工作线程已退出\n");
    fflush(stdout);
    return NULL;
}

static int wifi_push_job(wifi_job_id_t id, const char* service, const char* pass)
{
    if (!s_inited) return -1;

    pthread_mutex_lock(&s_lock);
    if (s_q_count >= WIFI_JOB_QUEUE_LEN) {
        pthread_mutex_unlock(&s_lock);
        printf(WIFI_LOG "命令队列已满, 丢弃本次请求\n");
        fflush(stdout);
        return -1;
    }
    memset(&s_queue[s_q_tail], 0, sizeof(s_queue[s_q_tail]));
    s_queue[s_q_tail].id = id;
    if (service) {
        snprintf(s_queue[s_q_tail].service, sizeof(s_queue[s_q_tail].service),
                 "%s", service);
    }
    if (pass) {
        snprintf(s_queue[s_q_tail].pass, sizeof(s_queue[s_q_tail].pass),
                 "%s", pass);
    }
    s_q_tail = (s_q_tail + 1) % WIFI_JOB_QUEUE_LEN;
    s_q_count++;
    pthread_cond_signal(&s_cond);
    pthread_mutex_unlock(&s_lock);
    return 0;
}

/*---------------------------------------------------------------------------
 * 对外接口
 *--------------------------------------------------------------------------*/
int wifi_service_init(void)
{
    if (s_inited) return 0;

    s_q_head = s_q_tail = s_q_count = 0;
    s_ap_count = 0;
    s_ap_version = 0;
    s_thread_run = true;

    if (pthread_create(&s_thread, NULL, wifi_worker, NULL) != 0) {
        printf(WIFI_LOG "创建线程失败: %s\n", strerror(errno));
        fflush(stdout);
        s_thread_run = false;
        return -1;
    }
    s_inited = true;
    return 0;
}

void wifi_service_deinit(void)
{
    if (!s_inited) return;
    pthread_mutex_lock(&s_lock);
    s_thread_run = false;
    pthread_cond_signal(&s_cond);
    pthread_mutex_unlock(&s_lock);
    pthread_join(s_thread, NULL);
    s_inited = false;
}

int  wifi_service_enable(void)        { return wifi_push_job(JOB_ENABLE, NULL, NULL); }
int  wifi_service_disable(void)       { return wifi_push_job(JOB_DISABLE, NULL, NULL); }
int  wifi_service_request_scan(void)  { return wifi_push_job(JOB_SCAN, NULL, NULL); }

bool wifi_service_is_enabled(void)
{
    bool en;
    pthread_mutex_lock(&s_lock);
    en = s_enabled;
    pthread_mutex_unlock(&s_lock);
    return en;
}

int wifi_service_get_ap_list(wifi_ap_t* out, int max)
{
    int n;
    if (!out || max <= 0) return 0;
    pthread_mutex_lock(&s_lock);
    n = (s_ap_count < max) ? s_ap_count : max;
    memcpy(out, s_aps, sizeof(wifi_ap_t) * (size_t)n);
    pthread_mutex_unlock(&s_lock);
    return n;
}

int wifi_service_ap_version(void)
{
    int v;
    pthread_mutex_lock(&s_lock);
    v = s_ap_version;
    pthread_mutex_unlock(&s_lock);
    return v;
}

int wifi_service_find_ap(const char* service, wifi_ap_t* out)
{
    int ret = -1;
    if (!service) return -1;
    pthread_mutex_lock(&s_lock);
    for (int i = 0; i < s_ap_count; i++) {
        if (strcmp(s_aps[i].service, service) == 0) {
            if (out) *out = s_aps[i];
            ret = 0;
            break;
        }
    }
    pthread_mutex_unlock(&s_lock);
    return ret;
}

int wifi_service_connect(const char* service, const char* passphrase)
{
    if (!is_wifi_service(service)) return -1;
    return wifi_push_job(JOB_CONNECT, service, passphrase);
}

int wifi_service_disconnect(const char* service)
{
    if (!is_wifi_service(service)) return -1;
    return wifi_push_job(JOB_DISCONNECT, service, NULL);
}

bool wifi_service_need_password(char* service, size_t sz)
{
    bool need;
    pthread_mutex_lock(&s_lock);
    need = s_need_pwd;
    if (need && service && sz) {
        snprintf(service, sz, "%s", s_need_pwd_service);
    }
    pthread_mutex_unlock(&s_lock);
    return need;
}

void wifi_service_clear_need_password(void)
{
    wifi_set_need_password(NULL);
}

wifi_state_t wifi_service_get_state(void)
{
    wifi_state_t st;
    pthread_mutex_lock(&s_lock);
    st = s_state;
    pthread_mutex_unlock(&s_lock);
    return st;
}

const char* wifi_service_get_status_text(char* buf, size_t sz)
{
    if (!buf || sz == 0) return "";
    pthread_mutex_lock(&s_lock);
    snprintf(buf, sz, "%s", s_status);
    pthread_mutex_unlock(&s_lock);
    return buf;
}

const char* wifi_service_get_connected_ssid(void)
{
    static char buf[WIFI_SSID_MAX_LEN];
    pthread_mutex_lock(&s_lock);
    snprintf(buf, sizeof(buf), "%s", s_conn_ssid);
    pthread_mutex_unlock(&s_lock);
    return buf[0] ? buf : NULL;
}

const char* wifi_sec_to_string(uint8_t sec)
{
    switch (sec) {
    case WIFI_SEC_NONE:  return "none";
    case WIFI_SEC_WEP:   return "wep";
    case WIFI_SEC_PSK:   return "psk";
    case WIFI_SEC_8021X: return "ieee8021x";
    default:             return "unknown";
    }
}
