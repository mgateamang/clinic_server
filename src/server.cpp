#include "server.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <regex>
#include <chrono>
#include <iomanip>

namespace fs = std::filesystem;

namespace aidans {

static thread_local std::chrono::steady_clock::time_point g_reqStartTime;

ClinicServer::ClinicServer(const std::string& host, int port, const std::string& dbPath, const std::string& staticDir,
                           const std::string& redisHost, int redisPort, bool verbose)
    : host_(host), port_(port), staticDir_(staticDir), verbose_(verbose), db_(dbPath), cache_(redisHost, redisPort) {
    db_.initSchema();
    db_.seedInitialDataIfEmpty();
}

void ClinicServer::setupLogging() {
    svr_.set_logger([this](const httplib::Request& req, const httplib::Response& res) {
        if (!verbose_) return;

        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream timeStr;
        timeStr << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S");

        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - g_reqStartTime).count();
        double ms = duration / 1000.0;

        std::string statusColor = "\033[32m"; // green
        if (res.status >= 400 && res.status < 500) statusColor = "\033[33m"; // yellow
        else if (res.status >= 500) statusColor = "\033[31m"; // red

        std::cout << "\033[90m[" << timeStr.str() << "]\033[0m "
                  << "\033[1;36m" << req.method << "\033[0m "
                  << req.path;

        if (!req.params.empty()) {
            std::cout << "?";
            bool first = true;
            for (const auto& [k, v] : req.params) {
                if (!first) std::cout << "&";
                std::cout << k << "=" << v;
                first = false;
            }
        }

        std::cout << " -> " << statusColor << res.status << "\033[0m"
                  << " \033[90m(" << std::fixed << std::setprecision(2) << ms << "ms from "
                  << req.remote_addr << ")\033[0m";

        if (!req.body.empty() && req.method != "GET") {
            std::string snippet = req.body;
            if (snippet.size() > 180) snippet = snippet.substr(0, 180) + "...";
            std::cout << "\n    \033[94mPayload:\033[0m " << snippet;
        }

        if (res.status >= 400 && !res.body.empty()) {
            std::cout << "\n    \033[91mError:\033[0m " << res.body;
        }

        std::cout << std::endl;
    });
}

void ClinicServer::setupCors() {
    svr_.set_pre_routing_handler([](const httplib::Request& req, httplib::Response& res) {
        g_reqStartTime = std::chrono::steady_clock::now();
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, PATCH, PUT, DELETE, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization, Accept");
        if (req.method == "OPTIONS") {
            res.status = 204;
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });
}

void ClinicServer::setupStaticFiles() {
    if (staticDir_.empty() || !fs::exists(staticDir_)) {
        return;
    }

    // Serve static files from clinic_portal if configured
    svr_.set_mount_point("/", staticDir_);
    svr_.Get("/", [this](const httplib::Request&, httplib::Response& res) {
        std::string indexPath = staticDir_ + "/index.html";
        if (fs::exists(indexPath)) {
            std::ifstream f(indexPath);
            std::stringstream buffer;
            buffer << f.rdbuf();
            res.set_content(buffer.str(), "text/html");
        } else {
            res.status = 404;
            res.set_content("index.html not found", "text/plain");
        }
    });
}

std::vector<std::string> ClinicServer::generateStandardSlots(const std::string& dateStr) {
    // Clinic hours 09:00 - 17:00, 30-min intervals
    std::vector<std::string> slots;
    for (int hour = 9; hour < 17; ++hour) {
        char buf1[32], buf2[32];
        std::snprintf(buf1, sizeof(buf1), "%s %02d:00:00", dateStr.c_str(), hour);
        std::snprintf(buf2, sizeof(buf2), "%s %02d:30:00", dateStr.c_str(), hour);
        slots.push_back(buf1);
        slots.push_back(buf2);
    }
    return slots;
}

void ClinicServer::setupDoctorRoutes() {
    // GET /api/v1/doctors
    svr_.Get("/api/v1/doctors", [this](const httplib::Request&, httplib::Response& res) {
        auto doctors = db_.getDoctors();
        nlohmann::json j = doctors;
        res.set_content(j.dump(), "application/json");
    });

    // GET /api/v1/doctors/{doctorId}/available-slots?date=YYYY-MM-DD
    svr_.Get(R"(/api/v1/doctors/(\d+)/available-slots)", [this](const httplib::Request& req, httplib::Response& res) {
        int64_t doctorId = std::stoll(req.matches[1]);
        if (!req.has_param("date")) {
            res.status = 400;
            res.set_content(nlohmann::json{{"error", "Query parameter 'date' (YYYY-MM-DD) is required"}}.dump(), "application/json");
            return;
        }

        std::string date = req.get_param_value("date");
        // Validate date format YYYY-MM-DD
        std::regex dateRegex(R"(^\d{4}-\d{2}-\d{2}$)");
        if (!std::regex_match(date, dateRegex)) {
            res.status = 400;
            res.set_content(nlohmann::json{{"error", "Invalid date format, expected YYYY-MM-DD"}}.dump(), "application/json");
            return;
        }

        auto doc = db_.getDoctorById(doctorId);
        if (!doc.has_value()) {
            res.status = 404;
            res.set_content(nlohmann::json{{"error", "Doctor not found"}}.dump(), "application/json");
            return;
        }

        auto standardSlots = generateStandardSlots(date);
        auto bookedSlots = db_.getBookedSlotsForDoctor(doctorId, date);

        // Filter out booked slots
        std::unordered_set<std::string> bookedSet(bookedSlots.begin(), bookedSlots.end());
        std::vector<std::string> availableSlots;
        for (const auto& slot : standardSlots) {
            if (bookedSet.find(slot) == bookedSet.end()) {
                availableSlots.push_back(slot);
            }
        }

        nlohmann::json j = availableSlots;
        res.set_content(j.dump(), "application/json");
    });
}

void ClinicServer::setupAppointmentRoutes() {
    // GET /api/v1/appointments
    svr_.Get("/api/v1/appointments", [this](const httplib::Request& req, httplib::Response& res) {
        std::optional<std::string> date;
        std::optional<std::string> status;
        if (req.has_param("date")) date = req.get_param_value("date");
        if (req.has_param("status")) status = req.get_param_value("status");

        auto list = db_.getAppointments(date, status);
        nlohmann::json j = list;
        res.set_content(j.dump(), "application/json");
    });

    // POST /api/v1/appointments
    svr_.Post("/api/v1/appointments", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = nlohmann::json::parse(req.body);
            if (!body.contains("doctor_id") || !body.contains("appointment_time") ||
                !body.contains("first_name") || !body.contains("last_name") || !body.contains("phone")) {
                res.status = 400;
                res.set_content(nlohmann::json{{"error", "Missing required fields (doctor_id, appointment_time, first_name, last_name, phone)"}}.dump(), "application/json");
                return;
            }

            Patient p;
            p.first_name = body["first_name"].get<std::string>();
            p.last_name = body["last_name"].get<std::string>();
            p.phone = body["phone"].get<std::string>();
            if (body.contains("email")) p.email = body["email"].get<std::string>();
            if (body.contains("date_of_birth")) p.date_of_birth = body["date_of_birth"].get<std::string>();
            else p.date_of_birth = "2000-01-01"; // Default fallback if omitted by patient portal

            Appointment appt;
            appt.doctor_id = body["doctor_id"].get<int64_t>();
            appt.appointment_time = body["appointment_time"].get<std::string>();
            if (body.contains("reason")) appt.reason = body["reason"].get<std::string>();

            bool ok = db_.bookAppointment(appt, p);
            if (!ok) {
                res.status = 409;
                res.set_content(nlohmann::json{{"error", "Doctor slot already booked or invalid slot"}}.dump(), "application/json");
                return;
            }

            // Hydrate doctor and patient name
            auto saved = db_.getAppointmentById(appt.id);
            if (saved.has_value()) {
                appt = saved.value();
            }

            res.status = 201;
            nlohmann::json respJson = appt;
            res.set_content(respJson.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json{{"error", std::string("Malformed JSON: ") + e.what()}}.dump(), "application/json");
        }
    });

    // PATCH /api/v1/appointments/{id}/status
    svr_.Patch(R"(/api/v1/appointments/(\d+)/status)", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            int64_t id = std::stoll(req.matches[1]);
            auto body = nlohmann::json::parse(req.body);
            if (!body.contains("status")) {
                res.status = 400;
                res.set_content(nlohmann::json{{"error", "Field 'status' is required"}}.dump(), "application/json");
                return;
            }

            std::string status = body["status"].get<std::string>();
            bool ok = db_.updateAppointmentStatus(id, status);
            if (!ok) {
                res.status = 404;
                res.set_content(nlohmann::json{{"error", "Appointment not found or status update failed"}}.dump(), "application/json");
                return;
            }

            auto appt = db_.getAppointmentById(id);
            if (appt.has_value()) {
                nlohmann::json resp = appt.value();
                res.set_content(resp.dump(), "application/json");
            } else {
                res.set_content(nlohmann::json{{"success", true}}.dump(), "application/json");
            }
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json");
        }
    });
}

void ClinicServer::setupInventoryRoutes() {
    // GET /api/v1/inventory
    svr_.Get("/api/v1/inventory", [this](const httplib::Request& req, httplib::Response& res) {
        bool lowStockOnly = req.has_param("low_stock_only") && (req.get_param_value("low_stock_only") == "true" || req.get_param_value("low_stock_only") == "1");
        std::string search = req.has_param("search") ? req.get_param_value("search") : "";

        auto items = db_.getInventory(lowStockOnly, search);
        nlohmann::json j = items;
        res.set_content(j.dump(), "application/json");
    });

    // POST /api/v1/inventory
    svr_.Post("/api/v1/inventory", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = nlohmann::json::parse(req.body);
            InventoryItem item = body.get<InventoryItem>();
            int64_t id = db_.createInventoryItem(item);
            if (id <= 0) {
                res.status = 400;
                res.set_content(nlohmann::json{{"error", "Failed to create inventory item (duplicate SKU or invalid fields)"}}.dump(), "application/json");
                return;
            }
            item.id = id;
            cache_.publishInventoryUpdate("item_created", id);
            res.status = 201;
            nlohmann::json resp = item;
            res.set_content(resp.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    // POST /api/v1/inventory/{id}/restock
    svr_.Post(R"(/api/v1/inventory/(\d+)/restock)", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            int64_t id = std::stoll(req.matches[1]);
            auto body = nlohmann::json::parse(req.body);
            if (!body.contains("quantity") || !body.contains("reason")) {
                res.status = 400;
                res.set_content(nlohmann::json{{"error", "Fields 'quantity' and 'reason' are required"}}.dump(), "application/json");
                return;
            }

            int qty = body["quantity"].get<int>();
            std::string reason = body["reason"].get<std::string>();

            bool ok = db_.restockItem(id, qty, reason);
            if (!ok) {
                res.status = 400;
                res.set_content(nlohmann::json{{"error", "Restock failed (invalid item ID or negative resulting stock)"}}.dump(), "application/json");
                return;
            }

            cache_.publishInventoryUpdate("restock", id);

            auto item = db_.getInventoryItem(id);
            if (item.has_value()) {
                nlohmann::json resp = item.value();
                res.set_content(resp.dump(), "application/json");
            } else {
                res.set_content(nlohmann::json{{"success", true}}.dump(), "application/json");
            }
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    // POST /api/v1/inventory/publish
    svr_.Post("/api/v1/inventory/publish", [this](const httplib::Request& req, httplib::Response& res) {
        std::string action = "manual_sync";
        int64_t itemId = 0;
        try {
            if (!req.body.empty()) {
                auto b = nlohmann::json::parse(req.body);
                if (b.contains("action")) action = b["action"].get<std::string>();
                if (b.contains("item_id")) itemId = b["item_id"].get<int64_t>();
            }
        } catch (...) {}
        bool ok = cache_.publishInventoryUpdate(action, itemId);
        res.status = 200;
        res.set_content(nlohmann::json{{"published", ok}, {"action", action}, {"item_id", itemId}}.dump(), "application/json");
    });

    // GET /api/v1/inventory/alerts
    svr_.Get("/api/v1/inventory/alerts", [this](const httplib::Request&, httplib::Response& res) {
        auto alerts = db_.getLowStockAlerts();
        nlohmann::json j = alerts;
        res.set_content(j.dump(), "application/json");
    });
}

void ClinicServer::setupPrescriptionRoutes() {
    // GET /api/v1/prescriptions
    svr_.Get("/api/v1/prescriptions", [this](const httplib::Request& req, httplib::Response& res) {
        std::optional<std::string> status;
        std::optional<int64_t> patientId;
        if (req.has_param("status")) status = req.get_param_value("status");
        if (req.has_param("patient_id")) patientId = std::stoll(req.get_param_value("patient_id"));

        auto list = db_.getPrescriptions(status, patientId);
        nlohmann::json j = list;
        res.set_content(j.dump(), "application/json");
    });

    // POST /api/v1/prescriptions
    svr_.Post("/api/v1/prescriptions", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = nlohmann::json::parse(req.body);
            if (!body.contains("patient_id") || !body.contains("doctor_id") || !body.contains("items")) {
                res.status = 400;
                res.set_content(nlohmann::json{{"error", "Fields 'patient_id', 'doctor_id', and 'items' are required"}}.dump(), "application/json");
                return;
            }

            Prescription rx;
            rx.patient_id = body["patient_id"].get<int64_t>();
            rx.doctor_id = body["doctor_id"].get<int64_t>();
            if (body.contains("notes")) rx.notes = body["notes"].get<std::string>();

            for (const auto& it : body["items"]) {
                PrescriptionItem item;
                item.inventory_item_id = it["inventory_item_id"].get<int64_t>();
                item.quantity = it["quantity"].get<int>();
                item.dosage_instructions = it["dosage_instructions"].get<std::string>();
                rx.items.push_back(item);
            }

            std::string code = db_.createPrescription(rx);
            if (code.empty()) {
                res.status = 400;
                res.set_content(nlohmann::json{{"error", "Failed to create prescription"}}.dump(), "application/json");
                return;
            }

            // Push to Redis/Cache FIFO queue
            cache_.pushPendingPrescription(code);

            auto created = db_.getPrescriptionByCode(code);
            res.status = 201;
            nlohmann::json resp = created.value();
            res.set_content(resp.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    // GET /api/v1/prescriptions/{code}
    svr_.Get(R"(/api/v1/prescriptions/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        std::string code = req.matches[1];
        auto rx = db_.getPrescriptionByCode(code);
        if (!rx.has_value()) {
            res.status = 404;
            res.set_content(nlohmann::json{{"error", "Prescription not found"}}.dump(), "application/json");
            return;
        }
        nlohmann::json j = rx.value();
        res.set_content(j.dump(), "application/json");
    });
}

void ClinicServer::setupPosRoutes() {
    // POST /api/v1/pos/checkout
    svr_.Post("/api/v1/pos/checkout", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = nlohmann::json::parse(req.body);
            SaleCheckoutRequest saleReq = body.get<SaleCheckoutRequest>();

            SaleReceipt receipt;
            auto [ok, errOrTxCode] = db_.processSale(saleReq, receipt);
            if (!ok) {
                res.status = 400;
                res.set_content(nlohmann::json{{"error", errOrTxCode}}.dump(), "application/json");
                return;
            }

            if (saleReq.payment_method == "WEBHOOK_MOCK") {
                // Store in ephemeral cache
                nlohmann::json sessionJson = receipt;
                cache_.setPaymentSession(receipt.transaction_code, sessionJson.dump());
            }

            if (!saleReq.prescription_code.empty()) {
                cache_.removePendingPrescription(saleReq.prescription_code);
            }

            // Ripple update inventory to POS and Inventory terminals via Redis
            cache_.publishInventoryUpdate("checkout", 0);

            res.status = 200;
            nlohmann::json resp = receipt;
            res.set_content(resp.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json");
        }
    });
}

void ClinicServer::setupPaymentRoutes() {
    // POST /api/v1/payments/webhook
    svr_.Post("/api/v1/payments/webhook", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = nlohmann::json::parse(req.body);
            if (!body.contains("transaction_code") || !body.contains("status")) {
                res.status = 400;
                res.set_content(nlohmann::json{{"error", "Fields 'transaction_code' and 'status' are required"}}.dump(), "application/json");
                return;
            }

            PaymentWebhookPayload payload = body.get<PaymentWebhookPayload>();
            if (payload.status != "PAID" && payload.status != "FAILED") {
                res.status = 400;
                res.set_content(nlohmann::json{{"error", "Invalid payment status, must be 'PAID' or 'FAILED'"}}.dump(), "application/json");
                return;
            }

            bool ok = db_.updatePaymentStatus(payload.transaction_code, payload.status, payload.gateway_reference);
            if (!ok) {
                res.status = 404;
                res.set_content(nlohmann::json{{"error", "Transaction code not found"}}.dump(), "application/json");
                return;
            }

            cache_.clearPaymentSession(payload.transaction_code);

            res.status = 200;
            res.set_content(nlohmann::json{
                {"received", true},
                {"status", payload.status},
                {"transaction_code", payload.transaction_code}
            }.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json");
        }
    });
}

void ClinicServer::setupReportRoutes() {
    // GET /api/v1/reports/daily-summary?date=YYYY-MM-DD
    svr_.Get("/api/v1/reports/daily-summary", [this](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_param("date")) {
            res.status = 400;
            res.set_content(nlohmann::json{{"error", "Query parameter 'date' (YYYY-MM-DD) is required"}}.dump(), "application/json");
            return;
        }

        std::string date = req.get_param_value("date");
        auto report = db_.getDailySummary(date);
        nlohmann::json j = report;
        res.set_content(j.dump(), "application/json");
    });
}

void ClinicServer::setupHealthRoutes() {
    auto healthHandler = [this](const httplib::Request&, httplib::Response& res) {
        bool redisOk = cache_.isRedisOnline();
        nlohmann::json j = {
            {"status", "ok"},
            {"service", "aidans_clinic_server"},
            {"version", "1.0.0"},
            {"redis", redisOk ? "connected" : "offline"}
        };
        res.status = 200;
        res.set_content(j.dump(), "application/json");
    };

    svr_.Get("/health", healthHandler);
    svr_.Get("/api/v1/health", healthHandler);
}

void ClinicServer::registerRoutes() {
    setupLogging();
    setupCors();
    setupStaticFiles();
    setupDoctorRoutes();
    setupAppointmentRoutes();
    setupInventoryRoutes();
    setupPrescriptionRoutes();
    setupPosRoutes();
    setupPaymentRoutes();
    setupReportRoutes();
    setupHealthRoutes();
}

bool ClinicServer::start() {
    std::cout << "[AIDANS Clinic Server] Listening on " << host_ << ":" << port_ << std::endl;
    return svr_.listen(host_.c_str(), port_);
}

void ClinicServer::stop() {
    svr_.stop();
}

} // namespace aidans
