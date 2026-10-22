#pragma once

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

#include <string>
#include <memory>
#include <httplib.h>
#include "db.hpp"
#include "cache.hpp"

namespace aidans {

class ClinicServer {
public:
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
    ClinicServer(const std::string& host, int port, const std::string& dbPath, const std::string& staticDir = "",
                 const std::string& redisHost = "127.0.0.1", int redisPort = 6379, bool verbose = true);

    ~ClinicServer() = default;

    /**
     * @brief Registers all HTTP REST API endpoints and middleware handlers.
     */
    void registerRoutes();

    /**
     * @brief Starts the HTTP listener loop. Blocks until stopped or terminated.
     * @return true on clean shutdown, false on bind/listen failure.
     */
    bool start();

    /**
     * @brief Gracefully stops the HTTP server and terminates active worker threads.
     */
    void stop();

    /// Accessor for underlying SQLite database instance
    Database& getDb() { return db_; }

    /// Accessor for cache manager instance
    CacheManager& getCache() { return cache_; }

    /// Accessor for underlying httplib server instance
    httplib::Server& getHttpServer() { return svr_; }

private:
    std::string host_;         ///< Bound host IP
    int port_;                 ///< Bound TCP port
    std::string staticDir_;    ///< Static web portal assets folder
    bool verbose_{true};       ///< Verbose console logging flag
    Database db_;              ///< SQLite transactional persistence layer
    CacheManager cache_;       ///< In-memory cache layer
    httplib::Server svr_;      ///< Multi-threaded HTTP server engine

    // Internal initialization and routing helpers
    void setupLogging();
    void setupCors();
    void setupStaticFiles();
    void setupDoctorRoutes();
    void setupAppointmentRoutes();
    void setupInventoryRoutes();
    void setupPrescriptionRoutes();
    void setupPosRoutes();
    void setupHealthRoutes();

    /**
     * @brief Helper to generate standard 30-minute clinical operating slots
     * (09:00 to 17:00, with 12:00-13:00 lunch hour excluded).
     * @param dateStr Target appointment date (YYYY-MM-DD).
     * @return Vector of formatted slot times (e.g. "09:00", "09:30", ...).
     */
    static std::vector<std::string> generateStandardSlots(const std::string& dateStr);
};

} // namespace aidans
