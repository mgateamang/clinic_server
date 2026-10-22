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

#include "server.hpp" // Include server.hpp header for declarations and symbols
#include <iostream> // Include iostream header for declarations and symbols
#include <csignal> // Include csignal header for declarations and symbols
#include <string> // Include string header for declarations and symbols

// Global pointer to server instance for clean signal termination
static aidans::ClinicServer* g_server = nullptr; // Assign calculated value to target variable

/**
 * @brief Signal handler callback for graceful shutdown on Ctrl+C or kill.
 * @param signum OS signal code (e.g., SIGINT, SIGTERM).
 */
void signalHandler(int signum) { // Process clinical data operation and advance application state
    std::cout << "\nReceived signal " << signum << ", shutting down clinic_server..." << std::endl; // Write diagnostic log output to stream
    if (g_server) { // Evaluate conditional expression to branch execution flow
        g_server->stop(); // Execute statement and maintain application state
    } // End of enclosing block or namespace scope
} // End of enclosing block or namespace scope

int main(int argc, char* argv[]) { // Process clinical data operation and advance application state
    // Default server configuration parameters
    std::string host = "0.0.0.0"; // Assign calculated value to target variable
    int port = 8080; // Assign calculated value to target variable
    std::string dbPath = "aidans.db"; // Assign calculated value to target variable
    std::string staticDir = "../clinic_portal"; // Assign calculated value to target variable
    std::string redisHost = "127.0.0.1"; // Assign calculated value to target variable
    int redisPort = 6379; // Assign calculated value to target variable
    bool verbose = true; // Assign calculated value to target variable

    // Command-line flag parsing
    for (int i = 1; i < argc; ++i) { // Iterate through collection or index range
        std::string arg = argv[i]; // Assign calculated value to target variable
        if (arg == "--host" && i + 1 < argc) { // Evaluate conditional expression to branch execution flow
            host = argv[++i]; // Assign calculated value to target variable
        } else if (arg == "--port" && i + 1 < argc) { // Check secondary condition if prior branch was not satisfied
            port = std::stoi(argv[++i]); // Assign calculated value to target variable
        } else if (arg == "--db" && i + 1 < argc) { // Check secondary condition if prior branch was not satisfied
            dbPath = argv[++i]; // Assign calculated value to target variable
        } else if (arg == "--static-dir" && i + 1 < argc) { // Check secondary condition if prior branch was not satisfied
            staticDir = argv[++i]; // Assign calculated value to target variable
        } else if (arg == "--redis-host" && i + 1 < argc) { // Check secondary condition if prior branch was not satisfied
            redisHost = argv[++i]; // Assign calculated value to target variable
        } else if (arg == "--redis-port" && i + 1 < argc) { // Check secondary condition if prior branch was not satisfied
            redisPort = std::stoi(argv[++i]); // Assign calculated value to target variable
        } else if (arg == "--verbose") { // Check secondary condition if prior branch was not satisfied
            verbose = true; // Assign calculated value to target variable
        } else if (arg == "--quiet" || arg == "-q") { // Check secondary condition if prior branch was not satisfied
            verbose = false; // Assign calculated value to target variable
        } else if (arg == "--help") { // Check secondary condition if prior branch was not satisfied
            std::cout << "Usage: clinic_server [options]\n" // Write diagnostic log output to stream
                      << "  --host <ip>          Bind IP (default: 0.0.0.0)\n" // Process clinical data operation and advance application state
                      << "  --port <port>        Port number (default: 8080)\n" // Process clinical data operation and advance application state
                      << "  --db <path>          SQLite DB path (default: aidans.db)\n" // Process clinical data operation and advance application state
                      << "  --static-dir <dir>   Static files directory for clinic_portal\n" // Process clinical data operation and advance application state
                      << "  --redis-host <host>  Redis host (default: 127.0.0.1)\n" // Process clinical data operation and advance application state
                      << "  --redis-port <port>  Redis port (default: 6379)\n" // Process clinical data operation and advance application state
                      << "  --verbose            Enable detailed request logging (default: on)\n" // Process clinical data operation and advance application state
                      << "  --quiet, -q          Suppress request/response logging\n" // Process clinical data operation and advance application state
                      << "  --help               Show this help message\n"; // Execute statement and maintain application state
            return 0; // Exit function and return status to caller
        } // End of enclosing block or namespace scope
    } // End of enclosing block or namespace scope

    // Install POSIX / Windows signal handlers
    std::signal(SIGINT, signalHandler); // Execute statement and maintain application state
    std::signal(SIGTERM, signalHandler); // Execute statement and maintain application state

    std::cout << "=========================================================\n" // Write diagnostic log output to stream
              << "       AIDANS Clinic Server (High-Performance C++20)     \n" // Process clinical data operation and advance application state
              << "=========================================================\n" // Assign calculated value to target variable
              << " Listening on  : http://" << host << ":" << port << "\n"
              << " Database file : " << dbPath << "\n" // Process clinical data operation and advance application state
              << " Web Portal dir: " << staticDir << "\n" // Process clinical data operation and advance application state
              << "=========================================================" << std::endl; // Assign calculated value to target variable

    // Instantiate and start server
    aidans::ClinicServer server(host, port, dbPath, staticDir, redisHost, redisPort, verbose); // Execute statement and maintain application state
    g_server = &server; // Assign calculated value to target variable
    server.registerRoutes(); // Execute statement and maintain application state

    if (!server.start()) { // Evaluate conditional expression to branch execution flow
        std::cerr << "Fatal: Failed to start clinic_server on " << host << ":" << port << std::endl; // Write diagnostic log output to stream
        return 1; // Compute and return value to caller
    } // End of enclosing block or namespace scope

    std::cout << "Clinic server stopped cleanly. Goodbye!" << std::endl; // Write diagnostic log output to stream
    return 0; // Exit function and return status to caller
} // End of enclosing block or namespace scope
