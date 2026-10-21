#include "journal.hpp"
#include "passthrough.hpp"
#include "search.hpp"
#include "watch.hpp"

#include <CLI11.hpp>
#include <json.hpp>
#include <httplib.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

// ─────────────────────────────────────────────
// .env loader  (mirrors dotenvy)
// ─────────────────────────────────────────────
static void load_dotenv(const std::string& file_path) {
    std::ifstream f(file_path);
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        // Strip comments and whitespace
        auto pos = line.find('#');
        if (pos != std::string::npos) line = line.substr(0, pos);
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back())))
            line.pop_back();
        if (line.empty()) continue;

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key   = line.substr(0, eq);
        std::string value = line.substr(eq + 1);

        // Strip surrounding quotes from value
        if (value.size() >= 2 &&
            ((value.front() == '"' && value.back() == '"') ||
             (value.front() == '\'' && value.back() == '\'')))
            value = value.substr(1, value.size() - 2);

#ifdef _WIN32
        SetEnvironmentVariableA(key.c_str(), value.c_str());
#else
        setenv(key.c_str(), value.c_str(), 0 /*no overwrite*/);
#endif
    }
}

// ─────────────────────────────────────────────
// Groq key helper  (mirrors Rust ai_schema())
// ─────────────────────────────────────────────
static SearchSchema ai_schema(const std::string& query) {
    const char* key = std::getenv("GROQ_API_KEY");
    if (key && std::string(key).find_first_not_of(" \t") != std::string::npos)
        return parse_with_groq(query, key);
    return parse_query(query);
}

// ─────────────────────────────────────────────
// Serve command  (mirrors Rust axum server)
// ─────────────────────────────────────────────
static void run_server(const std::string& bind_addr) {
    // Parse "host:port"
    auto colon = bind_addr.rfind(':');
    std::string host = colon != std::string::npos
                       ? bind_addr.substr(0, colon) : "127.0.0.1";
    int port = colon != std::string::npos
               ? std::stoi(bind_addr.substr(colon + 1)) : 8787;

    httplib::Server svr;

    svr.Post("/api/parse", [](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = nlohmann::json::parse(req.body);
            std::string query = j.at("query").get<std::string>();
            SearchSchema schema = ai_schema(query);

            nlohmann::json out = {
                {"keywords",   schema.keywords},
                {"extensions", schema.extensions},
                {"intent",     schema.intent}
            };
            res.set_content(out.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 502;
            res.set_content(e.what(), "text/plain");
        }
    });

    // CORS pre-flight
    svr.Options(".*", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin",  "*");
        res.set_header("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        res.status = 204;
    });
    svr.set_post_routing_handler([](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
    });

    std::cout << "Listening on http://" << host << ":" << port << "\n";
    if (!svr.listen(host.c_str(), port))
        throw std::runtime_error("Failed to bind " + bind_addr);
}

// ─────────────────────────────────────────────
// main
// ─────────────────────────────────────────────
int main(int argc, char** argv) {
    // Load .env from current dir, then parent dir (mirrors dotenvy)
    load_dotenv(".env");
    load_dotenv("../.env");

    CLI::App app{"SafeVault FS journaling foundation"};
    app.require_subcommand(1);

    // --database option (global)
    std::string db_path = ".safevault_storage/safevault.sqlite3";
    app.add_option("--database", db_path, "SQLite database path")
       ->capture_default_str();

    // ── init ──────────────────────────────────
    auto* init_cmd = app.add_subcommand("init", "Initialise the database");
    init_cmd->callback([&]() {
        Journal jrnl(db_path);
        std::cout << "Initialized " << db_path << "\n";
    });

    // ── record ────────────────────────────────
    std::string rec_event_str, rec_path_str, rec_old_path_str;
    auto* rec_cmd = app.add_subcommand("record", "Record a filesystem event");
    rec_cmd->add_option("event", rec_event_str,
        "Event type: create | modify | delete | rename")->required();
    rec_cmd->add_option("path", rec_path_str, "File path")->required();
    rec_cmd->add_option("--old-path", rec_old_path_str, "Previous path (rename only)");
    rec_cmd->callback([&]() {
        // Normalise to uppercase for matching
        std::string ev_upper = rec_event_str;
        std::transform(ev_upper.begin(), ev_upper.end(), ev_upper.begin(), ::toupper);

        EventKind kind = event_kind_from_str(ev_upper);
        std::optional<std::filesystem::path> old_p;
        if (!rec_old_path_str.empty()) old_p = rec_old_path_str;

#ifdef _WIN32
        uint32_t pid = GetCurrentProcessId();
#else
        uint32_t pid = static_cast<uint32_t>(getpid());
#endif
        Journal jrnl(db_path);
        auto id = jrnl.append(EventRecord::now(kind, rec_path_str, old_p, pid));
        std::cout << "Recorded event " << id << "\n";
    });

    // ── history ───────────────────────────────
    std::string hist_path_str;
    auto* hist_cmd = app.add_subcommand("history", "Show event history");
    hist_cmd->add_option("--path", hist_path_str, "Filter by file path");
    hist_cmd->callback([&]() {
        Journal jrnl(db_path);
        std::optional<std::filesystem::path> fp;
        if (!hist_path_str.empty()) fp = hist_path_str;

        for (const auto& ev : jrnl.history(fp)) {
            std::cout << "#" << std::setfill('0') << std::setw(4) << ev.id
                      << " " << ev.timestamp
                      << " " << ev.kind
                      << " pid=" << ev.pid;
            if (ev.old_path)
                std::cout << " (from " << ev.old_path->string() << ")";
            std::cout << "\n      " << ev.path.string() << "\n";
        }
    });

    // ── search ────────────────────────────────
    std::string srch_query, srch_root = ".";
    auto* srch_cmd = app.add_subcommand("search", "AI-assisted file search");
    srch_cmd->add_option("query", srch_query, "Natural-language query")->required();
    srch_cmd->add_option("--root", srch_root, "Root directory to search")
            ->capture_default_str();
    srch_cmd->callback([&]() {
        auto schema  = ai_schema(srch_query);
        auto results = search_files(srch_root, schema);
        nlohmann::json out = nlohmann::json::array();
        for (const auto& r : results) {
            out.push_back({
                {"path",       r.path.string()},
                {"file_name",  r.file_name},
                {"extension",  r.extension},
                {"size_bytes", r.size_bytes}
            });
        }
        std::cout << out.dump(2) << "\n";
    });

    // ── serve ─────────────────────────────────
    std::string srv_bind = "127.0.0.1:8787";
    auto* srv_cmd = app.add_subcommand("serve", "Start the local AI-parse API server");
    srv_cmd->add_option("--bind", srv_bind, "Bind address host:port")
           ->capture_default_str();
    srv_cmd->callback([&]() {
        run_server(srv_bind);
    });

    // ── watch ─────────────────────────────────
    std::string watch_root_str;
    auto* watch_cmd = app.add_subcommand("watch",
        "Watch a directory, journal changes, and provide a terminal menu");
    watch_cmd->add_option("root", watch_root_str,
        "Directory to watch")->required();
    watch_cmd->callback([&]() {
        watch(watch_root_str, db_path);
    });

    // ─────────────────────────────────────────
    CLI11_PARSE(app, argc, argv);
    return 0;
}
