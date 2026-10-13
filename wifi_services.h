/**
 * @file    wifi_services.h
 * @brief   基于 connman(connmanctl) 的 Wi-Fi 服务层
 *
 * 目标平台 : 正点原子 i.MX6U Linux + LVGL8
 * 无线模块 : USB Wi-Fi (RTL8188EUS/FU 等), 仅支持 2.4GHz
 *
 * 本模块不依赖 LVGL, 所有 connmanctl 命令在后台线程执行,
 * UI 线程通过 wifi_service_get_ap_list() / wifi_service_ap_version()
 * 轮询结果, 版本号变化时才重建列表。
 */
#ifndef WIFI_SERVICES_H
#define WIFI_SERVICES_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*---------------------------------------------------------------------------
 * 宏定义
 *--------------------------------------------------------------------------*/
#define WIFI_SSID_MAX_LEN        33
#define WIFI_PASSPHRASE_MAX_LEN  64
#define WIFI_SERVICE_PATH_LEN    128
#define WIFI_AP_MAX_COUNT        48
#define WIFI_STATUS_TEXT_LEN     128

/*---------------------------------------------------------------------------
 * 数据类型
 *--------------------------------------------------------------------------*/
typedef enum {
    WIFI_SEC_UNKNOWN = 0,
    WIFI_SEC_NONE,
    WIFI_SEC_WEP,
    WIFI_SEC_PSK,
    WIFI_SEC_8021X
} wifi_sec_t;

typedef enum {
    WIFI_STATE_IDLE = 0,
    WIFI_STATE_ENABLING,
    WIFI_STATE_DISABLING,
    WIFI_STATE_SCANNING,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_ERROR
} wifi_state_t;

typedef struct {
    char     ssid[WIFI_SSID_MAX_LEN];
    char     service[WIFI_SERVICE_PATH_LEN];
    int8_t   strength;      /* 0~100, -1 表示未知 */
    uint16_t frequency;     /* MHz, 0 表示未知 */
    uint8_t  security;      /* wifi_sec_t */
    bool     saved;         /* 已保存的网络 */
    bool     connected;     /* 已连接 */
} wifi_ap_t;

/*---------------------------------------------------------------------------
 * 生命周期
 *--------------------------------------------------------------------------*/
int  wifi_service_init(void);
void wifi_service_deinit(void);

/*---------------------------------------------------------------------------
 * 射频开关
 *--------------------------------------------------------------------------*/
int  wifi_service_enable(void);
int  wifi_service_disable(void);
bool wifi_service_is_enabled(void);

/*---------------------------------------------------------------------------
 * 扫描与列表
 *--------------------------------------------------------------------------*/
int  wifi_service_request_scan(void);
int  wifi_service_get_ap_list(wifi_ap_t* out, int max);
int  wifi_service_ap_version(void);
int  wifi_service_find_ap(const char* service, wifi_ap_t* out);

/*---------------------------------------------------------------------------
 * 连接 / 断开
 *--------------------------------------------------------------------------*/
int  wifi_service_connect(const char* service, const char* passphrase);
int  wifi_service_disconnect(const char* service);

bool wifi_service_need_password(char* service, size_t sz);
void wifi_service_clear_need_password(void);

/*---------------------------------------------------------------------------
 * 状态查询
 *--------------------------------------------------------------------------*/
wifi_state_t wifi_service_get_state(void);
const char*  wifi_service_get_status_text(char* buf, size_t sz);
const char*  wifi_service_get_connected_ssid(void);
const char*  wifi_sec_to_string(uint8_t sec);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_SERVICES_H */
