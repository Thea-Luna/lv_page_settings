#include "page_settings.h"
#include "wifi_services.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*---------------------------------------------------------------------------
 * 菜单项构造变体(与 lv_menu_cont 布局相关, 原工程使用)
 *--------------------------------------------------------------------------*/
enum {
    LV_MENU_ITEM_BUILDER_VARIANT_1,
    LV_MENU_ITEM_BUILDER_VARIANT_2
};
typedef uint8_t lv_menu_builder_variant_t;

/*---------------------------------------------------------------------------
 * Wi-Fi 页面文案 (0 = 英文, 1 = 中文)
 *--------------------------------------------------------------------------*/
#ifndef WIFI_UI_USE_CHINESE
#define WIFI_UI_USE_CHINESE     0
#endif

#ifndef WIFI_UI_FONT
#define WIFI_UI_FONT            (&lv_font_montserrat_20)
#endif

#if WIFI_UI_USE_CHINESE
#define WIFI_TXT_TITLE          "无线局域网"
#define WIFI_TXT_MY_NETWORKS    "我的网络"
#define WIFI_TXT_OTHER_NETWORKS "其他网络"
#define WIFI_TXT_NO_NETWORK     "未搜索到可连接的网络"
#define WIFI_TXT_NEED_SWITCH    "打开上面的开关开始搜索网络"
#define WIFI_TXT_PASSWORD       "请输入密码"
#define WIFI_TXT_PLACEHOLDER    "网络密码"
#define WIFI_TXT_CONNECT        "连接"
#define WIFI_TXT_CANCEL         "取消"
#define WIFI_TXT_CLOSE          "关闭"
#define WIFI_TXT_DISCONNECT     "断开连接"
#define WIFI_TXT_INFO           "网络信息"
#define WIFI_TXT_ENT_TITLE      "提示"
#define WIFI_TXT_ENT_TEXT       "该网络为 802.1x 企业级认证, 暂不支持"
#define WIFI_TXT_CONNECTED      "已连接"
#define WIFI_TXT_SAVED          "已保存"
#else
#define WIFI_TXT_TITLE          "Wi-Fi"
#define WIFI_TXT_MY_NETWORKS    "My Networks"
#define WIFI_TXT_OTHER_NETWORKS "Other Networks"
#define WIFI_TXT_NO_NETWORK     "No connectable network found"
#define WIFI_TXT_NEED_SWITCH    "Turn on the switch above to scan"
#define WIFI_TXT_PASSWORD       "Enter password"
#define WIFI_TXT_PLACEHOLDER    "Password"
#define WIFI_TXT_CONNECT        "Connect"
#define WIFI_TXT_CANCEL         "Cancel"
#define WIFI_TXT_CLOSE          "Close"
#define WIFI_TXT_DISCONNECT     "Ignore"
#define WIFI_TXT_INFO           "Network info"
#define WIFI_TXT_ENT_TITLE      "Notice"
#define WIFI_TXT_ENT_TEXT       "802.1x enterprise network is not supported yet"
#define WIFI_TXT_CONNECTED      "Connected"
#define WIFI_TXT_SAVED          "Saved"
#endif

/* 颜色 / 尺寸 */
#define WIFI_UI_COLOR_BG        lv_color_hex(0xEEF2F6)
#define WIFI_UI_COLOR_SEP       lv_color_hex(0xDEDEDE)
#define WIFI_UI_COLOR_BLUE      lv_color_hex(0x4AA1FF)
#define WIFI_UI_COLOR_TEXT      lv_color_hex(0x1C1C1E)
#define WIFI_UI_COLOR_GRAY      lv_color_hex(0x6D7278)
#define WIFI_UI_COLOR_PRESS     lv_color_hex(0xE4EFFC)
#define WIFI_UI_ROW_H           50
#define WIFI_UI_CARD_RADIUS     10
#define WIFI_UI_SWITCH_W        52
#define WIFI_UI_SWITCH_H        32
#define WIFI_UI_KB_H            240
#define WIFI_UI_TIMER_MS        100

/*---------------------------------------------------------------------------
 * Wi-Fi 页面内部状态
 *--------------------------------------------------------------------------*/
static lv_obj_t*    g_wifi_switch       = NULL;
static lv_obj_t*    g_wifi_list_cont    = NULL;
static lv_obj_t*    g_wifi_status_label = NULL;
static lv_timer_t*  g_wifi_timer        = NULL;
static int          g_wifi_ap_version   = -1;
static lv_obj_t*    g_wifi_info_box     = NULL;
static char         g_wifi_info_service[WIFI_SERVICE_PATH_LEN] = "";
static uint32_t     g_wifi_rescan_tick   = 0;
static bool         g_wifi_rescan_queued = false;

#if LV_USE_KEYBOARD && LV_USE_TEXTAREA
static lv_obj_t*    g_wifi_pwd_box      = NULL;
static lv_obj_t*    g_wifi_pwd_ta       = NULL;
static lv_obj_t*    g_wifi_kb           = NULL;
static char         g_wifi_pwd_service[WIFI_SERVICE_PATH_LEN] = "";
static char         g_wifi_pwd_ssid[WIFI_SSID_MAX_LEN] = "";
#endif

/*---------------------------------------------------------------------------
 * Wi-Fi 页面基础控件
 *--------------------------------------------------------------------------*/
static lv_obj_t* wifi_ui_create_card(lv_obj_t* parent)
{
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, WIFI_UI_CARD_RADIUS, 0);
    lv_obj_set_style_clip_corner(card, true, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_style_pad_row(card, 0, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

static lv_obj_t* wifi_ui_create_section_title(lv_obj_t* parent, const char* txt)
{
    lv_obj_t* box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_width(box, lv_pct(100));
    lv_obj_set_height(box, WIFI_UI_ROW_H);
    lv_obj_set_style_bg_color(box, WIFI_UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* label = lv_label_create(box);
    lv_label_set_text(label, txt);
    lv_obj_set_style_text_font(label, WIFI_UI_FONT, 0);
    lv_obj_set_style_text_color(label, WIFI_UI_COLOR_GRAY, 0);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 17, 0);
    return box;
}

/* 用小方框拼出一把锁, 作为加密标志 */
static lv_obj_t* wifi_ui_create_lock(lv_obj_t* parent)
{
    lv_obj_t* box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, 16, 20);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* shackle = lv_obj_create(box);
    lv_obj_remove_style_all(shackle);
    lv_obj_set_size(shackle, 10, 10);
    lv_obj_align(shackle, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(shackle, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(shackle, 2, 0);
    lv_obj_set_style_border_color(shackle, WIFI_UI_COLOR_TEXT, 0);
    lv_obj_set_style_border_side(shackle,
        LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_radius(shackle, 5, 0);

    lv_obj_t* body = lv_obj_create(box);
    lv_obj_remove_style_all(body);
    lv_obj_set_size(body, 14, 11);
    lv_obj_align(body, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(body, WIFI_UI_COLOR_TEXT, 0);
    lv_obj_set_style_bg_opa(body, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(body, 3, 0);

    return box;
}

static lv_opa_t wifi_ui_signal_opa(int8_t strength)
{
    if (strength < 0)   return LV_OPA_50;
    if (strength >= 75) return LV_OPA_COVER;
    if (strength >= 50) return LV_OPA_70;
    if (strength >= 25) return LV_OPA_50;
    return LV_OPA_30;
}

static lv_obj_t* wifi_ui_create_signal(lv_obj_t* parent, int8_t strength)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(label, WIFI_UI_FONT, 0);
    lv_obj_set_style_text_color(label, WIFI_UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_opa(label, wifi_ui_signal_opa(strength), 0);
    return label;
}

/*---------------------------------------------------------------------------
 * Wi-Fi 页面事件回调
 *--------------------------------------------------------------------------*/
static void wifi_ui_row_delete_cb(lv_event_t* e)
{
    void* p = lv_event_get_user_data(e);
    if (p) free(p);
}

static void wifi_ui_info_open(const wifi_ap_t* ap);
#if LV_USE_KEYBOARD && LV_USE_TEXTAREA
static void wifi_ui_password_open(const wifi_ap_t* ap);
#endif

static void wifi_ui_msgbox(const char* title, const char* text)
{
    static const char* btns[] = { "OK", "" };
    lv_obj_t* mbox = lv_msgbox_create(NULL, title, text, btns, true);
    lv_obj_set_style_text_font(mbox, WIFI_UI_FONT, 0);
    lv_obj_center(mbox);
}

static void wifi_ui_row_event_cb(lv_event_t* e)
{
    const char* service = (const char*)lv_event_get_user_data(e);
    wifi_ap_t   ap;

    if (!service) return;
    if (wifi_service_find_ap(service, &ap) != 0) return;

    if (ap.connected) {
        wifi_ui_info_open(&ap);
        return;
    }

    if (ap.security == WIFI_SEC_8021X) {
        wifi_ui_msgbox(WIFI_TXT_ENT_TITLE, WIFI_TXT_ENT_TEXT);
        return;
    }

#if LV_USE_KEYBOARD && LV_USE_TEXTAREA
    if (ap.security != WIFI_SEC_NONE && !ap.saved) {
        wifi_ui_password_open(&ap);
        return;
    }
#endif

    wifi_service_connect(ap.service, NULL);
}

static void wifi_ui_info_event_cb(lv_event_t* e)
{
    const char* service = (const char*)lv_event_get_user_data(e);
    wifi_ap_t   ap;

    if (!service) return;
    if (wifi_service_find_ap(service, &ap) != 0) return;
    wifi_ui_info_open(&ap);
}

static void wifi_ui_info_deleted_cb(lv_event_t* e)
{
    (void)e;
    g_wifi_info_box = NULL;
}

static void wifi_ui_info_btn_event_cb(lv_event_t* e)
{
    lv_obj_t* mbox = lv_event_get_current_target(e);

    if (lv_msgbox_get_active_btn(mbox) == 0 && g_wifi_info_service[0]) {
        wifi_service_disconnect(g_wifi_info_service);
    }
    g_wifi_info_service[0] = '\0';
}

static void wifi_ui_info_open(const wifi_ap_t* ap)
{
    static const char* btns_conn[]  = { WIFI_TXT_DISCONNECT, WIFI_TXT_CLOSE, "" };
    static const char* btns_plain[] = { WIFI_TXT_CLOSE, "" };
    char txt[384];

    if (g_wifi_info_box) {
        lv_obj_del(g_wifi_info_box);
        g_wifi_info_box = NULL;
    }

    snprintf(g_wifi_info_service, sizeof(g_wifi_info_service), "%s", ap->service);

    snprintf(txt, sizeof(txt),
             "SSID : %s\nSecurity : %s\nSignal : %d%%\nState : %s\nPath : %s",
             ap->ssid,
             wifi_sec_to_string(ap->security),
             ap->strength < 0 ? 0 : ap->strength,
             ap->connected ? WIFI_TXT_CONNECTED : (ap->saved ? WIFI_TXT_SAVED : "-"),
             ap->service);

    g_wifi_info_box = lv_msgbox_create(NULL, WIFI_TXT_INFO, txt,
                                       ap->connected ? btns_conn : btns_plain, true);
    lv_obj_set_style_text_font(g_wifi_info_box, WIFI_UI_FONT, 0);
    lv_obj_set_width(g_wifi_info_box, lv_pct(75));
    lv_obj_center(g_wifi_info_box);
    lv_obj_add_event_cb(g_wifi_info_box, wifi_ui_info_deleted_cb, LV_EVENT_DELETE, NULL);

    if (ap->connected) {
        lv_obj_add_event_cb(g_wifi_info_box, wifi_ui_info_btn_event_cb,
                            LV_EVENT_VALUE_CHANGED, NULL);
    }
}

/*---------------------------------------------------------------------------
 * Wi-Fi 密码输入
 *--------------------------------------------------------------------------*/
#if LV_USE_KEYBOARD && LV_USE_TEXTAREA
static void wifi_ui_password_close(void)
{
    if (g_wifi_pwd_box) {
        lv_obj_del(g_wifi_pwd_box);
        g_wifi_pwd_box = NULL;
    }
    g_wifi_pwd_ta = NULL;
    g_wifi_kb     = NULL;
    g_wifi_pwd_service[0] = '\0';
    g_wifi_pwd_ssid[0]    = '\0';
}

static void wifi_ui_password_confirm(void)
{
    char service[WIFI_SERVICE_PATH_LEN];
    const char* pwd = g_wifi_pwd_ta ? lv_textarea_get_text(g_wifi_pwd_ta) : "";

    strncpy(service, g_wifi_pwd_service, sizeof(service) - 1);
    service[sizeof(service) - 1] = '\0';

    if (service[0]) {
        wifi_service_connect(service, pwd);
    }
    wifi_ui_password_close();
}

static void wifi_ui_pwd_btn_event_cb(lv_event_t* e)
{
    uintptr_t user = (uintptr_t)lv_event_get_user_data(e);
    if (user == 1) wifi_ui_password_confirm();
    else           wifi_ui_password_close();
}

static void wifi_ui_kb_event_cb(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY)        wifi_ui_password_confirm();
    else if (code == LV_EVENT_CANCEL)  wifi_ui_password_close();
}

static lv_obj_t* wifi_ui_create_dialog_btn(lv_obj_t* parent, const char* txt,
                                           bool primary, uintptr_t user)
{
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 150, 44);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    if (primary) {
        lv_obj_set_style_bg_color(btn, WIFI_UI_COLOR_BLUE, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    } else {
        lv_obj_set_style_bg_color(btn, WIFI_UI_COLOR_BG, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    }
    lv_obj_add_event_cb(btn, wifi_ui_pwd_btn_event_cb, LV_EVENT_CLICKED, (void*)user);

    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, txt);
    lv_obj_set_style_text_font(label, WIFI_UI_FONT, 0);
    lv_obj_set_style_text_color(label, primary ? lv_color_white() : WIFI_UI_COLOR_TEXT, 0);
    lv_obj_center(label);
    return btn;
}

static void wifi_ui_password_open(const wifi_ap_t* ap)
{
    lv_obj_t* card;
    lv_obj_t* title;
    lv_obj_t* hint;

    wifi_ui_password_close();
    wifi_service_clear_need_password();

    strncpy(g_wifi_pwd_service, ap->service, sizeof(g_wifi_pwd_service) - 1);
    strncpy(g_wifi_pwd_ssid,    ap->ssid,    sizeof(g_wifi_pwd_ssid) - 1);
    g_wifi_pwd_service[sizeof(g_wifi_pwd_service) - 1] = '\0';
    g_wifi_pwd_ssid[sizeof(g_wifi_pwd_ssid) - 1]       = '\0';

    g_wifi_pwd_box = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g_wifi_pwd_box);
    lv_obj_set_size(g_wifi_pwd_box, lv_pct(100), lv_pct(100));
    lv_obj_center(g_wifi_pwd_box);
    lv_obj_set_style_bg_color(g_wifi_pwd_box, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_wifi_pwd_box, LV_OPA_40, 0);
    lv_obj_add_flag(g_wifi_pwd_box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(g_wifi_pwd_box, LV_OBJ_FLAG_SCROLLABLE);

    card = lv_obj_create(g_wifi_pwd_box);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 480, 200);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_set_style_bg_color(card, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, WIFI_UI_CARD_RADIUS, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    title = lv_label_create(card);
    lv_label_set_text(title, g_wifi_pwd_ssid);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title, lv_pct(90));
    lv_obj_set_style_text_font(title, WIFI_UI_FONT, 0);
    lv_obj_set_style_text_color(title, WIFI_UI_COLOR_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

    hint = lv_label_create(card);
    lv_label_set_text(hint, WIFI_TXT_PASSWORD);
    lv_obj_set_style_text_font(hint, WIFI_UI_FONT, 0);
    lv_obj_set_style_text_color(hint, WIFI_UI_COLOR_GRAY, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_LEFT, 24, 56);

    g_wifi_pwd_ta = lv_textarea_create(card);
    lv_textarea_set_one_line(g_wifi_pwd_ta, true);
    lv_textarea_set_password_mode(g_wifi_pwd_ta, true);
    lv_textarea_set_max_length(g_wifi_pwd_ta, WIFI_PASSPHRASE_MAX_LEN - 1);
    lv_textarea_set_placeholder_text(g_wifi_pwd_ta, WIFI_TXT_PLACEHOLDER);
    lv_obj_set_width(g_wifi_pwd_ta, lv_pct(88));
    lv_obj_set_style_text_font(g_wifi_pwd_ta, WIFI_UI_FONT, 0);
    lv_obj_align(g_wifi_pwd_ta, LV_ALIGN_TOP_MID, 0, 88);

    {
        lv_obj_t* btn_cancel = wifi_ui_create_dialog_btn(card, WIFI_TXT_CANCEL, false, 0);
        lv_obj_t* btn_ok     = wifi_ui_create_dialog_btn(card, WIFI_TXT_CONNECT, true, 1);
        lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_MID, -90, -18);
        lv_obj_align(btn_ok,     LV_ALIGN_BOTTOM_MID,  90, -18);
    }

    g_wifi_kb = lv_keyboard_create(g_wifi_pwd_box);
    lv_obj_set_size(g_wifi_kb, lv_pct(100), WIFI_UI_KB_H);
    lv_obj_align(g_wifi_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(g_wifi_kb, g_wifi_pwd_ta);
    lv_keyboard_set_mode(g_wifi_kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_add_event_cb(g_wifi_kb, wifi_ui_kb_event_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(g_wifi_kb, wifi_ui_kb_event_cb, LV_EVENT_CANCEL, NULL);
}
#endif /* LV_USE_KEYBOARD && LV_USE_TEXTAREA */

/*---------------------------------------------------------------------------
 * Wi-Fi 列表构建
 *--------------------------------------------------------------------------*/
static lv_obj_t* wifi_ui_create_row(lv_obj_t* parent, const wifi_ap_t* ap,
                                    bool last_in_section)
{
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, WIFI_UI_ROW_H);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(row, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(row, WIFI_UI_COLOR_PRESS, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(row, WIFI_UI_COLOR_SEP, 0);
    lv_obj_set_style_border_width(row, last_in_section ? 0 : 1, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_pad_top(row, 0, 0);
    lv_obj_set_style_pad_bottom(row, 0, 0);
    lv_obj_set_style_pad_right(row, 16, 0);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    if (ap->connected) {
        lv_obj_t* icon = lv_label_create(row);
        lv_label_set_text(icon, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_font(icon, WIFI_UI_FONT, 0);
        lv_obj_set_style_text_color(icon, WIFI_UI_COLOR_BLUE, 0);
        lv_obj_set_style_pad_left(row, 16, 0);
    } else {
        lv_obj_set_style_pad_left(row, 48, 0);
    }

    {
        lv_obj_t* name = lv_label_create(row);
        lv_label_set_text(name, ap->ssid);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_flex_grow(name, 1);
        lv_obj_set_style_text_font(name, WIFI_UI_FONT, 0);
        lv_obj_set_style_text_color(name, WIFI_UI_COLOR_TEXT, 0);
    }

    if (ap->security != WIFI_SEC_NONE) {
        wifi_ui_create_lock(row);
    }

    wifi_ui_create_signal(row, ap->strength);

    {
        lv_obj_t* info = lv_obj_create(row);
        lv_obj_remove_style_all(info);
        lv_obj_set_size(info, 26, 26);
        lv_obj_clear_flag(info, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(info, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(info, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(info, 2, 0);
        lv_obj_set_style_border_color(info, WIFI_UI_COLOR_BLUE, 0);
        lv_obj_set_style_bg_opa(info, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(info, WIFI_UI_COLOR_BLUE, LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(info, LV_OPA_30, LV_STATE_PRESSED);

        lv_obj_t* i = lv_label_create(info);
        lv_label_set_text(i, "i");
        lv_obj_set_style_text_font(i, WIFI_UI_FONT, 0);
        lv_obj_set_style_text_color(i, WIFI_UI_COLOR_BLUE, 0);
        lv_obj_center(i);

        char* svc = (char*)malloc(WIFI_SERVICE_PATH_LEN);
        if (svc) {
            snprintf(svc, WIFI_SERVICE_PATH_LEN, "%s", ap->service);
            lv_obj_add_event_cb(info, wifi_ui_info_event_cb, LV_EVENT_CLICKED, svc);
            lv_obj_add_event_cb(info, wifi_ui_row_delete_cb, LV_EVENT_DELETE, svc);
        }
    }

    {
        char* svc = (char*)malloc(WIFI_SERVICE_PATH_LEN);
        if (svc) {
            snprintf(svc, WIFI_SERVICE_PATH_LEN, "%s", ap->service);
            lv_obj_add_event_cb(row, wifi_ui_row_event_cb, LV_EVENT_CLICKED, svc);
            lv_obj_add_event_cb(row, wifi_ui_row_delete_cb, LV_EVENT_DELETE, svc);
        }
    }

    return row;
}

static void wifi_ui_build_list(void)
{
    wifi_ap_t aps[WIFI_AP_MAX_COUNT];
    int  n;
    int  i;
    bool header_done;
    lv_obj_t* card;

    if (!g_wifi_list_cont) return;
    lv_obj_clean(g_wifi_list_cont);

    n = wifi_service_get_ap_list(aps, WIFI_AP_MAX_COUNT);

    if (n <= 0) {
        lv_obj_t* hint = lv_label_create(g_wifi_list_cont);
        lv_label_set_text(hint, WIFI_TXT_NO_NETWORK);
        lv_obj_set_style_text_font(hint, WIFI_UI_FONT, 0);
        lv_obj_set_style_text_color(hint, WIFI_UI_COLOR_GRAY, 0);
        lv_obj_set_style_pad_top(hint, 24, 0);
        lv_obj_set_style_pad_bottom(hint, 24, 0);
        lv_obj_center(hint);
        return;
    }

    /* 已连接的网络 */
    for (i = 0; i < n; i++) {
        if (aps[i].connected) {
            card = wifi_ui_create_card(g_wifi_list_cont);
            wifi_ui_create_row(card, &aps[i], true);
            break;
        }
    }

    /* 我的网络(已保存) */
    header_done = false;
    for (i = 0; i < n; i++) {
        if (aps[i].connected || !aps[i].saved) continue;
        if (!header_done) {
            wifi_ui_create_section_title(g_wifi_list_cont, WIFI_TXT_MY_NETWORKS);
            card = wifi_ui_create_card(g_wifi_list_cont);
            header_done = true;
        }
        bool last = true;
        for (int k = i + 1; k < n; k++) {
            if (!aps[k].connected && aps[k].saved) { last = false; break; }
        }
        wifi_ui_create_row(card, &aps[i], last);
    }

    /* 其他网络 */
    header_done = false;
    for (i = 0; i < n; i++) {
        if (aps[i].connected || aps[i].saved) continue;
        if (!header_done) {
            wifi_ui_create_section_title(g_wifi_list_cont, WIFI_TXT_OTHER_NETWORKS);
            card = wifi_ui_create_card(g_wifi_list_cont);
            header_done = true;
        }
        bool last = true;
        for (int k = i + 1; k < n; k++) {
            if (!aps[k].connected && !aps[k].saved) { last = false; break; }
        }
        wifi_ui_create_row(card, &aps[i], last);
    }
}

static void wifi_ui_timer_cb(lv_timer_t* timer)
{
    char status[WIFI_STATUS_TEXT_LEN];
    int  ver;
#if LV_USE_KEYBOARD && LV_USE_TEXTAREA
    char need_service[WIFI_SERVICE_PATH_LEN];
#endif

    (void)timer;

    wifi_service_get_status_text(status, sizeof(status));
    if (g_wifi_status_label) {
        if (status[0]) {
            lv_label_set_text(g_wifi_status_label, status);
            lv_obj_clear_flag(g_wifi_status_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(g_wifi_status_label, LV_OBJ_FLAG_HIDDEN);
        }
    }

    ver = wifi_service_ap_version();
    if (ver != g_wifi_ap_version) {
        g_wifi_ap_version = ver;
        wifi_ui_build_list();
    }

#if LV_USE_KEYBOARD && LV_USE_TEXTAREA
    if (!g_wifi_pwd_box && wifi_service_need_password(need_service, sizeof(need_service))) {
        wifi_ap_t ap;
        memset(&ap, 0, sizeof(ap));
        ap.strength = -1;
        if (wifi_service_find_ap(need_service, &ap) != 0) {
            snprintf(ap.ssid,    sizeof(ap.ssid),    "%s", need_service);
            snprintf(ap.service, sizeof(ap.service), "%s", need_service);
        }
        wifi_ui_password_open(&ap);
    }
#endif

    {
        wifi_state_t st = wifi_service_get_state();
        bool busy = (st == WIFI_STATE_SCANNING   ||
                     st == WIFI_STATE_CONNECTING ||
                     st == WIFI_STATE_ENABLING   ||
                     st == WIFI_STATE_DISABLING);

        if (g_wifi_rescan_queued && !busy) {
            g_wifi_rescan_queued = false;
        }

        if (!g_wifi_rescan_queued && wifi_service_is_enabled()) {
            if (++g_wifi_rescan_tick >= 100) {   /* 50 * 100ms = 5s */
                g_wifi_rescan_tick   = 0;
                g_wifi_rescan_queued = true;
                wifi_service_request_scan();
            }
        } else {
            g_wifi_rescan_tick = 0;
        }
    }
}

static void wifi_ui_page_delete_cb(lv_event_t* e)
{
    (void)e;
    if (g_wifi_timer) {
        lv_timer_del(g_wifi_timer);
        g_wifi_timer = NULL;
    }
    g_wifi_switch       = NULL;
    g_wifi_list_cont    = NULL;
    g_wifi_status_label = NULL;
    g_wifi_info_box     = NULL;
}

static void wifi_ui_switch_event_cb(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t*       sw   = lv_event_get_target(e);

    if (code != LV_EVENT_VALUE_CHANGED) return;

    if (lv_obj_has_state(sw, LV_STATE_CHECKED)) {
        if (g_wifi_list_cont)    lv_obj_clear_flag(g_wifi_list_cont, LV_OBJ_FLAG_HIDDEN);
        if (g_wifi_status_label) lv_obj_clear_flag(g_wifi_status_label, LV_OBJ_FLAG_HIDDEN);
        if (g_wifi_timer)        lv_timer_resume(g_wifi_timer);
        g_wifi_ap_version    = -1;
        g_wifi_rescan_tick   = 0;
        g_wifi_rescan_queued = false;

        wifi_service_enable();
        wifi_service_request_scan();
    } else {
        wifi_service_disable();
        if (g_wifi_list_cont) {
            lv_obj_clean(g_wifi_list_cont);
            lv_obj_add_flag(g_wifi_list_cont, LV_OBJ_FLAG_HIDDEN);
        }
        if (g_wifi_status_label) lv_obj_add_flag(g_wifi_status_label, LV_OBJ_FLAG_HIDDEN);
        if (g_wifi_timer)        lv_timer_pause(g_wifi_timer);
        g_wifi_ap_version = -1;

#if LV_USE_KEYBOARD && LV_USE_TEXTAREA
        wifi_ui_password_close();
#endif
    }
}

/*---------------------------------------------------------------------------
 * Wi-Fi 页面入口
 *--------------------------------------------------------------------------*/
void lv_wifi_page_create(lv_obj_t* parent)
{
    lv_obj_t* card;
    lv_obj_t* name;
    lv_obj_t* sw;

    wifi_service_init();

    lv_obj_set_style_pad_row(parent, 10, 0);
    lv_obj_add_event_cb(parent, wifi_ui_page_delete_cb, LV_EVENT_DELETE, NULL);

    /* Wi-Fi 总开关 */
    card = wifi_ui_create_card(parent);
    lv_obj_set_height(card, WIFI_UI_ROW_H + 10);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(card, 48, 0);
    lv_obj_set_style_pad_right(card, 14, 0);

    name = lv_label_create(card);
    lv_label_set_text(name, WIFI_TXT_TITLE);
    lv_obj_set_flex_grow(name, 1);
    lv_obj_set_style_text_font(name, WIFI_UI_FONT, 0);
    lv_obj_set_style_text_color(name, WIFI_UI_COLOR_TEXT, 0);

    sw = lv_switch_create(card);
    lv_obj_set_size(sw, WIFI_UI_SWITCH_W, WIFI_UI_SWITCH_H);
    lv_obj_set_style_bg_color(sw, lv_color_hex(0xE9E9EA), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sw, WIFI_UI_COLOR_BLUE,
                              LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, lv_color_white(), LV_PART_KNOB);
    lv_obj_add_event_cb(sw, wifi_ui_switch_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    g_wifi_switch = sw;

    /* 状态提示文字 */
    g_wifi_status_label = lv_label_create(parent);
    lv_label_set_text(g_wifi_status_label, WIFI_TXT_NEED_SWITCH);
    lv_obj_set_style_text_font(g_wifi_status_label, WIFI_UI_FONT, 0);
    lv_obj_set_style_text_color(g_wifi_status_label, WIFI_UI_COLOR_GRAY, 0);
    lv_obj_set_style_pad_left(g_wifi_status_label, 8, 0);
    lv_obj_add_flag(g_wifi_status_label, LV_OBJ_FLAG_HIDDEN);

    /* 网络列表容器 */
    g_wifi_list_cont = lv_obj_create(parent);
    lv_obj_remove_style_all(g_wifi_list_cont);
    lv_obj_set_width(g_wifi_list_cont, lv_pct(100));
    lv_obj_set_height(g_wifi_list_cont, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(g_wifi_list_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(g_wifi_list_cont, 10, 0);
    lv_obj_clear_flag(g_wifi_list_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_wifi_list_cont, LV_OBJ_FLAG_HIDDEN);

    /* 轮询定时器 */
    g_wifi_timer = lv_timer_create(wifi_ui_timer_cb, WIFI_UI_TIMER_MS, NULL);
    lv_timer_pause(g_wifi_timer);

    /* 如果 Wi-Fi 已经打开(比如从别的页面切回来), 同步一次开关状态 */
    if (wifi_service_is_enabled()) {
        lv_obj_add_state(g_wifi_switch, LV_STATE_CHECKED);
        lv_obj_clear_flag(g_wifi_list_cont, LV_OBJ_FLAG_HIDDEN);
        lv_timer_resume(g_wifi_timer);
        g_wifi_ap_version = -1;
    }
}

/*===========================================================================
 *  设置菜单
 *=========================================================================*/
static lv_obj_t* root_page;
static lv_obj_t* reboot_msgbox;
static lv_obj_t* poweroff_msgbox;

static lv_obj_t* g_volume_slider_cont = NULL;

/* --- create_text / create_slider / create_switch 前向声明, 保证调用顺序无关 --- */
static lv_obj_t* create_text(lv_obj_t* parent, const char* icon, const char* txt,
                             lv_menu_builder_variant_t builder_variant);
static lv_obj_t* create_slider(lv_obj_t* parent, const char* icon, const char* txt,
                               int32_t min, int32_t max, int32_t val,
                               lv_event_cb_t slider_event_cb);
static lv_obj_t* create_switch(lv_obj_t* parent, const char* icon,
                               const char* txt, bool chk);

static void back_event_handler(lv_event_t* e)
{
    lv_obj_t* obj  = lv_event_get_target(e);
    lv_obj_t* menu = (lv_obj_t*)lv_event_get_user_data(e);

    if (lv_menu_back_btn_is_root(menu, obj)) {
        lv_obj_t* mbox = lv_msgbox_create(NULL, "Hello", "Root back btn click.", NULL, true);
        lv_obj_center(mbox);
    }
}

/* 确认框 yes/no 回调: 点击 yes 时执行 user_data 传入的 shell 命令 */
static void msgbox_event_cb(lv_event_t* e)
{
    lv_obj_t* box = lv_event_get_current_target(e);

    if (lv_msgbox_get_active_btn(box) == 0) {
        const char* cmd = (const char*)lv_event_get_user_data(e);
        if (cmd) system(cmd);
    } else {
        lv_obj_add_flag(box, LV_OBJ_FLAG_HIDDEN);
    }
}

/* 亮度滑块: 同步标签并写入背光节点 */
static void brightness_slider_event_cb(lv_event_t* e)
{
    lv_obj_t* slider      = lv_event_get_target(e);
    lv_obj_t* value_label = lv_event_get_user_data(e);
    int val = (int)lv_slider_get_value(slider);

    lv_label_set_text_fmt(value_label, "%d", val);

    char cmd[80];
    snprintf(cmd, sizeof(cmd),
             "echo %d > /sys/class/backlight/backlight/brightness", val);
    system(cmd);
}

static void volume_slider_event_cb(lv_event_t* e)
{
    lv_obj_t* slider      = lv_event_get_target(e);
    lv_obj_t* value_label = lv_event_get_user_data(e);
    lv_label_set_text_fmt(value_label, "%d", (int)lv_slider_get_value(slider));
}

static lv_obj_t* create_text(lv_obj_t* parent, const char* icon, const char* txt,
                             lv_menu_builder_variant_t builder_variant)
{
    lv_obj_t* obj = lv_menu_cont_create(parent);
    lv_obj_t* img = NULL;
    lv_obj_t* label = NULL;

    if (icon) {
        img = lv_img_create(obj);
        lv_img_set_src(img, icon);
    }

    if (txt) {
        label = lv_label_create(obj);
        lv_label_set_text(label, txt);
        lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_flex_grow(label, 1);
    }

    if (builder_variant == LV_MENU_ITEM_BUILDER_VARIANT_2 && icon && txt) {
        lv_obj_add_flag(img, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);
        lv_obj_swap(img, label);
    }

    return obj;
}

static lv_obj_t* create_slider(lv_obj_t* parent, const char* icon, const char* txt,
                               int32_t min, int32_t max, int32_t val,
                               lv_event_cb_t slider_event_cb)
{
    lv_obj_t* obj = create_text(parent, icon, txt, LV_MENU_ITEM_BUILDER_VARIANT_2);

    lv_obj_t* value_label = lv_label_create(obj);
    lv_label_set_text_fmt(value_label, "%d", val);

    lv_obj_t* slider = lv_slider_create(obj);
    lv_obj_set_flex_grow(slider, 1);
    lv_slider_set_range(slider, min, max);
    lv_slider_set_value(slider, val, LV_ANIM_OFF);

    if (icon == NULL) {
        lv_obj_add_flag(slider, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);
    }

    lv_obj_add_event_cb(slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, value_label);
    return obj;
}

static lv_obj_t* create_switch(lv_obj_t* parent, const char* icon,
                               const char* txt, bool chk)
{
    lv_obj_t* obj = create_text(parent, icon, txt, LV_MENU_ITEM_BUILDER_VARIANT_1);

    lv_obj_t* sw = lv_switch_create(obj);
    lv_obj_add_state(sw, chk ? LV_STATE_CHECKED : 0);
    return obj;
}

static void switch_handler(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* menu = lv_event_get_user_data(e);
    lv_obj_t* obj  = lv_event_get_target(e);

    if (code == LV_EVENT_VALUE_CHANGED) {
        if (lv_obj_has_state(obj, LV_STATE_CHECKED)) {
            lv_menu_set_page(menu, NULL);
            lv_menu_set_sidebar_page(menu, root_page);
            lv_event_send(lv_obj_get_child(lv_obj_get_child(
                lv_menu_get_cur_sidebar_page(menu), 0), 0), LV_EVENT_CLICKED, NULL);
        } else {
            lv_menu_set_sidebar_page(menu, NULL);
            lv_menu_clear_history(menu);
            lv_menu_set_page(menu, root_page);
        }
    }
}

static void reboot_event_handler(lv_event_t* e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        lv_obj_clear_flag(reboot_msgbox, LV_OBJ_FLAG_HIDDEN);
    }
}

static void poweroff_event_handler(lv_event_t* e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        lv_obj_clear_flag(poweroff_msgbox, LV_OBJ_FLAG_HIDDEN);
    }
}

static void sound_switch_handler(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* sw = lv_event_get_target(e);

    if (code == LV_EVENT_VALUE_CHANGED && g_volume_slider_cont) {
        if (lv_obj_has_state(sw, LV_STATE_CHECKED)) {
            lv_obj_clear_flag(g_volume_slider_cont, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(g_volume_slider_cont, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/* 创建一个 yes/no 确认框, 点击 yes 时执行 cmd */
static lv_obj_t* create_confirm_msgbox(const char* title, const char* text,
                                       const char* cmd)
{
    static const char* btns[] = { "yes", "no", "" };

    lv_obj_t* box = lv_msgbox_create(lv_scr_act(), title, text, btns, false);
    lv_obj_set_size(box, 300, 170);
    lv_obj_center(box);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_shadow_width(box, 20, 0);
    lv_obj_set_style_shadow_color(box, lv_color_hex(0xa9a9a9), 0);
    lv_obj_set_style_pad_top(box, 18, 0);
    lv_obj_set_style_pad_left(box, 20, 0);
    lv_obj_add_event_cb(box, msgbox_event_cb, LV_EVENT_VALUE_CHANGED, (void*)cmd);

    lv_obj_t* btn = lv_msgbox_get_btns(box);
    lv_obj_set_style_bg_opa(btn, 0, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_ITEMS);
    lv_obj_set_style_text_font(btn, &lv_font_montserrat_20, LV_PART_ITEMS);
    lv_obj_set_style_text_color(btn, lv_color_hex(0x2271df), LV_PART_ITEMS);
    lv_obj_set_style_text_color(btn, lv_color_hex(0xff0000),
                                LV_PART_ITEMS | LV_STATE_PRESSED);

    lv_obj_add_flag(box, LV_OBJ_FLAG_HIDDEN);
    return box;
}

void lv_settings_menu_create(void)
{
    lv_obj_t* menu = lv_menu_create(lv_scr_act());

    lv_color_t bg_color = lv_obj_get_style_bg_color(menu, 0);
    if (lv_color_brightness(bg_color) > 127) {
        lv_obj_set_style_bg_color(menu,
            lv_color_darken(lv_obj_get_style_bg_color(menu, 0), 10), 0);
    } else {
        lv_obj_set_style_bg_color(menu,
            lv_color_darken(lv_obj_get_style_bg_color(menu, 0), 50), 0);
    }
    lv_menu_set_mode_root_back_btn(menu, LV_MENU_ROOT_BACK_BTN_ENABLED);
    lv_obj_add_event_cb(menu, back_event_handler, LV_EVENT_CLICKED, menu);
    lv_obj_set_size(menu, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));
    lv_obj_center(menu);

    lv_obj_t* cont;
    lv_obj_t* section;
    lv_obj_t* btn;

    /* Wi-Fi 子页面 */
    lv_obj_t* sub_wifi_page = lv_menu_page_create(menu, WIFI_TXT_TITLE);
    lv_obj_set_style_pad_hor(sub_wifi_page,
        lv_obj_get_style_pad_left(lv_menu_get_main_header(menu), 0), 0);
    lv_wifi_page_create(sub_wifi_page);

    /* Sound 子页面 */
    lv_obj_t* sub_sound_page = lv_menu_page_create(menu, NULL);
    lv_obj_set_style_pad_hor(sub_sound_page,
        lv_obj_get_style_pad_left(lv_menu_get_main_header(menu), 0), 0);
    lv_menu_separator_create(sub_sound_page);
    section = lv_menu_section_create(sub_sound_page);
    cont = create_switch(section, LV_SYMBOL_AUDIO, "Sound", false);
    lv_obj_add_event_cb(lv_obj_get_child(cont, 2), sound_switch_handler,
                        LV_EVENT_VALUE_CHANGED, NULL);
    g_volume_slider_cont = create_slider(section, NULL, "Volume", 0, 100, 50,
                                         volume_slider_event_cb);
    lv_obj_add_flag(g_volume_slider_cont, LV_OBJ_FLAG_HIDDEN);

    /* Display 子页面 */
    lv_obj_t* sub_display_page = lv_menu_page_create(menu, NULL);
    lv_obj_set_style_pad_hor(sub_display_page,
        lv_obj_get_style_pad_left(lv_menu_get_main_header(menu), 0), 0);
    lv_menu_separator_create(sub_display_page);
    section = lv_menu_section_create(sub_display_page);
    create_slider(section, NULL, "Brightness", 0, 7, 7, brightness_slider_event_cb);

    /* 软件信息 / 法律信息 */
    lv_obj_t* sub_software_info_page = lv_menu_page_create(menu, NULL);
    lv_obj_set_style_pad_hor(sub_software_info_page,
        lv_obj_get_style_pad_left(lv_menu_get_main_header(menu), 0), 0);
    section = lv_menu_section_create(sub_software_info_page);
    create_text(section, NULL, "Version 0.1", LV_MENU_ITEM_BUILDER_VARIANT_1);

    lv_obj_t* sub_legal_info_page = lv_menu_page_create(menu, NULL);
    lv_obj_set_style_pad_hor(sub_legal_info_page,
        lv_obj_get_style_pad_left(lv_menu_get_main_header(menu), 0), 0);
    section = lv_menu_section_create(sub_legal_info_page);
    for (uint32_t i = 0; i < 15; i++) {
        create_text(section, NULL,
            "This is a long long long long long long long long long text, "
            "if it is long enough it may scroll.",
            LV_MENU_ITEM_BUILDER_VARIANT_1);
    }

    lv_obj_t* sub_about_page = lv_menu_page_create(menu, NULL);
    lv_obj_set_style_pad_hor(sub_about_page,
        lv_obj_get_style_pad_left(lv_menu_get_main_header(menu), 0), 0);
    lv_menu_separator_create(sub_about_page);
    section = lv_menu_section_create(sub_about_page);
    cont = create_text(section, NULL, "Software information",
                       LV_MENU_ITEM_BUILDER_VARIANT_1);
    lv_menu_set_load_page_event(menu, cont, sub_software_info_page);
    cont = create_text(section, NULL, "Legal information",
                       LV_MENU_ITEM_BUILDER_VARIANT_1);
    lv_menu_set_load_page_event(menu, cont, sub_legal_info_page);

    /* 重启 / 关机 确认框 */
    reboot_msgbox   = create_confirm_msgbox(LV_SYMBOL_WARNING " Reboot",
                                            "Are you sure you want to reboot?",
                                            "reboot");
    poweroff_msgbox = create_confirm_msgbox(LV_SYMBOL_WARNING " Power off",
                                            "Are you sure you want to power off?",
                                            "poweroff");

    /* General 子页面 */
    lv_obj_t* sub_general_page = lv_menu_page_create(menu, NULL);
    lv_obj_set_style_pad_hor(sub_general_page,
        lv_obj_get_style_pad_left(lv_menu_get_main_header(menu), 0), 0);
    lv_menu_separator_create(sub_general_page);
    section = lv_menu_section_create(sub_general_page);
    cont = create_text(section, NULL, "reboot", LV_MENU_ITEM_BUILDER_VARIANT_1);
    lv_obj_add_flag(cont, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(cont, reboot_event_handler, LV_EVENT_CLICKED, NULL);
    cont = create_text(section, NULL, "exit", LV_MENU_ITEM_BUILDER_VARIANT_1);
    lv_obj_add_flag(cont, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(cont, poweroff_event_handler, LV_EVENT_CLICKED, NULL);

    /* Menu mode 子页面 */
    lv_obj_t* sub_menu_mode_page = lv_menu_page_create(menu, NULL);
    lv_obj_set_style_pad_hor(sub_menu_mode_page,
        lv_obj_get_style_pad_left(lv_menu_get_main_header(menu), 0), 0);
    lv_menu_separator_create(sub_menu_mode_page);
    section = lv_menu_section_create(sub_menu_mode_page);
    cont = create_switch(section, LV_SYMBOL_AUDIO, "Sidebar enable", true);
    lv_obj_add_event_cb(lv_obj_get_child(cont, 2), switch_handler,
                        LV_EVENT_VALUE_CHANGED, menu);

    /* 根页面 */
    root_page = lv_menu_page_create(menu, "Settings");
    lv_obj_set_style_pad_hor(root_page,
        lv_obj_get_style_pad_left(lv_menu_get_main_header(menu), 0), 0);
    create_text(root_page, NULL, "Connect", LV_MENU_ITEM_BUILDER_VARIANT_1);
    section = lv_menu_section_create(root_page);
    cont = create_text(section, LV_SYMBOL_WIFI, WIFI_TXT_TITLE,
                       LV_MENU_ITEM_BUILDER_VARIANT_1);
    lv_menu_set_load_page_event(menu, cont, sub_wifi_page);
    cont = create_text(section, LV_SYMBOL_AUDIO, "Sound",
                       LV_MENU_ITEM_BUILDER_VARIANT_1);
    lv_menu_set_load_page_event(menu, cont, sub_sound_page);
    cont = create_text(section, LV_SYMBOL_SETTINGS, "Display",
                       LV_MENU_ITEM_BUILDER_VARIANT_1);
    lv_menu_set_load_page_event(menu, cont, sub_display_page);

    create_text(root_page, NULL, "Others", LV_MENU_ITEM_BUILDER_VARIANT_1);
    section = lv_menu_section_create(root_page);
    cont = create_text(section, NULL, "About", LV_MENU_ITEM_BUILDER_VARIANT_1);
    lv_menu_set_load_page_event(menu, cont, sub_about_page);
    cont = create_text(section, LV_SYMBOL_SETTINGS, "general",
                       LV_MENU_ITEM_BUILDER_VARIANT_1);
    lv_menu_set_load_page_event(menu, cont, sub_general_page);
    cont = create_text(section, LV_SYMBOL_SETTINGS, "Menu mode",
                       LV_MENU_ITEM_BUILDER_VARIANT_1);
    lv_menu_set_load_page_event(menu, cont, sub_menu_mode_page);

    lv_menu_set_sidebar_page(menu, root_page);

    lv_event_send(lv_obj_get_child(lv_obj_get_child(
        lv_menu_get_cur_sidebar_page(menu), 0), 0), LV_EVENT_CLICKED, NULL);
}
