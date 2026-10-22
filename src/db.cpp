/**
 * @file db.cpp
 * @brief SQLite3 Data Access Layer Implementation.
 *
 * Implements high-performance, ACID-compliant database persistence for AIDANS:
 *  - WAL mode (Write-Ahead Logging) configuration and busy-timeout handling.
 *  - Parameterized prepared statements preventing SQL injection attacks.
 *  - Relational joins across doctors, patients, appointments, and prescriptions.
 *  - Atomic multi-table transactions for Point-of-Sale checkouts:
 *    stock verification -> stock decrement -> sales audit log -> prescription dispense.
 */

#include "db.hpp"
#include <iostream>
#include <sstream>
#include <chrono>
#include <random>
#include <iomanip>
#include <unordered_set>
#include <map>

namespace aidans {

/**
 * @brief Opens the SQLite database connection handle.
 */
Database::Database(const std::string& dbPath) {
    int rc = sqlite3_open(dbPath.c_str(), &db_);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to open SQLite database: " << sqlite3_errmsg(db_) << std::endl;
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
    }
}

Database::~Database() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool Database::execute(const std::string& sql) {
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error (" << rc << "): " << (errMsg ? errMsg : "unknown") << "\nQuery: " << sql << std::endl;
        if (errMsg) sqlite3_free(errMsg);
        return false;
    }
    return true;
}

std::string Database::generateUniqueCode(const std::string& prefix) {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    static std::mt19937 gen(1337);
    std::uniform_int_distribution<int> dis(1000, 9999);
    std::ostringstream ss;
    ss << prefix << "-" << (ms % 1000000) << "-" << dis(gen);
    return ss.str();
}

bool Database::initSchema() {
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (!db_) return false;

    // Enable WAL, Foreign Keys, and performance pragmas
    execute("PRAGMA journal_mode = WAL;");
    execute("PRAGMA foreign_keys = ON;");
    execute("PRAGMA busy_timeout = 5000;");
    execute("PRAGMA synchronous = NORMAL;");
    execute("PRAGMA cache_size = -64000;");
    execute("PRAGMA temp_store = MEMORY;");

    const char* schema = R"SQL(
        CREATE TABLE IF NOT EXISTS doctors (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            specialization TEXT NOT NULL,
            email TEXT UNIQUE,
            phone TEXT,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP
        );

        CREATE TABLE IF NOT EXISTS patients (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            first_name TEXT NOT NULL,
            last_name TEXT NOT NULL,
            date_of_birth DATE NOT NULL,
            phone TEXT NOT NULL,
            email TEXT,
            address TEXT,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP
        );
        CREATE INDEX IF NOT EXISTS idx_patients_phone ON patients(phone);

        CREATE TABLE IF NOT EXISTS appointments (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            patient_id INTEGER NOT NULL,
            doctor_id INTEGER NOT NULL,
            appointment_time DATETIME NOT NULL,
            reason TEXT,
            status TEXT CHECK(status IN ('PENDING', 'CONFIRMED', 'COMPLETED', 'CANCELLED', 'NO_SHOW')) DEFAULT 'PENDING',
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (patient_id) REFERENCES patients(id) ON DELETE CASCADE,
            FOREIGN KEY (doctor_id) REFERENCES doctors(id) ON DELETE RESTRICT
        );
        CREATE INDEX IF NOT EXISTS idx_appointments_slot ON appointments(doctor_id, appointment_time);
        CREATE INDEX IF NOT EXISTS idx_appointments_patient ON appointments(patient_id);
        CREATE INDEX IF NOT EXISTS idx_appointments_status ON appointments(status);

        CREATE TABLE IF NOT EXISTS inventory_items (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            sku TEXT UNIQUE NOT NULL,
            name TEXT NOT NULL,
            category TEXT NOT NULL,
            unit_price REAL NOT NULL CHECK(unit_price >= 0),
            stock_quantity INTEGER NOT NULL DEFAULT 0 CHECK(stock_quantity >= 0),
            reorder_threshold INTEGER NOT NULL DEFAULT 10 CHECK(reorder_threshold >= 0),
            unit TEXT NOT NULL DEFAULT 'unit',
            updated_at DATETIME DEFAULT CURRENT_TIMESTAMP
        );
        CREATE INDEX IF NOT EXISTS idx_inventory_sku ON inventory_items(sku);

        CREATE TABLE IF NOT EXISTS prescriptions (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            prescription_code TEXT UNIQUE NOT NULL,
            patient_id INTEGER NOT NULL,
            doctor_id INTEGER NOT NULL,
            notes TEXT,
            status TEXT CHECK(status IN ('PENDING', 'FILLED', 'DISPENSED', 'CANCELLED')) DEFAULT 'PENDING',
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (patient_id) REFERENCES patients(id) ON DELETE CASCADE,
            FOREIGN KEY (doctor_id) REFERENCES doctors(id) ON DELETE RESTRICT
        );
        CREATE INDEX IF NOT EXISTS idx_prescriptions_patient ON prescriptions(patient_id);
        CREATE INDEX IF NOT EXISTS idx_prescriptions_status ON prescriptions(status);
        CREATE INDEX IF NOT EXISTS idx_prescriptions_code ON prescriptions(prescription_code);

        CREATE TABLE IF NOT EXISTS prescription_items (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            prescription_id INTEGER NOT NULL,
            inventory_item_id INTEGER NOT NULL,
            quantity INTEGER NOT NULL CHECK(quantity > 0),
            dosage_instructions TEXT NOT NULL,
            FOREIGN KEY (prescription_id) REFERENCES prescriptions(id) ON DELETE CASCADE,
            FOREIGN KEY (inventory_item_id) REFERENCES inventory_items(id) ON DELETE RESTRICT
        );
        CREATE INDEX IF NOT EXISTS idx_prescription_items_rx ON prescription_items(prescription_id);

        CREATE TABLE IF NOT EXISTS sales_transactions (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            transaction_code TEXT UNIQUE NOT NULL,
            prescription_id INTEGER,
            total_amount REAL NOT NULL CHECK(total_amount >= 0),
            payment_method TEXT CHECK(payment_method IN ('CASH', 'CARD', 'E_WALLET', 'WEBHOOK_MOCK')) NOT NULL,
            payment_status TEXT CHECK(payment_status IN ('PENDING', 'PAID', 'FAILED', 'REFUNDED')) DEFAULT 'PENDING',
            payment_reference TEXT,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (prescription_id) REFERENCES prescriptions(id) ON DELETE SET NULL
        );
        CREATE INDEX IF NOT EXISTS idx_sales_code ON sales_transactions(transaction_code);
        CREATE INDEX IF NOT EXISTS idx_sales_created ON sales_transactions(created_at);

        CREATE TABLE IF NOT EXISTS sales_transaction_items (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            transaction_id INTEGER NOT NULL,
            inventory_item_id INTEGER NOT NULL,
            quantity INTEGER NOT NULL CHECK(quantity > 0),
            unit_price REAL NOT NULL CHECK(unit_price >= 0),
            subtotal REAL NOT NULL CHECK(subtotal >= 0),
            FOREIGN KEY (transaction_id) REFERENCES sales_transactions(id) ON DELETE CASCADE,
            FOREIGN KEY (inventory_item_id) REFERENCES inventory_items(id) ON DELETE RESTRICT
        );
    )SQL";

    return execute(schema);
}

void Database::seedInitialDataIfEmpty() {
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (!db_) return;

    sqlite3_stmt* stmt = nullptr;
    int count = 0;
    if (sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM doctors;", -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    if (count == 0) {
        // Seed Doctors
        execute(R"SQL(
            INSERT INTO doctors (name, specialization, email, phone) VALUES
            ('Dr. Elizabeth Becker', 'General Medicine', 'elizabeth.becker@clinic.de', '+49-69-123450'),
            ('Dr. Klaus Schneider', 'Pediatrics & Internal Medicine', 'klaus.schneider@clinic.de', '+49-69-123451'),
            ('Dr. Anna Weber', 'Dermatology & Allergology', 'anna.weber@clinic.de', '+49-69-123452');
        )SQL");

        // Seed Inventory
        execute(R"SQL(
            INSERT INTO inventory_items (sku, name, category, unit_price, stock_quantity, reorder_threshold, unit) VALUES
            ('MED-AMOX-500', 'Amoxicillin 500mg (20 Caps)', 'Antibiotics', 14.50, 45, 15, 'box'),
            ('MED-PARA-500', 'Paracetamol 500mg (30 Tabs)', 'Analgesic', 4.20, 120, 30, 'box'),
            ('MED-IBUP-400', 'Ibuprofen 400mg (20 Tabs)', 'NSAID', 5.80, 80, 25, 'box'),
            ('MED-OMEP-20',  'Omeprazole 20mg (14 Caps)', 'Gastrointestinal', 11.30, 8, 10, 'box'),
            ('MED-CETI-10',  'Cetirizine 10mg (10 Tabs)', 'Antihistamine', 6.50, 50, 15, 'box'),
            ('SUP-SYR-05ML', 'Sterile Syringes 5ml (100 pcs)', 'Supplies', 18.00, 12, 10, 'box'),
            ('SUP-GAUZ-STR', 'Sterile Gauze Bandage 10cm', 'Supplies', 2.10, 5, 20, 'roll'),
            ('SUP-ALCO-70P', 'Medical Isopropyl Alcohol 70% 500ml', 'Antiseptics', 7.50, 22, 10, 'bottle');
        )SQL");
    }
}

std::vector<Doctor> Database::getDoctors() {
    std::lock_guard<std::mutex> lock(dbMutex_);
    std::vector<Doctor> doctors;
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, name, specialization, email, phone FROM doctors ORDER BY name ASC;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            Doctor doc;
            doc.id = sqlite3_column_int64(stmt, 0);
            doc.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            doc.specialization = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            const char* email = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            if (email) doc.email = email;
            const char* phone = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            if (phone) doc.phone = phone;
            doctors.push_back(doc);
        }
        sqlite3_finalize(stmt);
    }
    return doctors;
}

std::optional<Doctor> Database::getDoctorById(int64_t id) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, name, specialization, email, phone FROM doctors WHERE id = ?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            Doctor doc;
            doc.id = sqlite3_column_int64(stmt, 0);
            doc.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            doc.specialization = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            const char* email = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            if (email) doc.email = email;
            const char* phone = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            if (phone) doc.phone = phone;
            sqlite3_finalize(stmt);
            return doc;
        }
        sqlite3_finalize(stmt);
    }
    return std::nullopt;
}

int64_t Database::createDoctor(const Doctor& doc) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO doctors (name, specialization, email, phone) VALUES (?, ?, ?, ?);";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, doc.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, doc.specialization.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, doc.email.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, doc.phone.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_DONE) {
            int64_t id = sqlite3_last_insert_rowid(db_);
            sqlite3_finalize(stmt);
            return id;
        }
        sqlite3_finalize(stmt);
    }
    return -1;
}

int64_t Database::findOrCreatePatient(const Patient& patient) {
    if (patient.phone.empty() || patient.first_name.empty()) {
        return -1;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* findSql = "SELECT id FROM patients WHERE phone = ? LIMIT 1;";
    if (sqlite3_prepare_v2(db_, findSql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, patient.phone.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int64_t id = sqlite3_column_int64(stmt, 0);
            sqlite3_finalize(stmt);
            return id;
        }
        sqlite3_finalize(stmt);
    }

    std::string dob = patient.date_of_birth.empty() ? "1990-01-01" : patient.date_of_birth;

    const char* insertSql = "INSERT INTO patients (first_name, last_name, date_of_birth, phone, email, address) VALUES (?, ?, ?, ?, ?, ?);";
    if (sqlite3_prepare_v2(db_, insertSql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, patient.first_name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, patient.last_name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, dob.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, patient.phone.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, patient.email.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, patient.address.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_DONE) {
            int64_t id = sqlite3_last_insert_rowid(db_);
            sqlite3_finalize(stmt);
            return id;
        }
        sqlite3_finalize(stmt);
    }
    return -1;
}

std::optional<Patient> Database::getPatientById(int64_t id) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, first_name, last_name, date_of_birth, phone, email, address FROM patients WHERE id = ?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            Patient p;
            p.id = sqlite3_column_int64(stmt, 0);
            p.first_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            p.last_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            p.date_of_birth = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            p.phone = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            const char* email = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            if (email) p.email = email;
            const char* address = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
            if (address) p.address = address;
            sqlite3_finalize(stmt);
            return p;
        }
        sqlite3_finalize(stmt);
    }
    return std::nullopt;
}

std::vector<std::string> Database::getBookedSlotsForDoctor(int64_t doctorId, const std::string& date) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    std::vector<std::string> bookedSlots;
    sqlite3_stmt* stmt = nullptr;
    std::string sql = "SELECT appointment_time FROM appointments WHERE doctor_id = ? AND strftime('%Y-%m-%d', appointment_time) = ? AND status != 'CANCELLED';";
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, doctorId);
        sqlite3_bind_text(stmt, 2, date.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            bookedSlots.push_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        }
        sqlite3_finalize(stmt);
    }
    return bookedSlots;
}

std::vector<Appointment> Database::getAppointments(const std::optional<std::string>& date,
                                                  const std::optional<std::string>& status) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    std::vector<Appointment> list;
    std::string sql = R"SQL(
        SELECT a.id, a.patient_id, a.doctor_id, a.appointment_time, a.reason, a.status, a.created_at,
               (p.first_name || ' ' || p.last_name) AS patient_name, p.phone AS patient_phone, d.name AS doctor_name
        FROM appointments a
        JOIN patients p ON a.patient_id = p.id
        JOIN doctors d ON a.doctor_id = d.id
        WHERE 1=1
    )SQL";

    if (date.has_value()) {
        sql += " AND strftime('%Y-%m-%d', a.appointment_time) = ?";
    }
    if (status.has_value()) {
        sql += " AND a.status = ?";
    }
    sql += " ORDER BY a.appointment_time ASC;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        int paramIdx = 1;
        if (date.has_value()) {
            sqlite3_bind_text(stmt, paramIdx++, date.value().c_str(), -1, SQLITE_TRANSIENT);
        }
        if (status.has_value()) {
            sqlite3_bind_text(stmt, paramIdx++, status.value().c_str(), -1, SQLITE_TRANSIENT);
        }
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            Appointment a;
            a.id = sqlite3_column_int64(stmt, 0);
            a.patient_id = sqlite3_column_int64(stmt, 1);
            a.doctor_id = sqlite3_column_int64(stmt, 2);
            a.appointment_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            const char* reason = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            if (reason) a.reason = reason;
            a.status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            a.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
            a.patient_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
            const char* phone = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
            if (phone) a.patient_phone = phone;
            a.doctor_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
            list.push_back(a);
        }
        sqlite3_finalize(stmt);
    }
    return list;
}

std::optional<Appointment> Database::getAppointmentById(int64_t id) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"SQL(
        SELECT a.id, a.patient_id, a.doctor_id, a.appointment_time, a.reason, a.status, a.created_at,
               (p.first_name || ' ' || p.last_name) AS patient_name, p.phone AS patient_phone, d.name AS doctor_name
        FROM appointments a
        JOIN patients p ON a.patient_id = p.id
        JOIN doctors d ON a.doctor_id = d.id
        WHERE a.id = ?;
    )SQL";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            Appointment a;
            a.id = sqlite3_column_int64(stmt, 0);
            a.patient_id = sqlite3_column_int64(stmt, 1);
            a.doctor_id = sqlite3_column_int64(stmt, 2);
            a.appointment_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            const char* reason = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            if (reason) a.reason = reason;
            a.status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            a.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
            a.patient_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
            const char* phone = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
            if (phone) a.patient_phone = phone;
            a.doctor_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
            sqlite3_finalize(stmt);
            return a;
        }
        sqlite3_finalize(stmt);
    }
    return std::nullopt;
}

bool Database::bookAppointment(Appointment& appt, const Patient& patientInfo) {
    if (appt.doctor_id <= 0 || appt.appointment_time.empty() || patientInfo.phone.empty()) {
        return false;
    }
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (!db_) return false;

    execute("BEGIN IMMEDIATE TRANSACTION;");

    // 1. Resolve patient ID
    int64_t pId = findOrCreatePatient(patientInfo);
    if (pId <= 0) {
        execute("ROLLBACK;");
        return false;
    }
    appt.patient_id = pId;

    // 2. Conflict prevention: check if slot already booked for doctor
    sqlite3_stmt* stmt = nullptr;
    const char* checkSql = "SELECT id FROM appointments WHERE doctor_id = ? AND appointment_time = ? AND status != 'CANCELLED';";
    if (sqlite3_prepare_v2(db_, checkSql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, appt.doctor_id);
        sqlite3_bind_text(stmt, 2, appt.appointment_time.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            sqlite3_finalize(stmt);
            execute("ROLLBACK;");
            return false; // Slot already booked
        }
        sqlite3_finalize(stmt);
    }

    // 3. Insert appointment
    const char* insertSql = "INSERT INTO appointments (patient_id, doctor_id, appointment_time, reason, status) VALUES (?, ?, ?, ?, 'CONFIRMED');";
    if (sqlite3_prepare_v2(db_, insertSql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, appt.patient_id);
        sqlite3_bind_int64(stmt, 2, appt.doctor_id);
        sqlite3_bind_text(stmt, 3, appt.appointment_time.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, appt.reason.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_DONE) {
            appt.id = sqlite3_last_insert_rowid(db_);
            appt.status = "CONFIRMED";
            sqlite3_finalize(stmt);
            execute("COMMIT;");
            return true;
        }
        sqlite3_finalize(stmt);
    }

    execute("ROLLBACK;");
    return false;
}

std::vector<InventoryItem> Database::getInventory(bool lowStockOnly, const std::string& search) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    std::vector<InventoryItem> list;
    std::string sql = "SELECT id, sku, name, category, unit_price, stock_quantity, reorder_threshold, unit, updated_at FROM inventory_items WHERE 1=1";
    if (lowStockOnly) {
        sql += " AND stock_quantity <= reorder_threshold";
    }
    if (!search.empty()) {
        sql += " AND (name LIKE ? OR sku LIKE ? OR category LIKE ?)";
    }
    sql += " ORDER BY name ASC;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (!search.empty()) {
            std::string pattern = "%" + search + "%";
            sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, pattern.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, pattern.c_str(), -1, SQLITE_TRANSIENT);
        }
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            InventoryItem item;
            item.id = sqlite3_column_int64(stmt, 0);
            item.sku = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            item.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            item.category = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            item.unit_price = sqlite3_column_double(stmt, 4);
            item.stock_quantity = sqlite3_column_int(stmt, 5);
            item.reorder_threshold = sqlite3_column_int(stmt, 6);
            item.unit = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
            item.updated_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
            list.push_back(item);
        }
        sqlite3_finalize(stmt);
    }
    return list;
}

std::optional<InventoryItem> Database::getInventoryItem(int64_t id) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, sku, name, category, unit_price, stock_quantity, reorder_threshold, unit, updated_at FROM inventory_items WHERE id = ?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            InventoryItem item;
            item.id = sqlite3_column_int64(stmt, 0);
            item.sku = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            item.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            item.category = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            item.unit_price = sqlite3_column_double(stmt, 4);
            item.stock_quantity = sqlite3_column_int(stmt, 5);
            item.reorder_threshold = sqlite3_column_int(stmt, 6);
            item.unit = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
            item.updated_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
            sqlite3_finalize(stmt);
            return item;
        }
        sqlite3_finalize(stmt);
    }
    return std::nullopt;
}

std::optional<InventoryItem> Database::getInventoryItemBySku(const std::string& sku) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, sku, name, category, unit_price, stock_quantity, reorder_threshold, unit, updated_at FROM inventory_items WHERE sku = ?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, sku.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            InventoryItem item;
            item.id = sqlite3_column_int64(stmt, 0);
            item.sku = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            item.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            item.category = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            item.unit_price = sqlite3_column_double(stmt, 4);
            item.stock_quantity = sqlite3_column_int(stmt, 5);
            item.reorder_threshold = sqlite3_column_int(stmt, 6);
            item.unit = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
            item.updated_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
            sqlite3_finalize(stmt);
            return item;
        }
        sqlite3_finalize(stmt);
    }
    return std::nullopt;
}

int64_t Database::createInventoryItem(const InventoryItem& item) {
    if (item.sku.empty() || item.name.empty() || item.unit_price < 0.0 || item.stock_quantity < 0 || item.reorder_threshold < 0) {
        return -1;
    }
    std::lock_guard<std::mutex> lock(dbMutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO inventory_items (sku, name, category, unit_price, stock_quantity, reorder_threshold, unit) VALUES (?, ?, ?, ?, ?, ?, ?);";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, item.sku.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, item.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, item.category.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 4, item.unit_price);
        sqlite3_bind_int(stmt, 5, item.stock_quantity);
        sqlite3_bind_int(stmt, 6, item.reorder_threshold);
        sqlite3_bind_text(stmt, 7, item.unit.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_DONE) {
            int64_t id = sqlite3_last_insert_rowid(db_);
            sqlite3_finalize(stmt);
            return id;
        }
        sqlite3_finalize(stmt);
    }
    return -1;
}

bool Database::restockItem(int64_t id, int delta, const std::string& reason) {
    if (id <= 0 || delta == 0) return false;
    std::lock_guard<std::mutex> lock(dbMutex_);
    execute("BEGIN IMMEDIATE TRANSACTION;");

    // Fetch current stock
    int currentStock = 0;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT stock_quantity FROM inventory_items WHERE id = ?;", -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            currentStock = sqlite3_column_int(stmt, 0);
        } else {
            sqlite3_finalize(stmt);
            execute("ROLLBACK;");
            return false;
        }
        sqlite3_finalize(stmt);
    }

    int newStock = currentStock + delta;
    if (newStock < 0) {
        execute("ROLLBACK;");
        return false;
    }

    // Update stock
    const char* updateSql = "UPDATE inventory_items SET stock_quantity = ?, updated_at = CURRENT_TIMESTAMP WHERE id = ?;";
    if (sqlite3_prepare_v2(db_, updateSql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, newStock);
        sqlite3_bind_int64(stmt, 2, id);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            sqlite3_finalize(stmt);
            execute("ROLLBACK;");
            return false;
        }
        sqlite3_finalize(stmt);
    }

    execute("COMMIT;");
    return true;
}

std::vector<InventoryItem> Database::getLowStockAlerts() {
    return getInventory(true);
}

std::string Database::createPrescription(const Prescription& rx) {
    if (rx.patient_id <= 0 || rx.doctor_id <= 0 || rx.items.empty()) {
        return "";
    }
    for (const auto& it : rx.items) {
        if (it.inventory_item_id <= 0 || it.quantity <= 0) {
            return "";
        }
    }

    std::lock_guard<std::mutex> lock(dbMutex_);
    execute("BEGIN IMMEDIATE TRANSACTION;");

    std::string rxCode = generateUniqueCode("RX");
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO prescriptions (prescription_code, patient_id, doctor_id, notes, status) VALUES (?, ?, ?, ?, 'PENDING');";
    int64_t rxId = -1;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, rxCode.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 2, rx.patient_id);
        sqlite3_bind_int64(stmt, 3, rx.doctor_id);
        sqlite3_bind_text(stmt, 4, rx.notes.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_DONE) {
            rxId = sqlite3_last_insert_rowid(db_);
        }
        sqlite3_finalize(stmt);
    }

    if (rxId <= 0) {
        execute("ROLLBACK;");
        return "";
    }

    // Insert prescription items
    const char* itemSql = "INSERT INTO prescription_items (prescription_id, inventory_item_id, quantity, dosage_instructions) VALUES (?, ?, ?, ?);";
    for (const auto& it : rx.items) {
        if (sqlite3_prepare_v2(db_, itemSql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, rxId);
            sqlite3_bind_int64(stmt, 2, it.inventory_item_id);
            sqlite3_bind_int(stmt, 3, it.quantity);
            sqlite3_bind_text(stmt, 4, it.dosage_instructions.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) != SQLITE_DONE) {
                sqlite3_finalize(stmt);
                execute("ROLLBACK;");
                return "";
            }
            sqlite3_finalize(stmt);
        }
    }

    execute("COMMIT;");
    return rxCode;
}

std::vector<Prescription> Database::getPrescriptions(const std::optional<std::string>& status,
                                                    std::optional<int64_t> patientId) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    std::vector<Prescription> list;
    std::string sql = R"SQL(
        SELECT pr.id, pr.prescription_code, pr.patient_id, pr.doctor_id, pr.notes, pr.status, pr.created_at,
               (p.first_name || ' ' || p.last_name) AS patient_name, d.name AS doctor_name
        FROM prescriptions pr
        JOIN patients p ON pr.patient_id = p.id
        JOIN doctors d ON pr.doctor_id = d.id
        WHERE 1=1
    )SQL";
    if (status.has_value()) {
        sql += " AND pr.status = ?";
    }
    if (patientId.has_value()) {
        sql += " AND pr.patient_id = ?";
    }
    sql += " ORDER BY pr.created_at DESC;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        int paramIdx = 1;
        if (status.has_value()) {
            sqlite3_bind_text(stmt, paramIdx++, status.value().c_str(), -1, SQLITE_TRANSIENT);
        }
        if (patientId.has_value()) {
            sqlite3_bind_int64(stmt, paramIdx++, patientId.value());
        }
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            Prescription rx;
            rx.id = sqlite3_column_int64(stmt, 0);
            rx.prescription_code = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            rx.patient_id = sqlite3_column_int64(stmt, 2);
            rx.doctor_id = sqlite3_column_int64(stmt, 3);
            const char* notes = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            if (notes) rx.notes = notes;
            rx.status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            rx.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
            rx.patient_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
            rx.doctor_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
            list.push_back(rx);
        }
        sqlite3_finalize(stmt);
    }

    // Hydrate prescription items using single prepared statement
    const char* itemsSql = R"SQL(
        SELECT pi.id, pi.prescription_id, pi.inventory_item_id, inv.name, pi.quantity, pi.dosage_instructions
        FROM prescription_items pi
        JOIN inventory_items inv ON pi.inventory_item_id = inv.id
        WHERE pi.prescription_id = ?;
    )SQL";
    if (sqlite3_prepare_v2(db_, itemsSql, -1, &stmt, nullptr) == SQLITE_OK) {
        for (auto& rx : list) {
            sqlite3_reset(stmt);
            sqlite3_bind_int64(stmt, 1, rx.id);
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                PrescriptionItem it;
                it.id = sqlite3_column_int64(stmt, 0);
                it.prescription_id = sqlite3_column_int64(stmt, 1);
                it.inventory_item_id = sqlite3_column_int64(stmt, 2);
                it.item_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
                it.quantity = sqlite3_column_int(stmt, 4);
                it.dosage_instructions = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
                rx.items.push_back(it);
            }
        }
        sqlite3_finalize(stmt);
    }

    return list;
}

std::optional<Prescription> Database::getPrescriptionByCode(const std::string& code) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"SQL(
        SELECT pr.id, pr.prescription_code, pr.patient_id, pr.doctor_id, pr.notes, pr.status, pr.created_at,
               (p.first_name || ' ' || p.last_name) AS patient_name, d.name AS doctor_name
        FROM prescriptions pr
        JOIN patients p ON pr.patient_id = p.id
        JOIN doctors d ON pr.doctor_id = d.id
        WHERE pr.prescription_code = ?;
    )SQL";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, code.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            Prescription rx;
            rx.id = sqlite3_column_int64(stmt, 0);
            rx.prescription_code = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            rx.patient_id = sqlite3_column_int64(stmt, 2);
            rx.doctor_id = sqlite3_column_int64(stmt, 3);
            const char* notes = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            if (notes) rx.notes = notes;
            rx.status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            rx.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
            rx.patient_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
            rx.doctor_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
            sqlite3_finalize(stmt);

            // Fetch items
            const char* itemsSql = R"SQL(
                SELECT pi.id, pi.prescription_id, pi.inventory_item_id, inv.name, pi.quantity, pi.dosage_instructions
                FROM prescription_items pi
                JOIN inventory_items inv ON pi.inventory_item_id = inv.id
                WHERE pi.prescription_id = ?;
            )SQL";
            if (sqlite3_prepare_v2(db_, itemsSql, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_int64(stmt, 1, rx.id);
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    PrescriptionItem it;
                    it.id = sqlite3_column_int64(stmt, 0);
                    it.prescription_id = sqlite3_column_int64(stmt, 1);
                    it.inventory_item_id = sqlite3_column_int64(stmt, 2);
                    it.item_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
                    it.quantity = sqlite3_column_int(stmt, 4);
                    it.dosage_instructions = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
                    rx.items.push_back(it);
                }
                sqlite3_finalize(stmt);
            }
            return rx;
        }
        sqlite3_finalize(stmt);
    }
    return std::nullopt;
}

bool Database::updatePrescriptionStatus(int64_t id, const std::string& status) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "UPDATE prescriptions SET status = ? WHERE id = ?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, status.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 2, id);
        bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return ok;
    }
    return false;
}

std::pair<bool, std::string> Database::processSale(const SaleCheckoutRequest& req, SaleReceipt& outReceipt) {
    if (req.items.empty()) {
        return {false, "Sale must contain at least one item"};
    }

    // Consolidate duplicate item IDs and validate quantities
    std::map<int64_t, int> consolidatedItems;
    for (const auto& itemInput : req.items) {
        if (itemInput.inventory_item_id <= 0) {
            return {false, "Invalid inventory item ID"};
        }
        if (itemInput.quantity <= 0) {
            return {false, "Item quantity must be greater than zero"};
        }
        consolidatedItems[itemInput.inventory_item_id] += itemInput.quantity;
    }

    std::lock_guard<std::mutex> lock(dbMutex_);
    execute("BEGIN IMMEDIATE TRANSACTION;");

    int64_t prescriptionId = 0;
    if (!req.prescription_code.empty()) {
        sqlite3_stmt* stmt = nullptr;
        const char* pSql = "SELECT id, status FROM prescriptions WHERE prescription_code = ?;";
        if (sqlite3_prepare_v2(db_, pSql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, req.prescription_code.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                prescriptionId = sqlite3_column_int64(stmt, 0);
                std::string status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                if (status == "DISPENSED") {
                    sqlite3_finalize(stmt);
                    execute("ROLLBACK;");
                    return {false, "Prescription has already been dispensed"};
                }
            } else {
                sqlite3_finalize(stmt);
                execute("ROLLBACK;");
                return {false, "Prescription code not found"};
            }
            sqlite3_finalize(stmt);
        }
    }

    // Validate items and calculate total while checking stock
    double totalAmount = 0.0;
    struct ItemCalc {
        int64_t id;
        std::string name;
        int requestedQty;
        int currentStock;
        double unitPrice;
        double subtotal;
    };
    std::vector<ItemCalc> validatedItems;

    for (const auto& [itemId, requestedQty] : consolidatedItems) {
        sqlite3_stmt* stmt = nullptr;
        const char* iSql = "SELECT name, unit_price, stock_quantity FROM inventory_items WHERE id = ?;";
        if (sqlite3_prepare_v2(db_, iSql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, itemId);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                ItemCalc ic;
                ic.id = itemId;
                ic.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                ic.unitPrice = sqlite3_column_double(stmt, 1);
                ic.currentStock = sqlite3_column_int(stmt, 2);
                ic.requestedQty = requestedQty;
                ic.subtotal = ic.unitPrice * ic.requestedQty;

                if (ic.requestedQty > ic.currentStock) {
                    sqlite3_finalize(stmt);
                    execute("ROLLBACK;");
                    return {false, "Insufficient stock for: " + ic.name + " (Available: " + std::to_string(ic.currentStock) + ")"};
                }

                totalAmount += ic.subtotal;
                validatedItems.push_back(ic);
            } else {
                sqlite3_finalize(stmt);
                execute("ROLLBACK;");
                return {false, "Inventory item ID " + std::to_string(itemId) + " does not exist"};
            }
            sqlite3_finalize(stmt);
        }
    }

    if (validatedItems.empty()) {
        execute("ROLLBACK;");
        return {false, "Sale must contain at least one item"};
    }

    std::string txCode = generateUniqueCode("TX");
    std::string paymentStatus = "PAID";

    // Insert sales_transactions
    sqlite3_stmt* stmt = nullptr;
    const char* txSql = "INSERT INTO sales_transactions (transaction_code, prescription_id, total_amount, payment_method, payment_status) VALUES (?, ?, ?, ?, ?);";
    int64_t txId = 0;
    if (sqlite3_prepare_v2(db_, txSql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, txCode.c_str(), -1, SQLITE_TRANSIENT);
        if (prescriptionId > 0) {
            sqlite3_bind_int64(stmt, 2, prescriptionId);
        } else {
            sqlite3_bind_null(stmt, 2);
        }
        sqlite3_bind_double(stmt, 3, totalAmount);
        sqlite3_bind_text(stmt, 4, req.payment_method.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, paymentStatus.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_DONE) {
            txId = sqlite3_last_insert_rowid(db_);
        }
        sqlite3_finalize(stmt);
    }

    if (txId <= 0) {
        execute("ROLLBACK;");
        return {false, "Failed to record sales transaction"};
    }

    // Insert transaction line items & deduct stock atomically
    for (const auto& item : validatedItems) {
        const char* liSql = "INSERT INTO sales_transaction_items (transaction_id, inventory_item_id, quantity, unit_price, subtotal) VALUES (?, ?, ?, ?, ?);";
        if (sqlite3_prepare_v2(db_, liSql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, txId);
            sqlite3_bind_int64(stmt, 2, item.id);
            sqlite3_bind_int(stmt, 3, item.requestedQty);
            sqlite3_bind_double(stmt, 4, item.unitPrice);
            sqlite3_bind_double(stmt, 5, item.subtotal);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }

        int newStock = item.currentStock - item.requestedQty;
        const char* deductSql = "UPDATE inventory_items SET stock_quantity = ?, updated_at = CURRENT_TIMESTAMP WHERE id = ?;";
        if (sqlite3_prepare_v2(db_, deductSql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, newStock);
            sqlite3_bind_int64(stmt, 2, item.id);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }

        SaleReceiptItem ri;
        ri.name = item.name;
        ri.quantity = item.requestedQty;
        ri.unit_price = item.unitPrice;
        ri.subtotal = item.subtotal;
        outReceipt.items.push_back(ri);
    }

    if (prescriptionId > 0) {
        const char* rxUpSql = "UPDATE prescriptions SET status = 'DISPENSED' WHERE id = ?;";
        if (sqlite3_prepare_v2(db_, rxUpSql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, prescriptionId);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    execute("COMMIT;");

    outReceipt.transaction_code = txCode;
    outReceipt.total_amount = totalAmount;
    outReceipt.payment_status = paymentStatus;
    outReceipt.created_at = "JUST NOW";

    return {true, txCode};
}

} // namespace aidans
