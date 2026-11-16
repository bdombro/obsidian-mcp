#include "app.hpp"
#include "log_file.hpp"
#include <mcp_http.hpp>

#include <cstring>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

int main(int argc, char *argv[]) try {
    std::optional<std::string> logFile;
    std::vector<char *> filteredArgv;
    if (argc > 0 && argv != nullptr) {
        filteredArgv.reserve(static_cast<size_t>(argc));
        filteredArgv.push_back(argv[0]);
        for (int i = 1; i < argc; ++i) {
            const char *arg = argv[i];
            if (arg == nullptr)
                continue;
            std::string_view sv(arg);
            if (!sv.empty() && sv.front() == '{') {
                filteredArgv.push_back(argv[i]);
                continue;
            }
            if (sv == "--log-file") {
                if (i + 1 >= argc || argv[i + 1] == nullptr || argv[i + 1][0] == '\0') {
                    std::cerr << "Missing path for --log-file\n";
                    return 1;
                }
                logFile = argv[++i];
            } else if (sv.starts_with("--log-file=")) {
                std::string_view val =
                    sv.substr(std::string_view("--log-file=").size());
                if (val.empty()) {
                    std::cerr << "Missing path for --log-file\n";
                    return 1;
                }
                logFile = std::string(val);
            } else {
                filteredArgv.push_back(argv[i]);
            }
        }
    }

    bool isMcpMode = false;
    for (size_t i = 1; i < filteredArgv.size(); ++i) {
        if (std::strcmp(filteredArgv[i], "--skip-checks") == 0)
            continue;
        isMcpMode = (std::strcmp(filteredArgv[i], "mcp") == 0);
        break;
    }

    std::unique_ptr<app_log::LogFileGuard> logGuard;
    if (logFile.has_value()) {
        if (isMcpMode) {
            std::cerr << "--log-file cannot be used with mcp stdio mode\n";
            return 1;
        }
        auto opened = app_log::LogFileGuard::open(*logFile);
        if (!opened) {
            std::cerr << opened.error() << "\n";
            return 1;
        }
        logGuard = std::move(*opened);
    }

    auto app = obsidian::make_obsidian_app();
    slimr::enable_http(app, slimr::HttpServerOptions{.host = "127.0.0.1", .port = 10001});
    return app.run(static_cast<int>(filteredArgv.size()), filteredArgv.data());
} catch (const std::exception &e) {
    std::cerr << "obsidian-mcp: fatal: " << e.what() << "\n";
    return 1;
} catch (...) {
    std::cerr << "obsidian-mcp: fatal: unknown error\n";
    return 1;
}
