#pragma once
#include <stddef.h>
#include <esp_http_server.h>

namespace web_assets {
size_t count();
bool registerRoutes(httpd_handle_t server);
} // namespace web_assets
