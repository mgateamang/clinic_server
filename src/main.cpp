/**
 * @file main.cpp
 * @brief Entry point executable for AIDANS C++20 Clinic Server.
 *
 * Responsibilities:
 *  - Parses command-line arguments (host, port, SQLite DB path, static assets path).
 *  - Registers OS signal handlers (SIGINT, SIGTERM) for graceful server shutdown.
 *  - Initializes database schema, pre-populates initial catalogs if empty.
 *  - Spins up the multi-threaded HTTP server listener.
 */

#include "server.hpp"
#include <iostream>
#include <csignal>
#include <string>

// Global pointer to server instance for clean signal termination
static aidans::ClinicServer* g_server = nullptr;

/**
 * @brief Signal handler callback for graceful shutdown on Ctrl+C or kill.
 * @param signum OS signal code (e.g., SIGINT, SIGTERM).
 */
void signalHandler(int signum) {
    std::cout << "\nReceived signal " << signum << ", shutting down clinic_server..." << std::endl;
    if (g_server) {
        g_server->stop();
    }
}

int main(int argc, char* argv[]) {
    // Default server configuration parameters
    std::string host = "0.0.0.0";
    int port = 8080;
    std::string dbPath = "aidans.db";
    std::string staticDir = "../clinic_portal";
    std::string redisHost = "127.0.0.1";
    int redisPort = 6379;
    bool verbose = true;

    // Command-line flag parsing
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--host" && i + 1 < argc) {
            host = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (arg == "--db" && i + 1 < argc) {
            dbPath = argv[++i];
        } else if (arg == "--static-dir" && i + 1 < argc) {
            staticDir = argv[++i];
        } else if (arg == "--redis-host" && i + 1 < argc) {
            redisHost = argv[++i];
        } else if (arg == "--redis-port" && i + 1 < argc) {
            redisPort = std::stoi(argv[++i]);
        } else if (arg == "--verbose") {
            verbose = true;
        } else if (arg == "--quiet" || arg == "-q") {
            verbose = false;
        } else if (arg == "--help") {
            std::cout << "Usage: clinic_server [options]\n"
                      << "  --host <ip>          Bind IP (default: 0.0.0.0)\n"
                      << "  --port <port>        Port number (default: 8080)\n"
                      << "  --db <path>          SQLite DB path (default: aidans.db)\n"
                      << "  --static-dir <dir>   Static files directory for clinic_portal\n"
                      << "  --redis-host <host>  Redis host (default: 127.0.0.1)\n"
                      << "  --redis-port <port>  Redis port (default: 6379)\n"
                      << "  --verbose            Enable detailed request logging (default: on)\n"
                      << "  --quiet, -q          Suppress request/response logging\n"
                      << "  --help               Show this help message\n";
            return 0;
        }
    }

    // Install POSIX / Windows signal handlers
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    std::cout << "=========================================================\n"
              << "       AIDANS Clinic Server (High-Performance C++20)     \n"
              << "=========================================================\n"
              << " Listening on  : http://" << host << ":" << port << "\n"
              << " Database file : " << dbPath << "\n"
              << " Web Portal dir: " << staticDir << "\n"
              << "=========================================================" << std::endl;

    // Instantiate and start server
    aidans::ClinicServer server(host, port, dbPath, staticDir, redisHost, redisPort, verbose);
    g_server = &server;
    server.registerRoutes();

    if (!server.start()) {
        std::cerr << "Fatal: Failed to start clinic_server on " << host << ":" << port << std::endl;
        return 1;
    }

    std::cout << "Clinic server stopped cleanly. Goodbye!" << std::endl;
    return 0;
}
