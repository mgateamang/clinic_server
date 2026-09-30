#pragma once

#include <string>
#include <memory>
#include <httplib.h>
#include "db.hpp"
#include "cache.hpp"

namespace aidans {

class ClinicServer {
public:
    ClinicServer(const std::string& host, int port, const std::string& dbPath, const std::string& staticDir = "",
                 const std::string& redisHost = "127.0.0.1", int redisPort = 6379, bool verbose = true);
    ~ClinicServer() = default;

    void registerRoutes();
    bool start();
    void stop();

    // Direct access to components for testing
    Database& getDb() { return db_; }
    CacheManager& getCache() { return cache_; }
    httplib::Server& getHttpServer() { return svr_; }

private:
    std::string host_;
    int port_;
    std::string staticDir_;
    bool verbose_{true};
    Database db_;
    CacheManager cache_;
    httplib::Server svr_;

    void setupLogging();
    void setupCors();
    void setupStaticFiles();
    void setupDoctorRoutes();
    void setupAppointmentRoutes();
    void setupInventoryRoutes();
    void setupPrescriptionRoutes();
    void setupPosRoutes();
    void setupPaymentRoutes();
    void setupReportRoutes();
    void setupHealthRoutes();

    static std::vector<std::string> generateStandardSlots(const std::string& dateStr);
};

} // namespace aidans
