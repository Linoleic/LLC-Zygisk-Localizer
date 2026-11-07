#include "../tools/https_client.hpp"
#include <cassert>
#include <string>
int main() {
    using https_client::http_reason;
    const std::string api = "https://api.github.com/repos/a/b/releases/tags/1";
    assert(std::string(http_reason("\nLLC_HTTP_STATUS:403", api)) == "github_api_access_denied");
    assert(std::string(http_reason("\nLLC_HTTP_STATUS:403", "https://github.com/a")) == "https_access_denied");
    assert(std::string(http_reason("\nLLC_HTTP_STATUS:429", api)) == "https_rate_limited");
    assert(std::string(http_reason("\nLLC_HTTP_STATUS:404", api)) == "https_not_found");
    assert(std::string(http_reason("\nLLC_HTTP_STATUS:503", api)) == "https_server_error");
    assert(std::string(http_reason("broken", api)) == "https_http_error");
    assert(std::string(http_reason("\nLLC_HTTP_STATUS:401", api)) == "https_http_error");
}
