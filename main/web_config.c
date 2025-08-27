
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include <ctype.h>
#include "nvs_flash.h"
#include "esp_system.h" // esp_restart用

#include "common.h"
#include "wifi_config.h"

// static const char *TAG = "web_config";

// URLデコード
static void url_decode(char *dst, const char *src, size_t dst_len) {
    char a, b;
    size_t i = 0;
    while (*src && i + 1 < dst_len) {
        if ((*src == '%') && ((a = src[1]) && (b = src[2])) && isxdigit(a) && isxdigit(b)) {
            if (i + 1 < dst_len) dst[i++] = (char)((strtol((char[]){a, b, 0}, NULL, 16)) & 0xFF);
            src += 3;
        } else if (*src == '+') {
            dst[i++] = ' ';
            src++;
        } else {
            dst[i++] = *src++;
        }
    }
    dst[i] = '\0';
}

// HTMLエスケープ（最低限）
static void html_escape(char *dst, const char *src, size_t dst_len) {
    size_t i = 0;
    while (*src && i + 6 < dst_len) {
        if (*src == '<') { strncpy(&dst[i], "&lt;", dst_len-i); i += 4; }
        else if (*src == '>') { strncpy(&dst[i], "&gt;", dst_len-i); i += 4; }
        else if (*src == '"') { strncpy(&dst[i], "&quot;", dst_len-i); i += 6; }
        else if (*src == '&') { strncpy(&dst[i], "&amp;", dst_len-i); i += 5; }
        else dst[i++] = *src;
        src++;
    }
    dst[i] = '\0';
}

static esp_err_t wifi_post_handler(httpd_req_t *req) {
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    char ssid_enc[64] = "", pass_enc[64] = "";
    char *ssid_p = strstr(buf, "ssid=");
    char *pass_p = strstr(buf, "pass=");
    if (ssid_p) {
        ssid_p += 5;
        char *end = strchr(ssid_p, '&');
        int len = end ? (end - ssid_p) : strlen(ssid_p);
        strncpy(ssid_enc, ssid_p, len); ssid_enc[len] = '\0';
    }
    if (pass_p) {
        pass_p += 5;
        char *end = strchr(pass_p, '&');
        int len = end ? (end - pass_p) : strlen(pass_p);
        strncpy(pass_enc, pass_p, len); pass_enc[len] = '\0';
    }
    char ssid[33] = "", pass[65] = "";
    url_decode(ssid, ssid_enc, sizeof(ssid));
    url_decode(pass, pass_enc, sizeof(pass));
    ESP_LOGI(TAG, "受信SSID: %s PASS: %s", ssid, pass);
    if (strlen(ssid) == 0) {
        httpd_resp_sendstr(req, "SSIDが未入力です");
        return ESP_OK;
    }
    wifi_config_save(ssid, pass);
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/done");
    httpd_resp_sendstr(req, "");
    vTaskDelay(pdMS_TO_TICKS(500)); // 応答送信のため少し待つ
    esp_restart();
    return ESP_OK;
}

static esp_err_t wifi_get_handler(httpd_req_t *req) {
    char *html = malloc(2048);
    if (!html) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
        return ESP_FAIL;
    }
    char ssid[33] = "";
    wifi_config_load();
    // 既存SSIDを取得（NVSから）
    nvs_handle_t nvs;
    size_t len = sizeof(ssid);
    if (nvs_open(WIFI_CONFIG_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        nvs_get_str(nvs, WIFI_CONFIG_SSID_KEY, ssid, &len);
        nvs_close(nvs);
    }
    char ssid_esc[64];
    html_escape(ssid_esc, ssid, sizeof(ssid_esc));
    snprintf(html, 2048,
        "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>WiFi Config</title>"
        "<style>"
        "body{font-family:Arial;margin:20px;background:#f5f5f5}"
        "h1{color:#333;text-align:center}"
        ".c{max-width:500px;margin:0 auto;background:white;padding:20px;border-radius:5px}"
        "input[type=text],input[type=password]{width:100%%;padding:10px;margin:5px 0;border:1px solid #ccc;border-radius:3px;box-sizing:border-box}"
        "button{background:#4CAF50;color:white;padding:12px;border:none;border-radius:3px;cursor:pointer;width:100%%;font-size:14px}"
        "button:hover{background:#45a049}"
        "table{width:100%%;border-collapse:collapse;margin:10px 0}"
        "td{padding:8px;border-bottom:1px solid #ddd}"
        ".t{color:#4CAF50;font-weight:bold}"
        "</style>"
        "</head><body>"
        "<div class=c>"
        "<h1>Cerevo Maker Chip Gacha<br>Wi-Fi設定</h1>"
        "<table>"
        "<tr><td><b>ファームウェア</b></td><td>1.0.0</td></tr>"
        "<tr><td><b>現在のSSID</b></td><td>%s</td></tr>"
        "</table>"
        "<h2>Wi-Fi情報の入力</h2>"
        "<form method='POST'>"
        "<label for=ssid>SSID:</label>"
        "<input type=text id=ssid name=ssid value=\"%s\" maxlength=\"32\" required>"
        "<label for=pass>パスワード:</label>"
        "<input type=password id=pass name=pass maxlength=\"64\">"
        "<button type=submit>保存</button>"
        "</form>"
        "<p style='color:gray;font-size:small;'>※SSIDは必須、パスワードは必要に応じて入力してください。</p>"
        "</div>"
        "</body></html>",
        ssid_esc, ssid_esc);
    httpd_resp_set_type(req, "text/html; charset=UTF-8");
    httpd_resp_sendstr(req, html);
    free(html);
    return ESP_OK;
}

static esp_err_t wifi_done_handler(httpd_req_t *req) {
    const char resp[] = "<h2>Wi-Fi情報を保存しました</h2>"
        "<p>設定が反映されるまで数秒お待ちください。</p>"
        "<a href='/'>設定画面に戻る</a>";
    httpd_resp_set_type(req, "text/html; charset=UTF-8");
    httpd_resp_sendstr(req, resp);
    return ESP_OK;
}

void start_wifi_config_server(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t get_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = wifi_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &get_uri);
        httpd_uri_t post_uri = {
            .uri = "/",
            .method = HTTP_POST,
            .handler = wifi_post_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &post_uri);
        httpd_uri_t done_uri = {
            .uri = "/done",
            .method = HTTP_GET,
            .handler = wifi_done_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &done_uri);
        ESP_LOGI(TAG, "Wi-Fi設定Webサーバ起動: PCやスマホで http://192.168.4.1/ にアクセスしてください");
    }
}
