
#include "wifi_config.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include <ctype.h>
#include "nvs_flash.h"

static const char *TAG = "web_config";

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
    return ESP_OK;
}

static esp_err_t wifi_get_handler(httpd_req_t *req) {
    char html[512];
    char ssid[33] = "";
    wifi_config_load();
    // 既存SSIDを取得（NVSから）
    nvs_handle_t nvs;
    size_t len = sizeof(ssid);
    if (nvs_open("wifi_cfg", NVS_READONLY, &nvs) == ESP_OK) {
        nvs_get_str(nvs, "ssid", ssid, &len);
        nvs_close(nvs);
    }
    char ssid_esc[64];
    html_escape(ssid_esc, ssid, sizeof(ssid_esc));
    snprintf(html, sizeof(html),
        "<h2>Wi-Fi設定</h2><form method='POST'><label>SSID:<input name='ssid' value='%s' maxlength='32'></label><br><label>パスワード:<input name='pass' type='password' maxlength='64'></label><br><button>保存</button></form>",
        ssid_esc);
    httpd_resp_set_type(req, "text/html; charset=UTF-8");
    httpd_resp_sendstr(req, html);
    return ESP_OK;
}

static esp_err_t wifi_done_handler(httpd_req_t *req) {
    const char resp[] = "<h2>保存しました</h2><a href='/'>戻る</a>";
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
        ESP_LOGI(TAG, "Wi-Fi設定Webサーバ起動");
    }
}
