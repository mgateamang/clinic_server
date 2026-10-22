#pragma once // Ensure header file is included only once per translation unit

/**
 * @file server.hpp
 * @brief REST HTTP Web Server Declaration for AIDANS Healthcare Management.
 *
 * This class hosts the embedded HTTP API server utilizing `httplib::Server`.
 * It provides cross-origin resource sharing (CORS), structured colorized logging,
 * static asset file serving for the web portal, and endpoint route registration.
 *
 * Architectural Features:
 *  - Modular Route Handlers: Endpoint registrations are grouped logically
 *    by domain (Doctors, Appointments, Inventory, Prescriptions, POS, Health).
 *  - Automatic Port Binding & Graceful Termination.
 *  - Standard Schedule Slot Calculations: Automatically computes available
 *    30-minute booking intervals excluding existing appointments.
 */

#include <string> // Include string for declarations and types
#include <memory> // Include memory for declarations and types
#include <httplib.h> // Include httplib.h for declarations and types
#include "db.hpp" // Include db.hpp for declarations and types
#include "cache.hpp" // Include cache.hpp for declarations and types

namespace aidans { // Execute clinical workflow step

class ClinicServer { // Execute clinical workflow step
public: // Execute clinical workflow step
    /**
     * @brief Constructs the clinic server instance.
     * @param host IP network interface to bind (e.g. "0.0.0.0" for all interfaces).
     * @param port Network port to listen on (default 8080).
     * @param dbPath Filesystem path to SQLite database.
     * @param staticDir Directory path containing clinic_portal web assets (optional).
     * @param redisHost Redis cache host (stubbed for lightweight embedded operation).
     * @param redisPort Redis cache port.
     * @param verbose When true, prints structured request logs with latency timings.
     */
    ClinicServer(const std::string& host, int port, const std::string& dbPath, const std::string& staticDir = "", // Assign value to target variable
                 const std::string& redisHost = "127.0.0.1", int redisPort = 6379, bool verbose = true); // Assign value to target variable

    ~ClinicServer() = default; // Assign value to target variable

    /**
     * @brief Registers all HTTP REST API endpoints and middleware handlers.
     */
    void registerRoutes(); // Execute clinical workflow step

    /**
     * @brief Starts the HTTP listener loop. Blocks until stopped or terminated.
     * @return true on clean shutdown, false on bind/listen failure.
     */
    bool start(); // Execute clinical workflow step

    /**
     * @brief Gracefully stops the HTTP server and terminates active worker threads.
     */
    void stop(); // Execute clinical workflow step

    /// Accessor for underlying SQLite database instance
    Database& getDb() { return db_; } // Execute clinical workflow step

    /// Accessor for cache manager instance
    CacheManager& getCache() { return cache_; } // Execute clinical workflow step

    /// Accessor for underlying httplib server instance
    httplib::Server& getHttpServer() { return svr_; } // Execute clinical workflow step

private: // Execute clinical workflow step
    std::string host_;         ///< Bound host IP
    int port_;                 ///< Bound TCP port
    std::string staticDir_;    ///< Static web portal assets folder
    bool verbose_{true};       ///< Verbose console logging flag
    Database db_;              ///< SQLite transactional persistence layer
    CacheManager cache_;       ///< In-memory cache layer
    httplib::Server svr_;      ///< Multi-threaded HTTP server engine

    // Internal initialization and routing helpers
    void setupLogging(); // Execute clinical workflow step
    void setupCors(); // Execute clinical workflow step
    void setupStaticFiles(); // Execute clinical workflow step
    void setupDoctorRoutes(); // Execute clinical workflow step
    void setupAppointmentRoutes(); // Execute clinical workflow step
    void setupInventoryRoutes(); // Execute clinical workflow step
    void setupPrescriptionRoutes(); // Execute clinical workflow step
    void setupPosRoutes(); // Execute clinical workflow step
    void setupHealthRoutes(); // Execute clinical workflow step

    /**
     * @brief Helper to generate standard 30-minute clinical operating slots
     * (09:00 to 17:00, with 12:00-13:00 lunch hour excluded).
     * @param dateStr Target appointment date (YYYY-MM-DD).
     * @return Vector of formatted slot times (e.g. "09:00", "09:30", ...).
     */
    static std::vector<std::string> generateStandardSlots(const std::string& dateStr); // Execute clinical workflow step
}; // Scope delimiter

} // namespace aidans
