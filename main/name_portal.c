/**
 * @file name_portal.c
 * @brief Tiny SoftAP + HTTP server that lets a phone set the badge nickname.
 *
 * Stripped-down sibling of the BYUI loader's wifi_config + portal_mode
 * components. We don't ask for WiFi SSID/password, just a name. One AP,
 * one URL, one form, one POST.
 */

#include "name_portal.h"
#include "display.h"
#include "buttons.h"
#include "leds.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>
#include <stdio.h>
#include <ctype.h>

#define TAG "name_portal"

#define NVS_PARTITION  "user_data"
#define NVS_NAMESPACE  "badge_cfg"
#define NVS_KEY_NICK   "nick"

#define AP_SSID_BASE   "BadgeName"
#define AP_CHANNEL     1
#define AP_MAX_CONN    1
#define AP_URL         "http://192.168.4.1/"

#define NAME_MAX_LEN   32

/* ── Module state ─────────────────────────────────────────────────── */
static httpd_handle_t  s_server      = NULL;
static esp_netif_t    *s_ap_netif    = NULL;
static volatile bool   s_done        = false;
static volatile bool   s_sta_joined  = false;
static char            s_submitted_name[NAME_MAX_LEN + 1] = {0};
static char            s_ap_ssid[24] = AP_SSID_BASE;
static esp_event_handler_instance_t s_wifi_evt_inst = NULL;

/* ── WiFi event handler — flag when a phone associates ───────────── */
static void wifi_evt(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI(TAG, "Station joined AP");
        s_sta_joined = true;
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STADISCONNECTED) {
        s_sta_joined = false;
    }
}

/* ── HTTP handlers ────────────────────────────────────────────────── */

static const char FORM_HTML[] =
    "<!doctype html><html><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>Friends Around Me</title>"
    "<style>"
    "body{font-family:sans-serif;background:#fff;color:#003;"
    "max-width:480px;margin:2em auto;padding:0 1em;text-align:center;}"
    "h1{color:#003;}"
    "input{font-size:1.4em;padding:.4em;width:90%;margin-top:1em;}"
    "button{font-size:1.4em;padding:.6em 2em;margin-top:1em;"
    "background:#003;color:#fff;border:0;border-radius:.4em;}"
    "</style></head><body>"
    "<h1>Welcome!</h1>"
    "<p>Please enter your name and tap Save.</p>"
    "<form method=\"POST\" action=\"/save\" autocomplete=\"off\">"
    "<input name=\"nick\" maxlength=\"32\" required autofocus "
    "placeholder=\"Your name\">"
    "<br><button type=\"submit\">Save</button>"
    "</form></body></html>";

static const char DONE_HTML[] =
    "<!doctype html><html><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>Done</title>"
    "<style>body{font-family:sans-serif;text-align:center;margin:3em;}</style>"
    "</head><body><h1>Saved!</h1>"
    "<p>You can close this page and put the phone away.</p>"
    "</body></html>";

static esp_err_t form_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, FORM_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* URL-decode in-place; returns the new length. */
static size_t url_decode(char *s)
{
    char *r = s, *w = s;
    while (*r) {
        if (*r == '+') { *w++ = ' '; r++; }
        else if (*r == '%' && r[1] && r[2]) {
            char hex[3] = { r[1], r[2], 0 };
            *w++ = (char)strtoul(hex, NULL, 16);
            r += 3;
        } else {
            *w++ = *r++;
        }
    }
    *w = '\0';
    return (size_t)(w - s);
}

/* Pull "nick=..." out of the form-urlencoded body. */
static bool extract_nick(const char *body, char *out, size_t outlen)
{
    const char *p = strstr(body, "nick=");
    if (!p) return false;
    p += 5;
    const char *end = strchr(p, '&');
    size_t len = end ? (size_t)(end - p) : strlen(p);
    if (len >= outlen) len = outlen - 1;
    memcpy(out, p, len);
    out[len] = '\0';
    url_decode(out);

    /* Trim leading/trailing whitespace. */
    char *s = out;
    while (*s && isspace((unsigned char)*s)) s++;
    if (s != out) memmove(out, s, strlen(s) + 1);
    size_t l = strlen(out);
    while (l && isspace((unsigned char)out[l-1])) out[--l] = '\0';

    return out[0] != '\0';
}

static esp_err_t save_post_handler(httpd_req_t *req)
{
    char body[256];
    int recv = httpd_req_recv(req, body, sizeof(body) - 1);
    if (recv <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no body");
        return ESP_FAIL;
    }
    body[recv] = '\0';

    char nick[NAME_MAX_LEN + 1] = {0};
    if (!extract_nick(body, nick, sizeof(nick))) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no nick");
        return ESP_FAIL;
    }

    /* Persist to NVS so the next boot picks it up automatically. */
    nvs_handle_t h;
    esp_err_t err = nvs_open_from_partition(NVS_PARTITION, NVS_NAMESPACE,
                                             NVS_READWRITE, &h);
    if (err == ESP_OK) {
        nvs_set_str(h, NVS_KEY_NICK, nick);
        nvs_commit(h);
        nvs_close(h);
    } else {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
    }

    strncpy(s_submitted_name, nick, sizeof(s_submitted_name) - 1);
    s_submitted_name[sizeof(s_submitted_name) - 1] = '\0';
    s_done = true;

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, DONE_HTML, HTTPD_RESP_USE_STRLEN);
    ESP_LOGI(TAG, "Saved nickname: %s", nick);
    return ESP_OK;
}

/* ── AP + HTTP setup ──────────────────────────────────────────────── */

static bool start_softap(void)
{
    /* Build a slightly-unique SSID from the BT MAC tail so two badges
     * within range don't collide. */
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_BT);
    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "%s_%02X%02X",
             AP_SSID_BASE, mac[4], mac[5]);

    /* Best-effort init of the netif/event subsystems; ignore "already". */
    esp_err_t e = esp_netif_init();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return false;
    e = esp_event_loop_create_default();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return false;

    s_ap_netif = esp_netif_create_default_wifi_ap();

    wifi_init_config_t wic = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&wic) != ESP_OK) return false;

    /* Listen for STA connect/disconnect so the display can switch from the
     * "join the AP" QR to the "open this URL" QR. */
    if (esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                            wifi_evt, NULL,
                                            &s_wifi_evt_inst) != ESP_OK) {
        return false;
    }

    wifi_config_t apc = {0};
    strlcpy((char *)apc.ap.ssid, s_ap_ssid, sizeof(apc.ap.ssid));
    apc.ap.ssid_len      = strlen(s_ap_ssid);
    apc.ap.channel       = AP_CHANNEL;
    apc.ap.authmode      = WIFI_AUTH_OPEN;
    apc.ap.max_connection= AP_MAX_CONN;

    if (esp_wifi_set_mode(WIFI_MODE_AP) != ESP_OK) return false;
    if (esp_wifi_set_config(WIFI_IF_AP, &apc) != ESP_OK) return false;
    if (esp_wifi_start() != ESP_OK) return false;

    httpd_config_t hc = HTTPD_DEFAULT_CONFIG();
    hc.lru_purge_enable = true;
    if (httpd_start(&s_server, &hc) != ESP_OK) return false;

    httpd_uri_t form = { .uri = "/", .method = HTTP_GET,
                         .handler = form_get_handler };
    httpd_uri_t save = { .uri = "/save", .method = HTTP_POST,
                         .handler = save_post_handler };
    httpd_register_uri_handler(s_server, &form);
    httpd_register_uri_handler(s_server, &save);

    ESP_LOGI(TAG, "AP up: SSID=\"%s\"  URL=%s", s_ap_ssid, AP_URL);
    return true;
}

static void stop_softap(void)
{
    if (s_server)   { httpd_stop(s_server); s_server = NULL; }
    if (s_wifi_evt_inst) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              s_wifi_evt_inst);
        s_wifi_evt_inst = NULL;
    }
    esp_wifi_stop();
    esp_wifi_deinit();
    if (s_ap_netif) { esp_netif_destroy(s_ap_netif); s_ap_netif = NULL; }
}

/* ── Display helpers (white bg, blue text, centered) ──────────────── */

static int centred_x(const char *s, int scale)
{
    int w = (int)strlen(s) * 8 * scale;
    int x = (DISPLAY_WIDTH - w) / 2;
    return x < 0 ? 0 : x;
}

/* Phase 1 — phone hasn't joined yet. QR encodes a Wi-Fi join descriptor
 * (`WIFI:T:nopass;S:<ssid>;;`); modern phones recognise it and prompt
 * the user to connect to the AP without typing the SSID.
 *
 * Layout: 24-px black header, large QR centred in the middle whitespace,
 * three scale-2 footer lines on white. */
static void draw_join_screen(void)
{
    display_fill(COLOR_WHITE);

    /* Single-line black header. */
    display_fill_rect(0, 0, DISPLAY_WIDTH, 24, COLOR_BLACK);
    display_draw_string((int16_t)centred_x("Enter Your Name", 2), 4,
                        "Enter Your Name", COLOR_WHITE, COLOR_BLACK, 2);

    /* QR — module 4 with a v3 (33-byte) WiFi payload → 148 px square,
     * which is the largest that fits between the header and footer. */
    char wifi_qr[64];
    snprintf(wifi_qr, sizeof(wifi_qr), "WIFI:T:nopass;S:%s;;", s_ap_ssid);
    display_draw_qr(160, 102, wifi_qr, 4, COLOR_BLACK, COLOR_WHITE);

    /* Footer at scale 2 (3 lines, 16 px each + spacing). */
    display_draw_string((int16_t)centred_x("Scan to join Wi-Fi", 2), 184,
                        "Scan to join Wi-Fi", COLOR_BLUE, COLOR_WHITE, 2);
    char ssid_line[48];
    snprintf(ssid_line, sizeof(ssid_line), "SSID: %s", s_ap_ssid);
    /* If the SSID busts 20 chars at scale 2 (long custom suffix) drop to
     * scale 1 for that one line so it doesn't get clipped. */
    uint8_t sscale = (strlen(ssid_line) <= 20) ? 2 : 1;
    int sy = (sscale == 2) ? 206 : 210;
    display_draw_string((int16_t)centred_x(ssid_line, sscale), sy,
                        ssid_line, COLOR_BLUE, COLOR_WHITE, sscale);
    display_draw_string((int16_t)centred_x("Press B to cancel", 2), 224,
                        "Press B to cancel", COLOR_GRAY, COLOR_WHITE, 2);
}

/* Phase 2 — phone is on the AP. QR now encodes the form URL so the user
 * can open it with a second scan instead of typing.
 *
 * Header on black bg (green-on-white was washed out). Footer lines now
 * scale 2 so they're readable from arm's length. */
static void draw_form_url_screen(void)
{
    display_fill(COLOR_WHITE);

    /* Black banner with bright-green title. */
    display_fill_rect(0, 0, DISPLAY_WIDTH, 24, COLOR_BLACK);
    display_draw_string((int16_t)centred_x("Phone Connected", 2), 4,
                        "Phone Connected", COLOR_GREEN, COLOR_BLACK, 2);

    /* QR centred in the whitespace between the header (24 px) and the
     * scale-2 footer block (~70 px). v1 URL → 29 modules × 4 px = 116 px. */
    display_draw_qr(160, 105, AP_URL, 4, COLOR_BLACK, COLOR_WHITE);

    /* Three footer lines, all scale 2. */
    display_draw_string((int16_t)centred_x("Scan again for Name", 2), 170,
                        "Scan again for Name", COLOR_BLUE, COLOR_WHITE, 2);
    display_draw_string((int16_t)centred_x(AP_URL, 2),                192,
                        AP_URL,                COLOR_BLUE, COLOR_WHITE, 2);
    display_draw_string((int16_t)centred_x("Press B to cancel", 2),   214,
                        "Press B to cancel",   COLOR_GRAY, COLOR_WHITE, 2);
}

static void draw_done_screen(const char *nick)
{
    display_fill(COLOR_WHITE);
    display_draw_string((int16_t)centred_x("Hello,", 3), 60,
                        "Hello,", COLOR_BLUE, COLOR_WHITE, 3);
    display_draw_string((int16_t)centred_x(nick, 3), 110,
                        nick, COLOR_BLUE, COLOR_WHITE, 3);
    display_draw_string((int16_t)centred_x("Returning...", 2), 180,
                        "Returning...", COLOR_GRAY, COLOR_WHITE, 2);
}

/* ── Cancel button watcher ────────────────────────────────────────── */

static volatile bool s_cancel = false;
static void portal_button_event(button_event_t event)
{
    if (event.event == BUTTON_EVENT_CLICK && event.button == BUTTON_B) {
        s_cancel = true;
    }
}

/* ── Public entry point ───────────────────────────────────────────── */

bool name_portal_run(char *nick_out, size_t outlen)
{
    s_done = false;
    s_cancel = false;
    s_sta_joined = false;
    s_submitted_name[0] = '\0';

    if (!start_softap()) {
        ESP_LOGE(TAG, "Failed to start AP / HTTP server");
        return false;
    }

    draw_join_screen();
    leds_clear();
    leds_show();

    buttons_register_callback(portal_button_event);

    bool form_url_shown = false;
    while (!s_done && !s_cancel) {
        vTaskDelay(pdMS_TO_TICKS(100));
        if (!form_url_shown && s_sta_joined) {
            form_url_shown = true;
            draw_form_url_screen();
        }
    }

    if (s_done && nick_out && outlen) {
        strncpy(nick_out, s_submitted_name, outlen - 1);
        nick_out[outlen - 1] = '\0';
        draw_done_screen(s_submitted_name);
        vTaskDelay(pdMS_TO_TICKS(1500));
    }

    stop_softap();
    return s_done;
}
