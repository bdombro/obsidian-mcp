#include "app.hpp"
#include <mcp_http.hpp>

int main(int argc, char* argv[]) {
    auto app = obsidian::make_obsidian_app();
    slimr::enable_http(app, slimr::HttpServerOptions{.host = "127.0.0.1", .port = 8081});
    return app.run(argc, argv);
}
