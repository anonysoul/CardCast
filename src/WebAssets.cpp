#include "WebAssets.h"

extern const char webIndexHtmlStart[] asm("_binary_web_index_html_start");
extern const char webIndexHtmlEnd[] asm("_binary_web_index_html_end");
extern const char webStylesCssStart[] asm("_binary_web_styles_css_start");
extern const char webStylesCssEnd[] asm("_binary_web_styles_css_end");
extern const char webPlayerJsStart[] asm("_binary_web_player_js_start");
extern const char webPlayerJsEnd[] asm("_binary_web_player_js_end");
extern const char webProtocolJsStart[] asm("_binary_web_protocol_js_start");
extern const char webProtocolJsEnd[] asm("_binary_web_protocol_js_end");
extern const char webStreamClockJsStart[] asm("_binary_web_stream_clock_js_start");
extern const char webStreamClockJsEnd[] asm("_binary_web_stream_clock_js_end");
extern const char webFrameStatsJsStart[] asm("_binary_web_frame_stats_js_start");
extern const char webFrameStatsJsEnd[] asm("_binary_web_frame_stats_js_end");
extern const char webPcmPlayerJsStart[] asm("_binary_web_pcm_player_js_start");
extern const char webPcmPlayerJsEnd[] asm("_binary_web_pcm_player_js_end");
extern const char webStreamControllerJsStart[] asm("_binary_web_stream_controller_js_start");
extern const char webStreamControllerJsEnd[] asm("_binary_web_stream_controller_js_end");
extern const char webVisualizerJsStart[] asm("_binary_web_visualizer_js_start");
extern const char webVisualizerJsEnd[] asm("_binary_web_visualizer_js_end");
extern const char webI18nJsStart[] asm("_binary_web_i18n_js_start");
extern const char webI18nJsEnd[] asm("_binary_web_i18n_js_end");
extern const char webPlayerViewJsStart[] asm("_binary_web_player_view_js_start");
extern const char webPlayerViewJsEnd[] asm("_binary_web_player_view_js_end");

namespace {
struct WebAsset {
    const char* uri;
    const char* contentType;
    const char* start;
    const char* end;
};

const WebAsset webAssets[] = {
    {"/", "text/html; charset=utf-8", webIndexHtmlStart, webIndexHtmlEnd},
    {"/styles.css", "text/css; charset=utf-8", webStylesCssStart, webStylesCssEnd},
    {"/player.js", "application/javascript; charset=utf-8", webPlayerJsStart, webPlayerJsEnd},
    {"/protocol.js", "application/javascript; charset=utf-8", webProtocolJsStart, webProtocolJsEnd},
    {"/stream-clock.js", "application/javascript; charset=utf-8", webStreamClockJsStart, webStreamClockJsEnd},
    {"/frame-stats.js", "application/javascript; charset=utf-8", webFrameStatsJsStart, webFrameStatsJsEnd},
    {"/pcm-player.js", "application/javascript; charset=utf-8", webPcmPlayerJsStart, webPcmPlayerJsEnd},
    {"/stream-controller.js", "application/javascript; charset=utf-8", webStreamControllerJsStart, webStreamControllerJsEnd},
    {"/visualizer.js", "application/javascript; charset=utf-8", webVisualizerJsStart, webVisualizerJsEnd},
    {"/i18n.js", "application/javascript; charset=utf-8", webI18nJsStart, webI18nJsEnd},
    {"/player-view.js", "application/javascript; charset=utf-8", webPlayerViewJsStart, webPlayerViewJsEnd},
};
constexpr size_t webAssetCount = sizeof(webAssets) / sizeof(webAssets[0]);

esp_err_t webAssetHandler(httpd_req_t* req) {
    const auto* asset = static_cast<const WebAsset*>(req->user_ctx);
    httpd_resp_set_type(req, asset->contentType);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Connection", "close");
    // embed_txtfiles appends a null terminator which is not part of the response.
    const esp_err_t result = httpd_resp_send(req, asset->start, asset->end - asset->start - 1);
    if (result != ESP_OK) return result;
    // The response header alone does not free an ESP-IDF HTTPD session. Queue
    // closure after sending so resource downloads cannot occupy every WS slot.
    // Returning failure also lets HTTPD close it if the close could not queue.
    return httpd_sess_trigger_close(req->handle, httpd_req_to_sockfd(req));
}

} // namespace

size_t web_assets::count() { return webAssetCount; }

bool web_assets::registerRoutes(httpd_handle_t server) {
    for (const auto& asset : webAssets) {
        httpd_uri_t route{};
        route.uri = asset.uri;
        route.method = HTTP_GET;
        route.handler = webAssetHandler;
        route.user_ctx = const_cast<WebAsset*>(&asset);
        if (httpd_register_uri_handler(server, &route) != ESP_OK) {
            return false;
        }
    }
    return true;
}
