#pragma once
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
constexpr esp_err_t ESP_FAIL = -1;
using httpd_handle_t = void*;
constexpr int HTTP_GET = 1;
struct httpd_req_t {
    httpd_handle_t handle = nullptr;
    int method = HTTP_GET;
    void* user_ctx = nullptr;
    int fd = -1;
};
enum httpd_ws_type_t { HTTPD_WS_TYPE_TEXT = 1, HTTPD_WS_TYPE_BINARY = 2, HTTPD_WS_TYPE_CLOSE = 8 };
struct httpd_ws_frame_t {
    bool final = false;
    bool fragmented = false;
    httpd_ws_type_t type = HTTPD_WS_TYPE_TEXT;
    uint8_t* payload = nullptr;
    size_t len = 0;
};
struct httpd_uri_t {
    const char* uri = nullptr;
    int method = HTTP_GET;
    esp_err_t (*handler)(httpd_req_t*) = nullptr;
    void* user_ctx = nullptr;
    bool is_websocket = false;
};
struct httpd_config_t {
    int server_port = 80;
    size_t max_uri_handlers = 8;
    size_t max_open_sockets = 7;
    bool lru_purge_enable = false;
    uint32_t stack_size = 4096;
    esp_err_t (*open_fn)(httpd_handle_t, int) = nullptr;
    void (*close_fn)(httpd_handle_t, int) = nullptr;
    int recv_wait_timeout = 5;
    int send_wait_timeout = 5;
};
#define HTTPD_DEFAULT_CONFIG() httpd_config_t{}
using httpd_work_fn_t = void (*)(void*);
esp_err_t httpd_start(httpd_handle_t*, const httpd_config_t*);
esp_err_t httpd_stop(httpd_handle_t);
esp_err_t httpd_register_uri_handler(httpd_handle_t, const httpd_uri_t*);
esp_err_t httpd_resp_set_type(httpd_req_t*, const char*);
esp_err_t httpd_resp_set_hdr(httpd_req_t*, const char*, const char*);
esp_err_t httpd_resp_send(httpd_req_t*, const char*, ssize_t);
int httpd_req_to_sockfd(httpd_req_t*);
esp_err_t httpd_sess_trigger_close(httpd_handle_t, int);
esp_err_t httpd_sess_update_lru_counter(httpd_handle_t, int);
esp_err_t httpd_ws_recv_frame(httpd_req_t*, httpd_ws_frame_t*, size_t);
esp_err_t httpd_ws_send_frame(httpd_req_t*, httpd_ws_frame_t*);
esp_err_t httpd_ws_send_frame_async(httpd_handle_t, int, httpd_ws_frame_t*);
esp_err_t httpd_queue_work(httpd_handle_t, httpd_work_fn_t, void*);
