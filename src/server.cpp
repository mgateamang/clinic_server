/**
 * @file server.cpp
 * @brief HTTP REST Controller & Endpoint Routing Implementation.
 *
 * Implements the core business logic, input validation, request parsing,
 * and JSON serialization for all clinical REST endpoints in AIDANS.
 *
 * Endpoint Domains:
 *  - CORS Pre-routing Handler & HTTP Request Logging Middleware
 *  - Doctors (/api/v1/doctors)
 *  - Scheduling & Appointments (/api/v1/appointments, /available-slots)
 *  - Medical Inventory & Supply Chain (/api/v1/inventory)
 *  - Digital e-Prescriptions (/api/v1/prescriptions)
 *  - Pharmacy Point-of-Sale Checkout (/api/v1/pos/checkout)
 *  - Health Check & Static Web Asset Serving (/health, /)
 */

#include "server.hpp" // Include server.hpp for declarations and types
#include <iostream> // Include iostream for declarations and types
#include <fstream> // Include fstream for declarations and types
#include <sstream> // Include sstream for declarations and types
#include <filesystem> // Include filesystem for declarations and types
#include <regex> // Include regex for declarations and types
#include <chrono> // Include chrono for declarations and types
#include <iomanip> // Include iomanip for declarations and types

namespace fs = std::filesystem; // Assign value to target variable

namespace aidans { // Execute clinical workflow step

// Thread-local timestamp storage to measure microsecond latency per request
static thread_local std::chrono::steady_clock::time_point g_reqStartTime; // Execute clinical workflow step

/**
 * @brief Constructs the ClinicServer, initializing database schema and seed data.
 */
ClinicServer::ClinicServer(const std::string& host, int port, const std::string& dbPath, const std::string& staticDir, // Execute clinical workflow step
                           const std::string& redisHost, int redisPort, bool verbose) // Execute clinical workflow step
    : host_(host), port_(port), staticDir_(staticDir), verbose_(verbose), db_(dbPath), cache_(redisHost, redisPort) { // Execute clinical workflow step
    // Ensure SQLite database tables, indices, and seed data exist
    db_.initSchema(); // Execute clinical workflow step
    db_.seedInitialDataIfEmpty(); // Execute clinical workflow step
} // Close active scope block

void ClinicServer::setupLogging() { // Execute clinical workflow step
    svr_.set_logger([this](const httplib::Request& req, const httplib::Response& res) { // Install structured request/response logging middleware
        if (!verbose_) return; // Evaluate conditional expression to branch execution flow

        auto now = std::chrono::system_clock::now(); // Assign value to target variable
        auto in_time_t = std::chrono::system_clock::to_time_t(now); // Assign value to target variable
        std::stringstream timeStr; // Execute clinical workflow step
        timeStr << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S"); // Execute clinical workflow step

        auto duration = std::chrono::duration_cast<std::chrono::microseconds>( // Assign value to target variable
            std::chrono::steady_clock::now() - g_reqStartTime).count(); // Execute clinical workflow step
        double ms = duration / 1000.0; // Assign value to target variable

        std::string statusColor = "\033[32m"; // green
        if (res.status >= 400 && res.status < 500) statusColor = "\033[33m"; // yellow
        else if (res.status >= 500) statusColor = "\033[31m"; // red

        std::cout << "\033[90m[" << timeStr.str() << "]\033[0m " // Print diagnostic informational message to standard output
                  << "\033[1;36m" << req.method << "\033[0m " // Execute clinical workflow step
                  << req.path; // Execute clinical workflow step

        if (!req.params.empty()) { // Access URL query parameters from HTTP request
            std::cout << "?"; // Print diagnostic informational message to standard output
            bool first = true; // Assign value to target variable
            for (const auto& [k, v] : req.params) { // Access URL query parameters from HTTP request
                if (!first) std::cout << "&"; // Print diagnostic informational message to standard output
                std::cout << k << "=" << v; // Print diagnostic informational message to standard output
                first = false; // Assign value to target variable
            } // Close active scope block
        } // Close active scope block

        std::cout << " -> " << statusColor << res.status << "\033[0m" // Print diagnostic informational message to standard output
                  << " \033[90m(" << std::fixed << std::setprecision(2) << ms << "ms from " // Execute clinical workflow step
                  << req.remote_addr << ")\033[0m"; // Execute clinical workflow step

        if (!req.body.empty() && req.method != "GET") { // Access raw request body payload
            std::string snippet = req.body; // Access raw request body payload
            if (snippet.size() > 180) snippet = snippet.substr(0, 180) + "..."; // Evaluate conditional expression to branch execution flow
            std::cout << "\n    \033[94mPayload:\033[0m " << snippet; // Print diagnostic informational message to standard output
        } // Close active scope block

        if (res.status >= 400 && !res.body.empty()) { // Evaluate conditional expression to branch execution flow
            std::cout << "\n    \033[91mError:\033[0m " << res.body; // Print diagnostic informational message to standard output
        } // Close active scope block

        std::cout << std::endl; // Print diagnostic informational message to standard output
    }); // Execute clinical workflow step
} // Close active scope block

void ClinicServer::setupCors() { // Execute clinical workflow step
    svr_.set_pre_routing_handler([](const httplib::Request& req, httplib::Response& res) { // Install CORS middleware executing prior to endpoint matching
        g_reqStartTime = std::chrono::steady_clock::now(); // Assign value to target variable
        res.set_header("Access-Control-Allow-Origin", "*"); // Set HTTP response header attribute
        res.set_header("Access-Control-Allow-Methods", "GET, POST, PATCH, PUT, DELETE, OPTIONS"); // Set HTTP response header attribute
        res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization, Accept"); // Set HTTP response header attribute
        if (req.method == "OPTIONS") { // Evaluate conditional expression to branch execution flow
            res.status = 204; // Assign HTTP status code to response object
            return httplib::Server::HandlerResponse::Handled; // Return computed result to caller
        } // Close active scope block
        return httplib::Server::HandlerResponse::Unhandled; // Return computed result to caller
    }); // Execute clinical workflow step
} // Close active scope block

void ClinicServer::setupStaticFiles() { // Execute clinical workflow step
    if (staticDir_.empty() || !fs::exists(staticDir_)) { // Evaluate conditional expression to branch execution flow
        return; // Exit current function execution scope
    } // Close active scope block

    // Serve static files from clinic_portal if configured
    svr_.set_mount_point("/", staticDir_); // Execute clinical workflow step
    svr_.Get("/", [this](const httplib::Request&, httplib::Response& res) { // Register HTTP GET endpoint route handler
        std::string indexPath = staticDir_ + "/index.html"; // Assign value to target variable
        if (fs::exists(indexPath)) { // Evaluate conditional expression to branch execution flow
            std::ifstream f(indexPath); // Execute clinical workflow step
            std::stringstream buffer; // Execute clinical workflow step
            buffer << f.rdbuf(); // Execute clinical workflow step
            res.set_content(buffer.str(), "text/html"); // Write payload body and MIME content type to HTTP response
        } else { // Execute default fallback branch
            res.status = 404; // Assign HTTP status code to response object
            res.set_content("index.html not found", "text/plain"); // Write payload body and MIME content type to HTTP response
        } // Close active scope block
    }); // Execute clinical workflow step
} // Close active scope block

std::vector<std::string> ClinicServer::generateStandardSlots(const std::string& dateStr) { // Execute clinical workflow step
    // Clinic hours 09:00 - 17:00, 30-min intervals
    std::vector<std::string> slots; // Execute clinical workflow step
    for (int hour = 9; hour < 17; ++hour) { // Iterate through items in sequence or collection
        char buf1[32], buf2[32]; // Execute clinical workflow step
        std::snprintf(buf1, sizeof(buf1), "%s %02d:00:00", dateStr.c_str(), hour); // Execute clinical workflow step
        std::snprintf(buf2, sizeof(buf2), "%s %02d:30:00", dateStr.c_str(), hour); // Execute clinical workflow step
        slots.push_back(buf1); // Execute clinical workflow step
        slots.push_back(buf2); // Execute clinical workflow step
    } // Close active scope block
    return slots; // Return computed result to caller
} // Close active scope block

void ClinicServer::setupDoctorRoutes() { // Execute clinical workflow step
    // GET /api/v1/doctors
    svr_.Get("/api/v1/doctors", [this](const httplib::Request&, httplib::Response& res) { // Register HTTP GET endpoint route handler
        auto doctors = db_.getDoctors(); // Assign value to target variable
        nlohmann::json j = doctors; // Parse raw JSON string into strongly-typed object
        res.set_content(j.dump(), "application/json"); // Write payload body and MIME content type to HTTP response
    }); // Execute clinical workflow step

    // GET /api/v1/doctors/{doctorId}/available-slots?date=YYYY-MM-DD
    svr_.Get(R"(/api/v1/doctors/(\d+)/available-slots)", [this](const httplib::Request& req, httplib::Response& res) { // Register HTTP endpoint route handler callback
        int64_t doctorId = std::stoll(req.matches[1]); // Assign value to target variable
        if (!req.has_param("date")) { // Evaluate conditional expression to branch execution flow
            res.status = 400; // Assign HTTP status code to response object
            res.set_content(nlohmann::json{{"error", "Query parameter 'date' (YYYY-MM-DD) is required"}}.dump(), "application/json"); // Write payload body and MIME content type to HTTP response
            return; // Exit current function execution scope
        } // Close active scope block

        std::string date = req.get_param_value("date"); // Assign value to target variable
        // Validate date format YYYY-MM-DD
        std::regex dateRegex(R"(^\d{4}-\d{2}-\d{2}$)"); // Execute clinical workflow step
        if (!std::regex_match(date, dateRegex)) { // Evaluate conditional expression to branch execution flow
            res.status = 400; // Assign HTTP status code to response object
            res.set_content(nlohmann::json{{"error", "Invalid date format, expected YYYY-MM-DD"}}.dump(), "application/json"); // Write payload body and MIME content type to HTTP response
            return; // Exit current function execution scope
        } // Close active scope block

        auto doc = db_.getDoctorById(doctorId); // Assign value to target variable
        if (!doc.has_value()) { // Evaluate conditional expression to branch execution flow
            res.status = 404; // Assign HTTP status code to response object
            res.set_content(nlohmann::json{{"error", "Doctor not found"}}.dump(), "application/json"); // Write payload body and MIME content type to HTTP response
            return; // Exit current function execution scope
        } // Close active scope block

        auto standardSlots = generateStandardSlots(date); // Assign value to target variable
        auto bookedSlots = db_.getBookedSlotsForDoctor(doctorId, date); // Assign value to target variable

        // Filter out booked slots
        std::unordered_set<std::string> bookedSet(bookedSlots.begin(), bookedSlots.end()); // Execute clinical workflow step
        std::vector<std::string> availableSlots; // Execute clinical workflow step
        for (const auto& slot : standardSlots) { // Iterate through items in sequence or collection
            if (bookedSet.find(slot) == bookedSet.end()) { // Evaluate conditional expression to branch execution flow
                availableSlots.push_back(slot); // Execute clinical workflow step
            } // Close active scope block
        } // Close active scope block

        nlohmann::json j = availableSlots; // Parse raw JSON string into strongly-typed object
        res.set_content(j.dump(), "application/json"); // Write payload body and MIME content type to HTTP response
    }); // Execute clinical workflow step
} // Close active scope block

void ClinicServer::setupAppointmentRoutes() { // Execute clinical workflow step
    // GET /api/v1/appointments
    svr_.Get("/api/v1/appointments", [this](const httplib::Request& req, httplib::Response& res) { // Register HTTP endpoint route handler callback
        std::optional<std::string> date; // Execute clinical workflow step
        std::optional<std::string> status; // Execute clinical workflow step
        if (req.has_param("date")) date = req.get_param_value("date"); // Evaluate conditional expression to branch execution flow
        if (req.has_param("status")) status = req.get_param_value("status"); // Evaluate conditional expression to branch execution flow

        auto list = db_.getAppointments(date, status); // Assign value to target variable
        nlohmann::json j = list; // Parse raw JSON string into strongly-typed object
        res.set_content(j.dump(), "application/json"); // Write payload body and MIME content type to HTTP response
    }); // Execute clinical workflow step

    // POST /api/v1/appointments
    svr_.Post("/api/v1/appointments", [this](const httplib::Request& req, httplib::Response& res) { // Register HTTP endpoint route handler callback
        try { // Begin guarded block for exception safety
            auto body = nlohmann::json::parse(req.body); // Access raw request body payload
            if (!body.contains("doctor_id") || !body.contains("appointment_time") || // Evaluate conditional expression to branch execution flow
                !body.contains("first_name") || !body.contains("last_name") || !body.contains("phone")) { // Execute clinical workflow step
                res.status = 400; // Assign HTTP status code to response object
                res.set_content(nlohmann::json{{"error", "Missing required fields (doctor_id, appointment_time, first_name, last_name, phone)"}}.dump(), "application/json"); // Write payload body and MIME content type to HTTP response
                return; // Exit current function execution scope
            } // Close active scope block

            Patient p; // Execute clinical workflow step
            p.first_name = body["first_name"].get<std::string>(); // Assign value to target variable
            p.last_name = body["last_name"].get<std::string>(); // Assign value to target variable
            p.phone = body["phone"].get<std::string>(); // Assign value to target variable
            if (body.contains("email")) p.email = body["email"].get<std::string>(); // Evaluate conditional expression to branch execution flow
            if (body.contains("date_of_birth")) p.date_of_birth = body["date_of_birth"].get<std::string>(); // Evaluate conditional expression to branch execution flow
            else p.date_of_birth = "2000-01-01"; // Default fallback if omitted by patient portal

            Appointment appt; // Execute clinical workflow step
            appt.doctor_id = body["doctor_id"].get<int64_t>(); // Assign value to target variable
            appt.appointment_time = body["appointment_time"].get<std::string>(); // Assign value to target variable
            if (body.contains("reason")) appt.reason = body["reason"].get<std::string>(); // Evaluate conditional expression to branch execution flow

            bool ok = db_.bookAppointment(appt, p); // Assign value to target variable
            if (!ok) { // Evaluate conditional expression to branch execution flow
                res.status = 409; // Assign HTTP status code to response object
                res.set_content(nlohmann::json{{"error", "Doctor slot already booked or invalid slot"}}.dump(), "application/json"); // Write payload body and MIME content type to HTTP response
                return; // Exit current function execution scope
            } // Close active scope block

            // Hydrate doctor and patient name
            auto saved = db_.getAppointmentById(appt.id); // Assign value to target variable
            if (saved.has_value()) { // Evaluate conditional expression to branch execution flow
                appt = saved.value(); // Assign value to target variable
            } // Close active scope block

            res.status = 201; // Assign HTTP status code to response object
            nlohmann::json respJson = appt; // Parse raw JSON string into strongly-typed object
            res.set_content(respJson.dump(), "application/json"); // Write payload body and MIME content type to HTTP response
        } catch (const std::exception& e) { // Handle caught exception gracefully
            res.status = 400; // Assign HTTP status code to response object
            res.set_content(nlohmann::json{{"error", std::string("Malformed JSON: ") + e.what()}}.dump(), "application/json"); // Write payload body and MIME content type to HTTP response
        } // Close active scope block
    }); // Execute clinical workflow step

} // Close active scope block

void ClinicServer::setupInventoryRoutes() { // Execute clinical workflow step
    // GET /api/v1/inventory
    svr_.Get("/api/v1/inventory", [this](const httplib::Request& req, httplib::Response& res) { // Register HTTP GET endpoint route handler
        bool lowStockOnly = req.has_param("low_stock_only") && (req.get_param_value("low_stock_only") == "true" || req.get_param_value("low_stock_only") == "1"); // Assign value to target variable
        std::string search = req.has_param("search") ? req.get_param_value("search") : ""; // Assign value to target variable

        auto items = db_.getInventory(lowStockOnly, search); // Assign value to target variable
        nlohmann::json j = items; // Parse raw JSON string into strongly-typed object
        res.set_content(j.dump(), "application/json"); // Write payload body and MIME content type to HTTP response
    }); // Execute clinical workflow step

    // POST /api/v1/inventory
    svr_.Post("/api/v1/inventory", [this](const httplib::Request& req, httplib::Response& res) { // Register HTTP POST endpoint route handler
        try { // Begin guarded block for exception safety
            auto body = nlohmann::json::parse(req.body); // Access raw request body payload
            InventoryItem item = body.get<InventoryItem>(); // Assign value to target variable
            int64_t id = db_.createInventoryItem(item); // Assign value to target variable
            if (id <= 0) { // Evaluate conditional expression to branch execution flow
                res.status = 400; // Assign HTTP status code to response object
                res.set_content(nlohmann::json{{"error", "Failed to create inventory item (duplicate SKU or invalid fields)"}}.dump(), "application/json"); // Write payload body and MIME content type to HTTP response
                return; // Exit current function execution scope
            } // Close active scope block
            item.id = id; // Assign value to target variable
            cache_.publishInventoryUpdate("item_created", id); // Execute clinical workflow step
            res.status = 201; // Assign HTTP status code to response object
            nlohmann::json resp = item; // Parse raw JSON string into strongly-typed object
            res.set_content(resp.dump(), "application/json"); // Write payload body and MIME content type to HTTP response
        } catch (const std::exception& e) { // Handle caught exception gracefully
            res.status = 400; // Assign HTTP status code to response object
            res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json"); // Write payload body and MIME content type to HTTP response
        } // Close active scope block
    }); // Execute clinical workflow step

    // POST /api/v1/inventory/{id}/restock
    svr_.Post(R"(/api/v1/inventory/(\d+)/restock)", [this](const httplib::Request& req, httplib::Response& res) { // Register HTTP endpoint route handler callback
        try { // Begin guarded block for exception safety
            int64_t id = std::stoll(req.matches[1]); // Assign value to target variable
            auto body = nlohmann::json::parse(req.body); // Access raw request body payload
            if (!body.contains("quantity") || !body.contains("reason")) { // Evaluate conditional expression to branch execution flow
                res.status = 400; // Assign HTTP status code to response object
                res.set_content(nlohmann::json{{"error", "Fields 'quantity' and 'reason' are required"}}.dump(), "application/json"); // Write payload body and MIME content type to HTTP response
                return; // Exit current function execution scope
            } // Close active scope block

            int qty = body["quantity"].get<int>(); // Assign value to target variable
            std::string reason = body["reason"].get<std::string>(); // Assign value to target variable

            bool ok = db_.restockItem(id, qty, reason); // Assign value to target variable
            if (!ok) { // Evaluate conditional expression to branch execution flow
                res.status = 400; // Assign HTTP status code to response object
                res.set_content(nlohmann::json{{"error", "Restock failed (invalid item ID or negative resulting stock)"}}.dump(), "application/json"); // Write payload body and MIME content type to HTTP response
                return; // Exit current function execution scope
            } // Close active scope block

            cache_.publishInventoryUpdate("restock", id); // Execute clinical workflow step

            auto item = db_.getInventoryItem(id); // Assign value to target variable
            if (item.has_value()) { // Evaluate conditional expression to branch execution flow
                nlohmann::json resp = item.value(); // Parse raw JSON string into strongly-typed object
                res.set_content(resp.dump(), "application/json"); // Write payload body and MIME content type to HTTP response
            } else { // Execute default fallback branch
                res.set_content(nlohmann::json{{"success", true}}.dump(), "application/json"); // Write payload body and MIME content type to HTTP response
            } // Close active scope block
        } catch (const std::exception& e) { // Handle caught exception gracefully
            res.status = 400; // Assign HTTP status code to response object
            res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json"); // Write payload body and MIME content type to HTTP response
        } // Close active scope block
    }); // Execute clinical workflow step


    // GET /api/v1/inventory/alerts
    svr_.Get("/api/v1/inventory/alerts", [this](const httplib::Request&, httplib::Response& res) { // Register HTTP GET endpoint route handler
        auto alerts = db_.getLowStockAlerts(); // Assign value to target variable
        nlohmann::json j = alerts; // Parse raw JSON string into strongly-typed object
        res.set_content(j.dump(), "application/json"); // Write payload body and MIME content type to HTTP response
    }); // Execute clinical workflow step
} // Close active scope block

void ClinicServer::setupPrescriptionRoutes() { // Execute clinical workflow step
    // GET /api/v1/prescriptions
    svr_.Get("/api/v1/prescriptions", [this](const httplib::Request& req, httplib::Response& res) { // Register HTTP GET endpoint route handler
        std::optional<std::string> status; // Execute clinical workflow step
        std::optional<int64_t> patientId; // Execute clinical workflow step
        if (req.has_param("status")) status = req.get_param_value("status"); // Evaluate conditional expression to branch execution flow
        if (req.has_param("patient_id")) patientId = std::stoll(req.get_param_value("patient_id")); // Evaluate conditional expression to branch execution flow

        auto list = db_.getPrescriptions(status, patientId); // Assign value to target variable
        nlohmann::json j = list; // Parse raw JSON string into strongly-typed object
        res.set_content(j.dump(), "application/json"); // Write payload body and MIME content type to HTTP response
    }); // Execute clinical workflow step

    // POST /api/v1/prescriptions
    svr_.Post("/api/v1/prescriptions", [this](const httplib::Request& req, httplib::Response& res) { // Register HTTP POST endpoint route handler
        try { // Begin guarded block for exception safety
            auto body = nlohmann::json::parse(req.body); // Access raw request body payload
            if (!body.contains("patient_id") || !body.contains("doctor_id") || !body.contains("items")) { // Evaluate conditional expression to branch execution flow
                res.status = 400; // Assign HTTP status code to response object
                res.set_content(nlohmann::json{{"error", "Fields 'patient_id', 'doctor_id', and 'items' are required"}}.dump(), "application/json"); // Write payload body and MIME content type to HTTP response
                return; // Exit current function execution scope
            } // Close active scope block

            Prescription rx; // Execute clinical workflow step
            rx.patient_id = body["patient_id"].get<int64_t>(); // Assign value to target variable
            rx.doctor_id = body["doctor_id"].get<int64_t>(); // Assign value to target variable
            if (body.contains("notes")) rx.notes = body["notes"].get<std::string>(); // Evaluate conditional expression to branch execution flow

            for (const auto& it : body["items"]) { // Iterate through items in sequence or collection
                PrescriptionItem item; // Execute clinical workflow step
                item.inventory_item_id = it["inventory_item_id"].get<int64_t>(); // Assign value to target variable
                item.quantity = it["quantity"].get<int>(); // Assign value to target variable
                item.dosage_instructions = it["dosage_instructions"].get<std::string>(); // Assign value to target variable
                rx.items.push_back(item); // Execute clinical workflow step
            } // Close active scope block

            std::string code = db_.createPrescription(rx); // Assign value to target variable
            if (code.empty()) { // Evaluate conditional expression to branch execution flow
                res.status = 400; // Assign HTTP status code to response object
                res.set_content(nlohmann::json{{"error", "Failed to create prescription"}}.dump(), "application/json"); // Write payload body and MIME content type to HTTP response
                return; // Exit current function execution scope
            } // Close active scope block

            // Push to Redis/Cache FIFO queue
            cache_.pushPendingPrescription(code); // Execute clinical workflow step

            auto created = db_.getPrescriptionByCode(code); // Assign value to target variable
            res.status = 201; // Assign HTTP status code to response object
            nlohmann::json resp = created.value(); // Parse raw JSON string into strongly-typed object
            res.set_content(resp.dump(), "application/json"); // Write payload body and MIME content type to HTTP response
        } catch (const std::exception& e) { // Handle caught exception gracefully
            res.status = 400; // Assign HTTP status code to response object
            res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json"); // Write payload body and MIME content type to HTTP response
        } // Close active scope block
    }); // Execute clinical workflow step

    // GET /api/v1/prescriptions/{code}
    svr_.Get(R"(/api/v1/prescriptions/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) { // Register HTTP endpoint route handler callback
        std::string code = req.matches[1]; // Assign value to target variable
        auto rx = db_.getPrescriptionByCode(code); // Assign value to target variable
        if (!rx.has_value()) { // Evaluate conditional expression to branch execution flow
            res.status = 404; // Assign HTTP status code to response object
            res.set_content(nlohmann::json{{"error", "Prescription not found"}}.dump(), "application/json"); // Write payload body and MIME content type to HTTP response
            return; // Exit current function execution scope
        } // Close active scope block
        nlohmann::json j = rx.value(); // Parse raw JSON string into strongly-typed object
        res.set_content(j.dump(), "application/json"); // Write payload body and MIME content type to HTTP response
    }); // Execute clinical workflow step
} // Close active scope block

void ClinicServer::setupPosRoutes() { // Execute clinical workflow step
    // POST /api/v1/pos/checkout
    svr_.Post("/api/v1/pos/checkout", [this](const httplib::Request& req, httplib::Response& res) { // Register HTTP endpoint route handler callback
        try { // Begin guarded block for exception safety
            auto body = nlohmann::json::parse(req.body); // Access raw request body payload
            SaleCheckoutRequest saleReq = body.get<SaleCheckoutRequest>(); // Assign value to target variable

            SaleReceipt receipt; // Execute clinical workflow step
            auto [ok, errOrTxCode] = db_.processSale(saleReq, receipt); // Assign value to target variable
            if (!ok) { // Evaluate conditional expression to branch execution flow
                res.status = 400; // Assign HTTP status code to response object
                res.set_content(nlohmann::json{{"error", errOrTxCode}}.dump(), "application/json"); // Write payload body and MIME content type to HTTP response
                return; // Exit current function execution scope
            } // Close active scope block

            res.status = 200; // Assign HTTP status code to response object
            nlohmann::json resp = receipt; // Parse raw JSON string into strongly-typed object
            res.set_content(resp.dump(), "application/json"); // Write payload body and MIME content type to HTTP response
        } catch (const std::exception& e) { // Handle caught exception gracefully
            res.status = 400; // Assign HTTP status code to response object
            res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json"); // Write payload body and MIME content type to HTTP response
        } // Close active scope block
    }); // Execute clinical workflow step
} // Close active scope block

void ClinicServer::setupHealthRoutes() { // Execute clinical workflow step
    auto healthHandler = [](const httplib::Request&, httplib::Response& res) { // Assign value to target variable
        nlohmann::json j = { // Parse raw JSON string into strongly-typed object
            {"status", "ok"}, // Execute clinical workflow step
            {"service", "aidans_clinic_server"}, // Execute clinical workflow step
            {"version", "1.0.0"} // Execute clinical workflow step
        }; // Scope delimiter
        res.status = 200; // Assign HTTP status code to response object
        res.set_content(j.dump(), "application/json"); // Write payload body and MIME content type to HTTP response
    }; // Scope delimiter

    svr_.Get("/health", healthHandler); // Register HTTP endpoint route handler callback
    svr_.Get("/api/v1/health", healthHandler); // Register HTTP endpoint route handler callback
} // Close active scope block

void ClinicServer::registerRoutes() { // Execute clinical workflow step
    setupLogging(); // Execute clinical workflow step
    setupCors(); // Execute clinical workflow step
    setupStaticFiles(); // Execute clinical workflow step
    setupDoctorRoutes(); // Execute clinical workflow step
    setupAppointmentRoutes(); // Execute clinical workflow step
    setupInventoryRoutes(); // Execute clinical workflow step
    setupPrescriptionRoutes(); // Execute clinical workflow step
    setupPosRoutes(); // Execute clinical workflow step
    setupHealthRoutes(); // Execute clinical workflow step
} // Close active scope block

bool ClinicServer::start() { // Execute clinical workflow step
    std::cout << "[AIDANS Clinic Server] Listening on " << host_ << ":" << port_ << std::endl; // Print diagnostic informational message to standard output
    return svr_.listen(host_.c_str(), port_); // Return computed result to caller
} // Close active scope block

void ClinicServer::stop() { // Execute clinical workflow step
    svr_.stop(); // Execute clinical workflow step
} // Close active scope block

} // namespace aidans
