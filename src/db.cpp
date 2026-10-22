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

#include "db.hpp" // Include db.hpp for declarations and types
#include <iostream> // Include iostream for declarations and types
#include <sstream> // Include sstream for declarations and types
#include <chrono> // Include chrono for declarations and types
#include <random> // Include random for declarations and types
#include <iomanip> // Include iomanip for declarations and types
#include <unordered_set> // Include unordered_set for declarations and types
#include <map> // Include map for declarations and types

namespace aidans { // Execute clinical workflow step

/**
 * @brief Opens the SQLite database connection handle.
 */
Database::Database(const std::string& dbPath) { // Execute clinical workflow step
    int rc = sqlite3_open(dbPath.c_str(), &db_); // Open SQLite database connection handle to target file
    if (rc != SQLITE_OK) { // Evaluate conditional expression to branch execution flow
        std::cerr << "Failed to open SQLite database: " << sqlite3_errmsg(db_) << std::endl; // Print error alert message to standard error stream
        if (db_) { // Evaluate conditional expression to branch execution flow
            sqlite3_close(db_); // Close SQLite connection and flush all WAL journal buffers
            db_ = nullptr; // Assign value to target variable
        } // Close active scope block
    } // Close active scope block
} // Close active scope block

Database::~Database() { // Execute clinical workflow step
    if (db_) { // Evaluate conditional expression to branch execution flow
        sqlite3_close(db_); // Close SQLite connection and flush all WAL journal buffers
        db_ = nullptr; // Assign value to target variable
    } // Close active scope block
} // Close active scope block

bool Database::execute(const std::string& sql) { // Execute clinical workflow step
    char* errMsg = nullptr; // Assign value to target variable
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg); // Execute raw SQL transaction statement or PRAGMA directive
    if (rc != SQLITE_OK) { // Evaluate conditional expression to branch execution flow
        std::cerr << "SQL error (" << rc << "): " << (errMsg ? errMsg : "unknown") << "\nQuery: " << sql << std::endl; // Print error alert message to standard error stream
        if (errMsg) sqlite3_free(errMsg); // Evaluate conditional expression to branch execution flow
        return false; // Return computed result to caller
    } // Close active scope block
    return true; // Return computed result to caller
} // Close active scope block

std::string Database::generateUniqueCode(const std::string& prefix) { // Execute clinical workflow step
    auto now = std::chrono::system_clock::now(); // Assign value to target variable
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count(); // Assign value to target variable
    static std::mt19937 gen(1337); // Execute clinical workflow step
    std::uniform_int_distribution<int> dis(1000, 9999); // Execute clinical workflow step
    std::ostringstream ss; // Execute clinical workflow step
    ss << prefix << "-" << (ms % 1000000) << "-" << dis(gen); // Execute clinical workflow step
    return ss.str(); // Return computed result to caller
} // Close active scope block

bool Database::initSchema() { // Execute clinical workflow step
    std::lock_guard<std::mutex> lock(dbMutex_); // Acquire RAII mutex lock ensuring thread-safe synchronized access
    if (!db_) return false; // Evaluate conditional expression to branch execution flow

    // Enable WAL, Foreign Keys, and performance pragmas
    execute("PRAGMA journal_mode = WAL;"); // Assign value to target variable
    execute("PRAGMA foreign_keys = ON;"); // Assign value to target variable
    execute("PRAGMA busy_timeout = 5000;"); // Assign value to target variable
    execute("PRAGMA synchronous = NORMAL;"); // Assign value to target variable
    execute("PRAGMA cache_size = -64000;"); // Assign value to target variable
    execute("PRAGMA temp_store = MEMORY;"); // Assign value to target variable

    const char* schema = R"SQL(
        CREATE TABLE IF NOT EXISTS doctors ( -- Create relational table entity
            id INTEGER PRIMARY KEY AUTOINCREMENT, -- Column field definition and constraints
            name TEXT NOT NULL, -- Column field definition and constraints
            specialization TEXT NOT NULL, -- Column field definition and constraints
            email TEXT UNIQUE, -- Column field definition and constraints
            phone TEXT, -- Column field definition and constraints
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP -- SQL schema directive
        ); -- End of schema table declaration

        CREATE TABLE IF NOT EXISTS patients ( -- Create relational table entity
            id INTEGER PRIMARY KEY AUTOINCREMENT, -- Column field definition and constraints
            first_name TEXT NOT NULL, -- Column field definition and constraints
            last_name TEXT NOT NULL, -- Column field definition and constraints
            date_of_birth DATE NOT NULL, -- Column field definition and constraints
            phone TEXT NOT NULL, -- Column field definition and constraints
            email TEXT, -- Column field definition and constraints
            address TEXT, -- Column field definition and constraints
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP -- SQL schema directive
        ); -- End of schema table declaration
        CREATE INDEX IF NOT EXISTS idx_patients_phone ON patients(phone); -- Create database query acceleration index

        CREATE TABLE IF NOT EXISTS appointments ( -- Create relational table entity
            id INTEGER PRIMARY KEY AUTOINCREMENT, -- Column field definition and constraints
            patient_id INTEGER NOT NULL, -- Column field definition and constraints
            doctor_id INTEGER NOT NULL, -- Column field definition and constraints
            appointment_time DATETIME NOT NULL, -- Column field definition and constraints
            reason TEXT, -- Column field definition and constraints
            status TEXT CHECK(status IN ('PENDING', 'CONFIRMED', 'COMPLETED', 'CANCELLED', 'NO_SHOW')) DEFAULT 'PENDING', -- Column field definition and constraints
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP, -- Column field definition and constraints
            FOREIGN KEY (patient_id) REFERENCES patients(id) ON DELETE CASCADE, -- Enforce referential integrity constraint
            FOREIGN KEY (doctor_id) REFERENCES doctors(id) ON DELETE RESTRICT -- Enforce referential integrity constraint
        ); -- End of schema table declaration
        CREATE INDEX IF NOT EXISTS idx_appointments_slot ON appointments(doctor_id, appointment_time); -- Create database query acceleration index
        CREATE INDEX IF NOT EXISTS idx_appointments_patient ON appointments(patient_id); -- Create database query acceleration index
        CREATE INDEX IF NOT EXISTS idx_appointments_status ON appointments(status); -- Create database query acceleration index

        CREATE TABLE IF NOT EXISTS inventory_items ( -- Create relational table entity
            id INTEGER PRIMARY KEY AUTOINCREMENT, -- Column field definition and constraints
            sku TEXT UNIQUE NOT NULL, -- Column field definition and constraints
            name TEXT NOT NULL, -- Column field definition and constraints
            category TEXT NOT NULL, -- Column field definition and constraints
            unit_price REAL NOT NULL CHECK(unit_price >= 0), -- Column field definition and constraints
            stock_quantity INTEGER NOT NULL DEFAULT 0 CHECK(stock_quantity >= 0), -- Column field definition and constraints
            reorder_threshold INTEGER NOT NULL DEFAULT 10 CHECK(reorder_threshold >= 0), -- Column field definition and constraints
            unit TEXT NOT NULL DEFAULT 'unit', -- Column field definition and constraints
            updated_at DATETIME DEFAULT CURRENT_TIMESTAMP -- SQL schema directive
        ); -- End of schema table declaration
        CREATE INDEX IF NOT EXISTS idx_inventory_sku ON inventory_items(sku); -- Create database query acceleration index

        CREATE TABLE IF NOT EXISTS prescriptions ( -- Create relational table entity
            id INTEGER PRIMARY KEY AUTOINCREMENT, -- Column field definition and constraints
            prescription_code TEXT UNIQUE NOT NULL, -- Column field definition and constraints
            patient_id INTEGER NOT NULL, -- Column field definition and constraints
            doctor_id INTEGER NOT NULL, -- Column field definition and constraints
            notes TEXT, -- Column field definition and constraints
            status TEXT CHECK(status IN ('PENDING', 'FILLED', 'DISPENSED', 'CANCELLED')) DEFAULT 'PENDING', -- Column field definition and constraints
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP, -- Column field definition and constraints
            FOREIGN KEY (patient_id) REFERENCES patients(id) ON DELETE CASCADE, -- Enforce referential integrity constraint
            FOREIGN KEY (doctor_id) REFERENCES doctors(id) ON DELETE RESTRICT -- Enforce referential integrity constraint
        ); -- End of schema table declaration
        CREATE INDEX IF NOT EXISTS idx_prescriptions_patient ON prescriptions(patient_id); -- Create database query acceleration index
        CREATE INDEX IF NOT EXISTS idx_prescriptions_status ON prescriptions(status); -- Create database query acceleration index
        CREATE INDEX IF NOT EXISTS idx_prescriptions_code ON prescriptions(prescription_code); -- Create database query acceleration index

        CREATE TABLE IF NOT EXISTS prescription_items ( -- Create relational table entity
            id INTEGER PRIMARY KEY AUTOINCREMENT, -- Column field definition and constraints
            prescription_id INTEGER NOT NULL, -- Column field definition and constraints
            inventory_item_id INTEGER NOT NULL, -- Column field definition and constraints
            quantity INTEGER NOT NULL CHECK(quantity > 0), -- Column field definition and constraints
            dosage_instructions TEXT NOT NULL, -- Column field definition and constraints
            FOREIGN KEY (prescription_id) REFERENCES prescriptions(id) ON DELETE CASCADE, -- Enforce referential integrity constraint
            FOREIGN KEY (inventory_item_id) REFERENCES inventory_items(id) ON DELETE RESTRICT -- Enforce referential integrity constraint
        ); -- End of schema table declaration
        CREATE INDEX IF NOT EXISTS idx_prescription_items_rx ON prescription_items(prescription_id); -- Create database query acceleration index

        CREATE TABLE IF NOT EXISTS sales_transactions ( -- Create relational table entity
            id INTEGER PRIMARY KEY AUTOINCREMENT, -- Column field definition and constraints
            transaction_code TEXT UNIQUE NOT NULL, -- Column field definition and constraints
            prescription_id INTEGER, -- Column field definition and constraints
            total_amount REAL NOT NULL CHECK(total_amount >= 0), -- Column field definition and constraints
            payment_method TEXT CHECK(payment_method IN ('CASH', 'CARD', 'E_WALLET', 'WEBHOOK_MOCK')) NOT NULL, -- Column field definition and constraints
            payment_status TEXT CHECK(payment_status IN ('PENDING', 'PAID', 'FAILED', 'REFUNDED')) DEFAULT 'PENDING', -- Column field definition and constraints
            payment_reference TEXT, -- Column field definition and constraints
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP, -- Column field definition and constraints
            FOREIGN KEY (prescription_id) REFERENCES prescriptions(id) ON DELETE SET NULL -- Enforce referential integrity constraint
        ); -- End of schema table declaration
        CREATE INDEX IF NOT EXISTS idx_sales_code ON sales_transactions(transaction_code); -- Create database query acceleration index
        CREATE INDEX IF NOT EXISTS idx_sales_created ON sales_transactions(created_at); -- Create database query acceleration index

        CREATE TABLE IF NOT EXISTS sales_transaction_items ( -- Create relational table entity
            id INTEGER PRIMARY KEY AUTOINCREMENT, -- Column field definition and constraints
            transaction_id INTEGER NOT NULL, -- Column field definition and constraints
            inventory_item_id INTEGER NOT NULL, -- Column field definition and constraints
            quantity INTEGER NOT NULL CHECK(quantity > 0), -- Column field definition and constraints
            unit_price REAL NOT NULL CHECK(unit_price >= 0), -- Column field definition and constraints
            subtotal REAL NOT NULL CHECK(subtotal >= 0), -- Column field definition and constraints
            FOREIGN KEY (transaction_id) REFERENCES sales_transactions(id) ON DELETE CASCADE, -- Enforce referential integrity constraint
            FOREIGN KEY (inventory_item_id) REFERENCES inventory_items(id) ON DELETE RESTRICT -- Enforce referential integrity constraint
        ); -- End of schema table declaration
    )SQL";

    return execute(schema); // Return computed result to caller
} // Close active scope block

void Database::seedInitialDataIfEmpty() { // Execute clinical workflow step
    std::lock_guard<std::mutex> lock(dbMutex_); // Acquire RAII mutex lock ensuring thread-safe synchronized access
    if (!db_) return; // Evaluate conditional expression to branch execution flow

    sqlite3_stmt* stmt = nullptr; // Assign value to target variable
    int count = 0; // Assign value to target variable
    if (sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM doctors;", -1, &stmt, nullptr) == SQLITE_OK) { // Compile SQL query into a reusable prepared statement byte code
        if (sqlite3_step(stmt) == SQLITE_ROW) { // Step statement execution to evaluate row or complete write operation
            count = sqlite3_column_int(stmt, 0); // Extract integer value from active database result column
        } // Close active scope block
        sqlite3_finalize(stmt); // Deallocate statement handle and release virtual machine resources
    } // Close active scope block

    if (count == 0) { // Evaluate conditional expression to branch execution flow
        // Seed Doctors
        execute(R"SQL(
            INSERT INTO doctors (name, specialization, email, phone) VALUES -- Column field definition and constraints
            ('Dr. Elizabeth Becker', 'General Medicine', 'elizabeth.becker@clinic.de', '+49-69-123450'), -- Column field definition and constraints
            ('Dr. Klaus Schneider', 'Pediatrics & Internal Medicine', 'klaus.schneider@clinic.de', '+49-69-123451'), -- Column field definition and constraints
            ('Dr. Anna Weber', 'Dermatology & Allergology', 'anna.weber@clinic.de', '+49-69-123452'); -- End of schema table declaration
        )SQL");

        // Seed Inventory
        execute(R"SQL(
            INSERT INTO inventory_items (sku, name, category, unit_price, stock_quantity, reorder_threshold, unit) VALUES -- Column field definition and constraints
            ('MED-AMOX-500', 'Amoxicillin 500mg (20 Caps)', 'Antibiotics', 14.50, 45, 15, 'box'), -- Column field definition and constraints
            ('MED-PARA-500', 'Paracetamol 500mg (30 Tabs)', 'Analgesic', 4.20, 120, 30, 'box'), -- Column field definition and constraints
            ('MED-IBUP-400', 'Ibuprofen 400mg (20 Tabs)', 'NSAID', 5.80, 80, 25, 'box'), -- Column field definition and constraints
            ('MED-OMEP-20',  'Omeprazole 20mg (14 Caps)', 'Gastrointestinal', 11.30, 8, 10, 'box'), -- Column field definition and constraints
            ('MED-CETI-10',  'Cetirizine 10mg (10 Tabs)', 'Antihistamine', 6.50, 50, 15, 'box'), -- Column field definition and constraints
            ('SUP-SYR-05ML', 'Sterile Syringes 5ml (100 pcs)', 'Supplies', 18.00, 12, 10, 'box'), -- Column field definition and constraints
            ('SUP-GAUZ-STR', 'Sterile Gauze Bandage 10cm', 'Supplies', 2.10, 5, 20, 'roll'), -- Column field definition and constraints
            ('SUP-ALCO-70P', 'Medical Isopropyl Alcohol 70% 500ml', 'Antiseptics', 7.50, 22, 10, 'bottle'); -- End of schema table declaration
        )SQL");
    } // Close active scope block
} // Close active scope block

std::vector<Doctor> Database::getDoctors() { // Execute clinical workflow step
    std::lock_guard<std::mutex> lock(dbMutex_); // Acquire RAII mutex lock ensuring thread-safe synchronized access
    std::vector<Doctor> doctors; // Execute clinical workflow step
    sqlite3_stmt* stmt = nullptr; // Assign value to target variable
    const char* sql = "SELECT id, name, specialization, email, phone FROM doctors ORDER BY name ASC;"; // Assign value to target variable
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) { // Compile SQL query into a reusable prepared statement byte code
        while (sqlite3_step(stmt) == SQLITE_ROW) { // Step statement execution to evaluate row or complete write operation
            Doctor doc; // Execute clinical workflow step
            doc.id = sqlite3_column_int64(stmt, 0); // Extract integer value from active database result column
            doc.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)); // Extract string text value from active database result column
            doc.specialization = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)); // Extract string text value from active database result column
            const char* email = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)); // Extract string text value from active database result column
            if (email) doc.email = email; // Evaluate conditional expression to branch execution flow
            const char* phone = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)); // Extract string text value from active database result column
            if (phone) doc.phone = phone; // Evaluate conditional expression to branch execution flow
            doctors.push_back(doc); // Execute clinical workflow step
        } // Close active scope block
        sqlite3_finalize(stmt); // Deallocate statement handle and release virtual machine resources
    } // Close active scope block
    return doctors; // Return computed result to caller
} // Close active scope block

std::optional<Doctor> Database::getDoctorById(int64_t id) { // Execute clinical workflow step
    std::lock_guard<std::mutex> lock(dbMutex_); // Acquire RAII mutex lock ensuring thread-safe synchronized access
    sqlite3_stmt* stmt = nullptr; // Assign value to target variable
    const char* sql = "SELECT id, name, specialization, email, phone FROM doctors WHERE id = ?;"; // Assign value to target variable
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) { // Compile SQL query into a reusable prepared statement byte code
        sqlite3_bind_int64(stmt, 1, id); // Bind integer ID/quantity parameter to prepared SQL query
        if (sqlite3_step(stmt) == SQLITE_ROW) { // Step statement execution to evaluate row or complete write operation
            Doctor doc; // Execute clinical workflow step
            doc.id = sqlite3_column_int64(stmt, 0); // Extract integer value from active database result column
            doc.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)); // Extract string text value from active database result column
            doc.specialization = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)); // Extract string text value from active database result column
            const char* email = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)); // Extract string text value from active database result column
            if (email) doc.email = email; // Evaluate conditional expression to branch execution flow
            const char* phone = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)); // Extract string text value from active database result column
            if (phone) doc.phone = phone; // Evaluate conditional expression to branch execution flow
            sqlite3_finalize(stmt); // Deallocate statement handle and release virtual machine resources
            return doc; // Return computed result to caller
        } // Close active scope block
        sqlite3_finalize(stmt); // Deallocate statement handle and release virtual machine resources
    } // Close active scope block
    return std::nullopt; // Return computed result to caller
} // Close active scope block

int64_t Database::createDoctor(const Doctor& doc) { // Execute clinical workflow step
    std::lock_guard<std::mutex> lock(dbMutex_); // Acquire RAII mutex lock ensuring thread-safe synchronized access
    sqlite3_stmt* stmt = nullptr; // Assign value to target variable
    const char* sql = "INSERT INTO doctors (name, specialization, email, phone) VALUES (?, ?, ?, ?);"; // Assign value to target variable
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) { // Compile SQL query into a reusable prepared statement byte code
        sqlite3_bind_text(stmt, 1, doc.name.c_str(), -1, SQLITE_TRANSIENT); // Safely bind text parameter to prepared statement preventing SQL injection
        sqlite3_bind_text(stmt, 2, doc.specialization.c_str(), -1, SQLITE_TRANSIENT); // Safely bind text parameter to prepared statement preventing SQL injection
        sqlite3_bind_text(stmt, 3, doc.email.c_str(), -1, SQLITE_TRANSIENT); // Safely bind text parameter to prepared statement preventing SQL injection
        sqlite3_bind_text(stmt, 4, doc.phone.c_str(), -1, SQLITE_TRANSIENT); // Safely bind text parameter to prepared statement preventing SQL injection
        if (sqlite3_step(stmt) == SQLITE_DONE) { // Step statement execution to evaluate row or complete write operation
            int64_t id = sqlite3_last_insert_rowid(db_); // Assign value to target variable
            sqlite3_finalize(stmt); // Deallocate statement handle and release virtual machine resources
            return id; // Return computed result to caller
        } // Close active scope block
        sqlite3_finalize(stmt); // Deallocate statement handle and release virtual machine resources
    } // Close active scope block
    return -1; // Return computed result to caller
} // Close active scope block

int64_t Database::findOrCreatePatient(const Patient& patient) { // Execute clinical workflow step
    if (patient.phone.empty() || patient.first_name.empty()) { // Evaluate conditional expression to branch execution flow
        return -1; // Return computed result to caller
    } // Close active scope block

    sqlite3_stmt* stmt = nullptr; // Assign value to target variable
    const char* findSql = "SELECT id FROM patients WHERE phone = ? LIMIT 1;"; // Assign value to target variable
    if (sqlite3_prepare_v2(db_, findSql, -1, &stmt, nullptr) == SQLITE_OK) { // Compile SQL query into a reusable prepared statement byte code
        sqlite3_bind_text(stmt, 1, patient.phone.c_str(), -1, SQLITE_TRANSIENT); // Safely bind text parameter to prepared statement preventing SQL injection
        if (sqlite3_step(stmt) == SQLITE_ROW) { // Step statement execution to evaluate row or complete write operation
            int64_t id = sqlite3_column_int64(stmt, 0); // Extract integer value from active database result column
            sqlite3_finalize(stmt); // Deallocate statement handle and release virtual machine resources
            return id; // Return computed result to caller
        } // Close active scope block
        sqlite3_finalize(stmt); // Deallocate statement handle and release virtual machine resources
    } // Close active scope block

    std::string dob = patient.date_of_birth.empty() ? "1990-01-01" : patient.date_of_birth; // Assign value to target variable

    const char* insertSql = "INSERT INTO patients (first_name, last_name, date_of_birth, phone, email, address) VALUES (?, ?, ?, ?, ?, ?);"; // Assign value to target variable
    if (sqlite3_prepare_v2(db_, insertSql, -1, &stmt, nullptr) == SQLITE_OK) { // Compile SQL query into a reusable prepared statement byte code
        sqlite3_bind_text(stmt, 1, patient.first_name.c_str(), -1, SQLITE_TRANSIENT); // Safely bind text parameter to prepared statement preventing SQL injection
        sqlite3_bind_text(stmt, 2, patient.last_name.c_str(), -1, SQLITE_TRANSIENT); // Safely bind text parameter to prepared statement preventing SQL injection
        sqlite3_bind_text(stmt, 3, dob.c_str(), -1, SQLITE_TRANSIENT); // Safely bind text parameter to prepared statement preventing SQL injection
        sqlite3_bind_text(stmt, 4, patient.phone.c_str(), -1, SQLITE_TRANSIENT); // Safely bind text parameter to prepared statement preventing SQL injection
        sqlite3_bind_text(stmt, 5, patient.email.c_str(), -1, SQLITE_TRANSIENT); // Safely bind text parameter to prepared statement preventing SQL injection
        sqlite3_bind_text(stmt, 6, patient.address.c_str(), -1, SQLITE_TRANSIENT); // Safely bind text parameter to prepared statement preventing SQL injection
        if (sqlite3_step(stmt) == SQLITE_DONE) { // Step statement execution to evaluate row or complete write operation
            int64_t id = sqlite3_last_insert_rowid(db_); // Assign value to target variable
            sqlite3_finalize(stmt); // Deallocate statement handle and release virtual machine resources
            return id; // Return computed result to caller
        } // Close active scope block
        sqlite3_finalize(stmt); // Deallocate statement handle and release virtual machine resources
    } // Close active scope block
    return -1; // Return computed result to caller
} // Close active scope block

std::optional<Patient> Database::getPatientById(int64_t id) { // Execute clinical workflow step
    std::lock_guard<std::mutex> lock(dbMutex_); // Acquire RAII mutex lock ensuring thread-safe synchronized access
    sqlite3_stmt* stmt = nullptr; // Assign value to target variable
    const char* sql = "SELECT id, first_name, last_name, date_of_birth, phone, email, address FROM patients WHERE id = ?;"; // Assign value to target variable
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) { // Compile SQL query into a reusable prepared statement byte code
        sqlite3_bind_int64(stmt, 1, id); // Bind integer ID/quantity parameter to prepared SQL query
        if (sqlite3_step(stmt) == SQLITE_ROW) { // Step statement execution to evaluate row or complete write operation
            Patient p; // Execute clinical workflow step
            p.id = sqlite3_column_int64(stmt, 0); // Extract integer value from active database result column
            p.first_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)); // Extract string text value from active database result column
            p.last_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)); // Extract string text value from active database result column
            p.date_of_birth = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)); // Extract string text value from active database result column
            p.phone = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)); // Extract string text value from active database result column
            const char* email = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5)); // Extract string text value from active database result column
            if (email) p.email = email; // Evaluate conditional expression to branch execution flow
            const char* address = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6)); // Extract string text value from active database result column
            if (address) p.address = address; // Evaluate conditional expression to branch execution flow
            sqlite3_finalize(stmt); // Deallocate statement handle and release virtual machine resources
            return p; // Return computed result to caller
        } // Close active scope block
        sqlite3_finalize(stmt); // Deallocate statement handle and release virtual machine resources
    } // Close active scope block
    return std::nullopt; // Return computed result to caller
} // Close active scope block

std::vector<std::string> Database::getBookedSlotsForDoctor(int64_t doctorId, const std::string& date) { // Execute clinical workflow step
    std::lock_guard<std::mutex> lock(dbMutex_); // Acquire RAII mutex lock ensuring thread-safe synchronized access
    std::vector<std::string> bookedSlots; // Execute clinical workflow step
    sqlite3_stmt* stmt = nullptr; // Assign value to target variable
    std::string sql = "SELECT appointment_time FROM appointments WHERE doctor_id = ? AND strftime('%Y-%m-%d', appointment_time) = ? AND status != 'CANCELLED';"; // Assign value to target variable
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) { // Compile SQL query into a reusable prepared statement byte code
        sqlite3_bind_int64(stmt, 1, doctorId); // Bind integer ID/quantity parameter to prepared SQL query
        sqlite3_bind_text(stmt, 2, date.c_str(), -1, SQLITE_TRANSIENT); // Safely bind text parameter to prepared statement preventing SQL injection
        while (sqlite3_step(stmt) == SQLITE_ROW) { // Step statement execution to evaluate row or complete write operation
            bookedSlots.push_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0))); // Extract string text value from active database result column
        } // Close active scope block
        sqlite3_finalize(stmt); // Deallocate statement handle and release virtual machine resources
    } // Close active scope block
    return bookedSlots; // Return computed result to caller
} // Close active scope block

std::vector<Appointment> Database::getAppointments(const std::optional<std::string>& date, // Execute clinical workflow step
                                                  const std::optional<std::string>& status) { // Execute clinical workflow step
    std::lock_guard<std::mutex> lock(dbMutex_); // Acquire RAII mutex lock ensuring thread-safe synchronized access
    std::vector<Appointment> list; // Execute clinical workflow step
    std::string sql = R"SQL(
        SELECT a.id, a.patient_id, a.doctor_id, a.appointment_time, a.reason, a.status, a.created_at, -- Column field definition and constraints
               (p.first_name || ' ' || p.last_name) AS patient_name, p.phone AS patient_phone, d.name AS doctor_name -- Column field definition and constraints
        FROM appointments a -- SQL schema directive
        JOIN patients p ON a.patient_id = p.id -- SQL schema directive
        JOIN doctors d ON a.doctor_id = d.id -- SQL schema directive
        WHERE 1=1 -- SQL schema directive
    )SQL";

    if (date.has_value()) { // Evaluate conditional expression to branch execution flow
        sql += " AND strftime('%Y-%m-%d', a.appointment_time) = ?"; // Assign value to target variable
    } // Close active scope block
    if (status.has_value()) { // Evaluate conditional expression to branch execution flow
        sql += " AND a.status = ?"; // Assign value to target variable
    } // Close active scope block
    sql += " ORDER BY a.appointment_time ASC;"; // Assign value to target variable

    sqlite3_stmt* stmt = nullptr; // Assign value to target variable
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) { // Compile SQL query into a reusable prepared statement byte code
        int paramIdx = 1; // Assign value to target variable
        if (date.has_value()) { // Evaluate conditional expression to branch execution flow
            sqlite3_bind_text(stmt, paramIdx++, date.value().c_str(), -1, SQLITE_TRANSIENT); // Safely bind text parameter to prepared statement preventing SQL injection
        } // Close active scope block
        if (status.has_value()) { // Evaluate conditional expression to branch execution flow
            sqlite3_bind_text(stmt, paramIdx++, status.value().c_str(), -1, SQLITE_TRANSIENT); // Safely bind text parameter to prepared statement preventing SQL injection
        } // Close active scope block
        while (sqlite3_step(stmt) == SQLITE_ROW) { // Step statement execution to evaluate row or complete write operation
            Appointment a; // Execute clinical workflow step
            a.id = sqlite3_column_int64(stmt, 0); // Extract integer value from active database result column
            a.patient_id = sqlite3_column_int64(stmt, 1); // Extract integer value from active database result column
            a.doctor_id = sqlite3_column_int64(stmt, 2); // Extract integer value from active database result column
            a.appointment_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)); // Extract string text value from active database result column
            const char* reason = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)); // Extract string text value from active database result column
            if (reason) a.reason = reason; // Evaluate conditional expression to branch execution flow
            a.status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5)); // Extract string text value from active database result column
            a.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6)); // Extract string text value from active database result column
            a.patient_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7)); // Extract string text value from active database result column
            const char* phone = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8)); // Extract string text value from active database result column
            if (phone) a.patient_phone = phone; // Evaluate conditional expression to branch execution flow
            a.doctor_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9)); // Extract string text value from active database result column
            list.push_back(a); // Execute clinical workflow step
        } // Close active scope block
        sqlite3_finalize(stmt); // Deallocate statement handle and release virtual machine resources
    } // Close active scope block
    return list; // Return computed result to caller
} // Close active scope block

std::optional<Appointment> Database::getAppointmentById(int64_t id) { // Execute clinical workflow step
    std::lock_guard<std::mutex> lock(dbMutex_); // Acquire RAII mutex lock ensuring thread-safe synchronized access
    sqlite3_stmt* stmt = nullptr; // Assign value to target variable
    const char* sql = R"SQL(
        SELECT a.id, a.patient_id, a.doctor_id, a.appointment_time, a.reason, a.status, a.created_at, -- Column field definition and constraints
               (p.first_name || ' ' || p.last_name) AS patient_name, p.phone AS patient_phone, d.name AS doctor_name -- Column field definition and constraints
        FROM appointments a -- SQL schema directive
        JOIN patients p ON a.patient_id = p.id -- SQL schema directive
        JOIN doctors d ON a.doctor_id = d.id -- SQL schema directive
        WHERE a.id = ?; -- SQL schema directive
    )SQL";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) { // Compile SQL query into a reusable prepared statement byte code
        sqlite3_bind_int64(stmt, 1, id); // Bind integer ID/quantity parameter to prepared SQL query
        if (sqlite3_step(stmt) == SQLITE_ROW) { // Step statement execution to evaluate row or complete write operation
            Appointment a; // Execute clinical workflow step
            a.id = sqlite3_column_int64(stmt, 0); // Extract integer value from active database result column
            a.patient_id = sqlite3_column_int64(stmt, 1); // Extract integer value from active database result column
            a.doctor_id = sqlite3_column_int64(stmt, 2); // Extract integer value from active database result column
            a.appointment_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)); // Extract string text value from active database result column
            const char* reason = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)); // Extract string text value from active database result column
            if (reason) a.reason = reason; // Evaluate conditional expression to branch execution flow
            a.status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5)); // Extract string text value from active database result column
            a.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6)); // Extract string text value from active database result column
            a.patient_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7)); // Extract string text value from active database result column
            const char* phone = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8)); // Extract string text value from active database result column
            if (phone) a.patient_phone = phone; // Evaluate conditional expression to branch execution flow
            a.doctor_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9)); // Extract string text value from active database result column
            sqlite3_finalize(stmt); // Deallocate statement handle and release virtual machine resources
            return a; // Return computed result to caller
        } // Close active scope block
        sqlite3_finalize(stmt); // Deallocate statement handle and release virtual machine resources
    } // Close active scope block
    return std::nullopt; // Return computed result to caller
} // Close active scope block

bool Database::bookAppointment(Appointment& appt, const Patient& patientInfo) { // Execute clinical workflow step
    if (appt.doctor_id <= 0 || appt.appointment_time.empty() || patientInfo.phone.empty()) { // Evaluate conditional expression to branch execution flow
        return false; // Return computed result to caller
    } // Close active scope block
    std::lock_guard<std::mutex> lock(dbMutex_); // Acquire RAII mutex lock ensuring thread-safe synchronized access
    if (!db_) return false; // Evaluate conditional expression to branch execution flow

    execute("BEGIN IMMEDIATE TRANSACTION;"); // Execute clinical workflow step

    // 1. Resolve patient ID
    int64_t pId = findOrCreatePatient(patientInfo); // Assign value to target variable
    if (pId <= 0) { // Evaluate conditional expression to branch execution flow
        execute("ROLLBACK;"); // Execute clinical workflow step
        return false; // Return computed result to caller
    } // Close active scope block
    appt.patient_id = pId; // Assign value to target variable

    // 2. Conflict prevention: check if slot already booked for doctor
    sqlite3_stmt* stmt = nullptr; // Assign value to target variable
    const char* checkSql = "SELECT id FROM appointments WHERE doctor_id = ? AND appointment_time = ? AND status != 'CANCELLED';"; // Assign value to target variable
    if (sqlite3_prepare_v2(db_, checkSql, -1, &stmt, nullptr) == SQLITE_OK) { // Compile SQL query into a reusable prepared statement byte code
        sqlite3_bind_int64(stmt, 1, appt.doctor_id); // Bind integer ID/quantity parameter to prepared SQL query
        sqlite3_bind_text(stmt, 2, appt.appointment_time.c_str(), -1, SQLITE_TRANSIENT); // Safely bind text parameter to prepared statement preventing SQL injection
        if (sqlite3_step(stmt) == SQLITE_ROW) { // Step statement execution to evaluate row or complete write operation
            sqlite3_finalize(stmt); // Deallocate statement handle and release virtual machine resources
            execute("ROLLBACK;"); // Execute clinical workflow step
            return false; // Slot already booked
        } // Close active scope block
        sqlite3_finalize(stmt); // Deallocate statement handle and release virtual machine resources
    } // Close active scope block

    // 3. Insert appointment
    const char* insertSql = "INSERT INTO appointments (patient_id, doctor_id, appointment_time, reason, status) VALUES (?, ?, ?, ?, 'CONFIRMED');"; // Assign value to target variable
    if (sqlite3_prepare_v2(db_, insertSql, -1, &stmt, nullptr) == SQLITE_OK) { // Compile SQL query into a reusable prepared statement byte code
        sqlite3_bind_int64(stmt, 1, appt.patient_id); // Bind integer ID/quantity parameter to prepared SQL query
        sqlite3_bind_int64(stmt, 2, appt.doctor_id); // Bind integer ID/quantity parameter to prepared SQL query
        sqlite3_bind_text(stmt, 3, appt.appointment_time.c_str(), -1, SQLITE_TRANSIENT); // Safely bind text parameter to prepared statement preventing SQL injection
        sqlite3_bind_text(stmt, 4, appt.reason.c_str(), -1, SQLITE_TRANSIENT); // Safely bind text parameter to prepared statement preventing SQL injection
        if (sqlite3_step(stmt) == SQLITE_DONE) { // Step statement execution to evaluate row or complete write operation
            appt.id = sqlite3_last_insert_rowid(db_); // Assign value to target variable
            appt.status = "CONFIRMED"; // Assign value to target variable
            sqlite3_finalize(stmt); // Deallocate statement handle and release virtual machine resources
            execute("COMMIT;"); // Execute clinical workflow step
            return true; // Return computed result to caller
        } // Close active scope block
        sqlite3_finalize(stmt); // Deallocate statement handle and release virtual machine resources
    } // Close active scope block

    execute("ROLLBACK;"); // Execute clinical workflow step
    return false; // Return computed result to caller
} // Close active scope block

std::vector<InventoryItem> Database::getInventory(bool lowStockOnly, const std::string& search) { // Execute clinical workflow step
    std::lock_guard<std::mutex> lock(dbMutex_); // Acquire RAII mutex lock ensuring thread-safe synchronized access
    std::vector<InventoryItem> list; // Execute clinical workflow step
    std::string sql = "SELECT id, sku, name, category, unit_price, stock_quantity, reorder_threshold, unit, updated_at FROM inventory_items WHERE 1=1"; // Assign value to target variable
    if (lowStockOnly) { // Evaluate conditional expression to branch execution flow
        sql += " AND stock_quantity <= reorder_threshold"; // Assign value to target variable
    } // Close active scope block
    if (!search.empty()) { // Evaluate conditional expression to branch execution flow
        sql += " AND (name LIKE ? OR sku LIKE ? OR category LIKE ?)"; // Assign value to target variable
    } // Close active scope block
    sql += " ORDER BY name ASC;"; // Assign value to target variable

    sqlite3_stmt* stmt = nullptr; // Assign value to target variable
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) { // Compile SQL query into a reusable prepared statement byte code
        if (!search.empty()) { // Evaluate conditional expression to branch execution flow
            std::string pattern = "%" + search + "%"; // Assign value to target variable
            sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT); // Safely bind text parameter to prepared statement preventing SQL injection
            sqlite3_bind_text(stmt, 2, pattern.c_str(), -1, SQLITE_TRANSIENT); // Safely bind text parameter to prepared statement preventing SQL injection
            sqlite3_bind_text(stmt, 3, pattern.c_str(), -1, SQLITE_TRANSIENT); // Safely bind text parameter to prepared statement preventing SQL injection
        } // Close active scope block
        while (sqlite3_step(stmt) == SQLITE_ROW) { // Step statement execution to evaluate row or complete write operation
            InventoryItem item; // Execute clinical workflow step
            item.id = sqlite3_column_int64(stmt, 0); // Extract integer value from active database result column
            item.sku = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)); // Extract string text value from active database result column
            item.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)); // Extract string text value from active database result column
            item.category = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)); // Extract string text value from active database result column
            item.unit_price = sqlite3_column_double(stmt, 4); // Extract floating point numerical value from active result column
            item.stock_quantity = sqlite3_column_int(stmt, 5); // Extract integer value from active database result column
            item.reorder_threshold = sqlite3_column_int(stmt, 6); // Extract integer value from active database result column
            item.unit = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7)); // Extract string text value from active database result column
            item.updated_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8)); // Extract string text value from active database result column
            list.push_back(item); // Execute clinical workflow step
        } // Close active scope block
        sqlite3_finalize(stmt); // Deallocate statement handle and release virtual machine resources
    } // Close active scope block
    return list; // Return computed result to caller
} // Close active scope block

std::optional<InventoryItem> Database::getInventoryItem(int64_t id) { // Execute clinical workflow step
    std::lock_guard<std::mutex> lock(dbMutex_); // Acquire RAII mutex lock ensuring thread-safe synchronized access
    sqlite3_stmt* stmt = nullptr; // Assign value to target variable
    const char* sql = "SELECT id, sku, name, category, unit_price, stock_quantity, reorder_threshold, unit, updated_at FROM inventory_items WHERE id = ?;"; // Assign value to target variable
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) { // Compile SQL query into a reusable prepared statement byte code
        sqlite3_bind_int64(stmt, 1, id); // Bind integer ID/quantity parameter to prepared SQL query
        if (sqlite3_step(stmt) == SQLITE_ROW) { // Step statement execution to evaluate row or complete write operation
            InventoryItem item; // Execute clinical workflow step
            item.id = sqlite3_column_int64(stmt, 0); // Extract integer value from active database result column
            item.sku = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)); // Extract string text value from active database result column
            item.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)); // Extract string text value from active database result column
            item.category = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)); // Extract string text value from active database result column
            item.unit_price = sqlite3_column_double(stmt, 4); // Extract floating point numerical value from active result column
            item.stock_quantity = sqlite3_column_int(stmt, 5); // Extract integer value from active database result column
            item.reorder_threshold = sqlite3_column_int(stmt, 6); // Extract integer value from active database result column
            item.unit = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7)); // Extract string text value from active database result column
            item.updated_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8)); // Extract string text value from active database result column
            sqlite3_finalize(stmt); // Deallocate statement handle and release virtual machine resources
            return item; // Return computed result to caller
        } // Close active scope block
        sqlite3_finalize(stmt); // Deallocate statement handle and release virtual machine resources
    } // Close active scope block
    return std::nullopt; // Return computed result to caller
} // Close active scope block

std::optional<InventoryItem> Database::getInventoryItemBySku(const std::string& sku) { // Execute clinical workflow step
    std::lock_guard<std::mutex> lock(dbMutex_); // Acquire RAII mutex lock ensuring thread-safe synchronized access
    sqlite3_stmt* stmt = nullptr; // Assign value to target variable
    const char* sql = "SELECT id, sku, name, category, unit_price, stock_quantity, reorder_threshold, unit, updated_at FROM inventory_items WHERE sku = ?;"; // Assign value to target variable
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) { // Compile SQL query into a reusable prepared statement byte code
        sqlite3_bind_text(stmt, 1, sku.c_str(), -1, SQLITE_TRANSIENT); // Safely bind text parameter to prepared statement preventing SQL injection
        if (sqlite3_step(stmt) == SQLITE_ROW) { // Step statement execution to evaluate row or complete write operation
            InventoryItem item; // Execute clinical workflow step
            item.id = sqlite3_column_int64(stmt, 0); // Extract integer value from active database result column
            item.sku = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)); // Extract string text value from active database result column
            item.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)); // Extract string text value from active database result column
            item.category = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)); // Extract string text value from active database result column
            item.unit_price = sqlite3_column_double(stmt, 4); // Extract floating point numerical value from active result column
            item.stock_quantity = sqlite3_column_int(stmt, 5); // Extract integer value from active database result column
            item.reorder_threshold = sqlite3_column_int(stmt, 6); // Extract integer value from active database result column
            item.unit = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7)); // Extract string text value from active database result column
            item.updated_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8)); // Extract string text value from active database result column
            sqlite3_finalize(stmt); // Deallocate statement handle and release virtual machine resources
            return item; // Return computed result to caller
        } // Close active scope block
        sqlite3_finalize(stmt); // Deallocate statement handle and release virtual machine resources
    } // Close active scope block
    return std::nullopt; // Return computed result to caller
} // Close active scope block

int64_t Database::createInventoryItem(const InventoryItem& item) { // Execute clinical workflow step
    if (item.sku.empty() || item.name.empty() || item.unit_price < 0.0 || item.stock_quantity < 0 || item.reorder_threshold < 0) { // Evaluate conditional expression to branch execution flow
        return -1; // Return computed result to caller
    } // Close active scope block
    std::lock_guard<std::mutex> lock(dbMutex_); // Acquire RAII mutex lock ensuring thread-safe synchronized access
    sqlite3_stmt* stmt = nullptr; // Assign value to target variable
    const char* sql = "INSERT INTO inventory_items (sku, name, category, unit_price, stock_quantity, reorder_threshold, unit) VALUES (?, ?, ?, ?, ?, ?, ?);"; // Assign value to target variable
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) { // Compile SQL query into a reusable prepared statement byte code
        sqlite3_bind_text(stmt, 1, item.sku.c_str(), -1, SQLITE_TRANSIENT); // Safely bind text parameter to prepared statement preventing SQL injection
        sqlite3_bind_text(stmt, 2, item.name.c_str(), -1, SQLITE_TRANSIENT); // Safely bind text parameter to prepared statement preventing SQL injection
        sqlite3_bind_text(stmt, 3, item.category.c_str(), -1, SQLITE_TRANSIENT); // Safely bind text parameter to prepared statement preventing SQL injection
        sqlite3_bind_double(stmt, 4, item.unit_price); // Bind floating-point numerical parameter to prepared query
        sqlite3_bind_int(stmt, 5, item.stock_quantity); // Bind integer ID/quantity parameter to prepared SQL query
        sqlite3_bind_int(stmt, 6, item.reorder_threshold); // Bind integer ID/quantity parameter to prepared SQL query
        sqlite3_bind_text(stmt, 7, item.unit.c_str(), -1, SQLITE_TRANSIENT); // Safely bind text parameter to prepared statement preventing SQL injection
        if (sqlite3_step(stmt) == SQLITE_DONE) { // Step statement execution to evaluate row or complete write operation
            int64_t id = sqlite3_last_insert_rowid(db_); // Assign value to target variable
            sqlite3_finalize(stmt); // Deallocate statement handle and release virtual machine resources
            return id; // Return computed result to caller
        } // Close active scope block
        sqlite3_finalize(stmt); // Deallocate statement handle and release virtual machine resources
    } // Close active scope block
    return -1; // Return computed result to caller
} // Close active scope block

bool Database::restockItem(int64_t id, int delta, const std::string& reason) { // Execute clinical workflow step
    if (id <= 0 || delta == 0) return false; // Evaluate conditional expression to branch execution flow
    std::lock_guard<std::mutex> lock(dbMutex_); // Acquire RAII mutex lock ensuring thread-safe synchronized access
    execute("BEGIN IMMEDIATE TRANSACTION;"); // Execute clinical workflow step

    // Fetch current stock
    int currentStock = 0; // Assign value to target variable
    sqlite3_stmt* stmt = nullptr; // Assign value to target variable
    if (sqlite3_prepare_v2(db_, "SELECT stock_quantity FROM inventory_items WHERE id = ?;", -1, &stmt, nullptr) == SQLITE_OK) { // Compile SQL query into a reusable prepared statement byte code
        sqlite3_bind_int64(stmt, 1, id); // Bind integer ID/quantity parameter to prepared SQL query
        if (sqlite3_step(stmt) == SQLITE_ROW) { // Step statement execution to evaluate row or complete write operation
            currentStock = sqlite3_column_int(stmt, 0); // Extract integer value from active database result column
        } else { // Execute default fallback branch
            sqlite3_finalize(stmt); // Deallocate statement handle and release virtual machine resources
            execute("ROLLBACK;"); // Execute clinical workflow step
            return false; // Return computed result to caller
        } // Close active scope block
        sqlite3_finalize(stmt); // Deallocate statement handle and release virtual machine resources
    } // Close active scope block

    int newStock = currentStock + delta; // Assign value to target variable
    if (newStock < 0) { // Evaluate conditional expression to branch execution flow
        execute("ROLLBACK;"); // Execute clinical workflow step
        return false; // Return computed result to caller
    } // Close active scope block

    // Update stock
    const char* updateSql = "UPDATE inventory_items SET stock_quantity = ?, updated_at = CURRENT_TIMESTAMP WHERE id = ?;"; // Assign value to target variable
    if (sqlite3_prepare_v2(db_, updateSql, -1, &stmt, nullptr) == SQLITE_OK) { // Compile SQL query into a reusable prepared statement byte code
        sqlite3_bind_int(stmt, 1, newStock); // Bind integer ID/quantity parameter to prepared SQL query
        sqlite3_bind_int64(stmt, 2, id); // Bind integer ID/quantity parameter to prepared SQL query
        if (sqlite3_step(stmt) != SQLITE_DONE) { // Step statement execution to evaluate row or complete write operation
            sqlite3_finalize(stmt); // Deallocate statement handle and release virtual machine resources
            execute("ROLLBACK;"); // Execute clinical workflow step
            return false; // Return computed result to caller
        } // Close active scope block
        sqlite3_finalize(stmt); // Deallocate statement handle and release virtual machine resources
    } // Close active scope block

    execute("COMMIT;"); // Execute clinical workflow step
    return true; // Return computed result to caller
} // Close active scope block

std::vector<InventoryItem> Database::getLowStockAlerts() { // Execute clinical workflow step
    return getInventory(true); // Return computed result to caller
} // Close active scope block

std::string Database::createPrescription(const Prescription& rx) { // Execute clinical workflow step
    if (rx.patient_id <= 0 || rx.doctor_id <= 0 || rx.items.empty()) { // Evaluate conditional expression to branch execution flow
        return ""; // Return computed result to caller
    } // Close active scope block
    for (const auto& it : rx.items) { // Iterate through items in sequence or collection
        if (it.inventory_item_id <= 0 || it.quantity <= 0) { // Evaluate conditional expression to branch execution flow
            return ""; // Return computed result to caller
        } // Close active scope block
    } // Close active scope block

    std::lock_guard<std::mutex> lock(dbMutex_); // Acquire RAII mutex lock ensuring thread-safe synchronized access
    execute("BEGIN IMMEDIATE TRANSACTION;"); // Execute clinical workflow step

    std::string rxCode = generateUniqueCode("RX"); // Assign value to target variable
    sqlite3_stmt* stmt = nullptr; // Assign value to target variable
    const char* sql = "INSERT INTO prescriptions (prescription_code, patient_id, doctor_id, notes, status) VALUES (?, ?, ?, ?, 'PENDING');"; // Assign value to target variable
    int64_t rxId = -1; // Assign value to target variable
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) { // Compile SQL query into a reusable prepared statement byte code
        sqlite3_bind_text(stmt, 1, rxCode.c_str(), -1, SQLITE_TRANSIENT); // Safely bind text parameter to prepared statement preventing SQL injection
        sqlite3_bind_int64(stmt, 2, rx.patient_id); // Bind integer ID/quantity parameter to prepared SQL query
        sqlite3_bind_int64(stmt, 3, rx.doctor_id); // Bind integer ID/quantity parameter to prepared SQL query
        sqlite3_bind_text(stmt, 4, rx.notes.c_str(), -1, SQLITE_TRANSIENT); // Safely bind text parameter to prepared statement preventing SQL injection
        if (sqlite3_step(stmt) == SQLITE_DONE) { // Step statement execution to evaluate row or complete write operation
            rxId = sqlite3_last_insert_rowid(db_); // Assign value to target variable
        } // Close active scope block
        sqlite3_finalize(stmt); // Deallocate statement handle and release virtual machine resources
    } // Close active scope block

    if (rxId <= 0) { // Evaluate conditional expression to branch execution flow
        execute("ROLLBACK;"); // Execute clinical workflow step
        return ""; // Return computed result to caller
    } // Close active scope block

    // Insert prescription items
    const char* itemSql = "INSERT INTO prescription_items (prescription_id, inventory_item_id, quantity, dosage_instructions) VALUES (?, ?, ?, ?);"; // Assign value to target variable
    for (const auto& it : rx.items) { // Iterate through items in sequence or collection
        if (sqlite3_prepare_v2(db_, itemSql, -1, &stmt, nullptr) == SQLITE_OK) { // Compile SQL query into a reusable prepared statement byte code
            sqlite3_bind_int64(stmt, 1, rxId); // Bind integer ID/quantity parameter to prepared SQL query
            sqlite3_bind_int64(stmt, 2, it.inventory_item_id); // Bind integer ID/quantity parameter to prepared SQL query
            sqlite3_bind_int(stmt, 3, it.quantity); // Bind integer ID/quantity parameter to prepared SQL query
            sqlite3_bind_text(stmt, 4, it.dosage_instructions.c_str(), -1, SQLITE_TRANSIENT); // Safely bind text parameter to prepared statement preventing SQL injection
            if (sqlite3_step(stmt) != SQLITE_DONE) { // Step statement execution to evaluate row or complete write operation
                sqlite3_finalize(stmt); // Deallocate statement handle and release virtual machine resources
                execute("ROLLBACK;"); // Execute clinical workflow step
                return ""; // Return computed result to caller
            } // Close active scope block
            sqlite3_finalize(stmt); // Deallocate statement handle and release virtual machine resources
        } // Close active scope block
    } // Close active scope block

    execute("COMMIT;"); // Execute clinical workflow step
    return rxCode; // Return computed result to caller
} // Close active scope block

std::vector<Prescription> Database::getPrescriptions(const std::optional<std::string>& status, // Execute clinical workflow step
                                                    std::optional<int64_t> patientId) { // Execute clinical workflow step
    std::lock_guard<std::mutex> lock(dbMutex_); // Acquire RAII mutex lock ensuring thread-safe synchronized access
    std::vector<Prescription> list; // Execute clinical workflow step
    std::string sql = R"SQL(
        SELECT pr.id, pr.prescription_code, pr.patient_id, pr.doctor_id, pr.notes, pr.status, pr.created_at, -- Column field definition and constraints
               (p.first_name || ' ' || p.last_name) AS patient_name, d.name AS doctor_name -- Column field definition and constraints
        FROM prescriptions pr -- SQL schema directive
        JOIN patients p ON pr.patient_id = p.id -- SQL schema directive
        JOIN doctors d ON pr.doctor_id = d.id -- SQL schema directive
        WHERE 1=1 -- SQL schema directive
    )SQL";
    if (status.has_value()) { // Evaluate conditional expression to branch execution flow
        sql += " AND pr.status = ?"; // Assign value to target variable
    } // Close active scope block
    if (patientId.has_value()) { // Evaluate conditional expression to branch execution flow
        sql += " AND pr.patient_id = ?"; // Assign value to target variable
    } // Close active scope block
    sql += " ORDER BY pr.created_at DESC;"; // Assign value to target variable

    sqlite3_stmt* stmt = nullptr; // Assign value to target variable
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) { // Compile SQL query into a reusable prepared statement byte code
        int paramIdx = 1; // Assign value to target variable
        if (status.has_value()) { // Evaluate conditional expression to branch execution flow
            sqlite3_bind_text(stmt, paramIdx++, status.value().c_str(), -1, SQLITE_TRANSIENT); // Safely bind text parameter to prepared statement preventing SQL injection
        } // Close active scope block
        if (patientId.has_value()) { // Evaluate conditional expression to branch execution flow
            sqlite3_bind_int64(stmt, paramIdx++, patientId.value()); // Bind integer ID/quantity parameter to prepared SQL query
        } // Close active scope block
        while (sqlite3_step(stmt) == SQLITE_ROW) { // Step statement execution to evaluate row or complete write operation
            Prescription rx; // Execute clinical workflow step
            rx.id = sqlite3_column_int64(stmt, 0); // Extract integer value from active database result column
            rx.prescription_code = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)); // Extract string text value from active database result column
            rx.patient_id = sqlite3_column_int64(stmt, 2); // Extract integer value from active database result column
            rx.doctor_id = sqlite3_column_int64(stmt, 3); // Extract integer value from active database result column
            const char* notes = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)); // Extract string text value from active database result column
            if (notes) rx.notes = notes; // Evaluate conditional expression to branch execution flow
            rx.status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5)); // Extract string text value from active database result column
            rx.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6)); // Extract string text value from active database result column
            rx.patient_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7)); // Extract string text value from active database result column
            rx.doctor_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8)); // Extract string text value from active database result column
            list.push_back(rx); // Execute clinical workflow step
        } // Close active scope block
        sqlite3_finalize(stmt); // Deallocate statement handle and release virtual machine resources
    } // Close active scope block

    // Hydrate prescription items using single prepared statement
    const char* itemsSql = R"SQL(
        SELECT pi.id, pi.prescription_id, pi.inventory_item_id, inv.name, pi.quantity, pi.dosage_instructions -- Column field definition and constraints
        FROM prescription_items pi -- SQL schema directive
        JOIN inventory_items inv ON pi.inventory_item_id = inv.id -- SQL schema directive
        WHERE pi.prescription_id = ?; -- SQL schema directive
    )SQL";
    if (sqlite3_prepare_v2(db_, itemsSql, -1, &stmt, nullptr) == SQLITE_OK) { // Compile SQL query into a reusable prepared statement byte code
        for (auto& rx : list) { // Iterate through items in sequence or collection
            sqlite3_reset(stmt); // Execute clinical workflow step
            sqlite3_bind_int64(stmt, 1, rx.id); // Bind integer ID/quantity parameter to prepared SQL query
            while (sqlite3_step(stmt) == SQLITE_ROW) { // Step statement execution to evaluate row or complete write operation
                PrescriptionItem it; // Execute clinical workflow step
                it.id = sqlite3_column_int64(stmt, 0); // Extract integer value from active database result column
                it.prescription_id = sqlite3_column_int64(stmt, 1); // Extract integer value from active database result column
                it.inventory_item_id = sqlite3_column_int64(stmt, 2); // Extract integer value from active database result column
                it.item_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)); // Extract string text value from active database result column
                it.quantity = sqlite3_column_int(stmt, 4); // Extract integer value from active database result column
                it.dosage_instructions = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5)); // Extract string text value from active database result column
                rx.items.push_back(it); // Execute clinical workflow step
            } // Close active scope block
        } // Close active scope block
        sqlite3_finalize(stmt); // Deallocate statement handle and release virtual machine resources
    } // Close active scope block

    return list; // Return computed result to caller
} // Close active scope block

std::optional<Prescription> Database::getPrescriptionByCode(const std::string& code) { // Execute clinical workflow step
    std::lock_guard<std::mutex> lock(dbMutex_); // Acquire RAII mutex lock ensuring thread-safe synchronized access
    sqlite3_stmt* stmt = nullptr; // Assign value to target variable
    const char* sql = R"SQL(
        SELECT pr.id, pr.prescription_code, pr.patient_id, pr.doctor_id, pr.notes, pr.status, pr.created_at, -- Column field definition and constraints
               (p.first_name || ' ' || p.last_name) AS patient_name, d.name AS doctor_name -- Column field definition and constraints
        FROM prescriptions pr -- SQL schema directive
        JOIN patients p ON pr.patient_id = p.id -- SQL schema directive
        JOIN doctors d ON pr.doctor_id = d.id -- SQL schema directive
        WHERE pr.prescription_code = ?; -- SQL schema directive
    )SQL";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) { // Compile SQL query into a reusable prepared statement byte code
        sqlite3_bind_text(stmt, 1, code.c_str(), -1, SQLITE_TRANSIENT); // Safely bind text parameter to prepared statement preventing SQL injection
        if (sqlite3_step(stmt) == SQLITE_ROW) { // Step statement execution to evaluate row or complete write operation
            Prescription rx; // Execute clinical workflow step
            rx.id = sqlite3_column_int64(stmt, 0); // Extract integer value from active database result column
            rx.prescription_code = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)); // Extract string text value from active database result column
            rx.patient_id = sqlite3_column_int64(stmt, 2); // Extract integer value from active database result column
            rx.doctor_id = sqlite3_column_int64(stmt, 3); // Extract integer value from active database result column
            const char* notes = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)); // Extract string text value from active database result column
            if (notes) rx.notes = notes; // Evaluate conditional expression to branch execution flow
            rx.status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5)); // Extract string text value from active database result column
            rx.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6)); // Extract string text value from active database result column
            rx.patient_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7)); // Extract string text value from active database result column
            rx.doctor_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8)); // Extract string text value from active database result column
            sqlite3_finalize(stmt); // Deallocate statement handle and release virtual machine resources

            // Fetch items
            const char* itemsSql = R"SQL(
                SELECT pi.id, pi.prescription_id, pi.inventory_item_id, inv.name, pi.quantity, pi.dosage_instructions -- Column field definition and constraints
                FROM prescription_items pi -- SQL schema directive
                JOIN inventory_items inv ON pi.inventory_item_id = inv.id -- SQL schema directive
                WHERE pi.prescription_id = ?; -- SQL schema directive
            )SQL";
            if (sqlite3_prepare_v2(db_, itemsSql, -1, &stmt, nullptr) == SQLITE_OK) { // Compile SQL query into a reusable prepared statement byte code
                sqlite3_bind_int64(stmt, 1, rx.id); // Bind integer ID/quantity parameter to prepared SQL query
                while (sqlite3_step(stmt) == SQLITE_ROW) { // Step statement execution to evaluate row or complete write operation
                    PrescriptionItem it; // Execute clinical workflow step
                    it.id = sqlite3_column_int64(stmt, 0); // Extract integer value from active database result column
                    it.prescription_id = sqlite3_column_int64(stmt, 1); // Extract integer value from active database result column
                    it.inventory_item_id = sqlite3_column_int64(stmt, 2); // Extract integer value from active database result column
                    it.item_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)); // Extract string text value from active database result column
                    it.quantity = sqlite3_column_int(stmt, 4); // Extract integer value from active database result column
                    it.dosage_instructions = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5)); // Extract string text value from active database result column
                    rx.items.push_back(it); // Execute clinical workflow step
                } // Close active scope block
                sqlite3_finalize(stmt); // Deallocate statement handle and release virtual machine resources
            } // Close active scope block
            return rx; // Return computed result to caller
        } // Close active scope block
        sqlite3_finalize(stmt); // Deallocate statement handle and release virtual machine resources
    } // Close active scope block
    return std::nullopt; // Return computed result to caller
} // Close active scope block

bool Database::updatePrescriptionStatus(int64_t id, const std::string& status) { // Execute clinical workflow step
    std::lock_guard<std::mutex> lock(dbMutex_); // Acquire RAII mutex lock ensuring thread-safe synchronized access
    sqlite3_stmt* stmt = nullptr; // Assign value to target variable
    const char* sql = "UPDATE prescriptions SET status = ? WHERE id = ?;"; // Assign value to target variable
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) { // Compile SQL query into a reusable prepared statement byte code
        sqlite3_bind_text(stmt, 1, status.c_str(), -1, SQLITE_TRANSIENT); // Safely bind text parameter to prepared statement preventing SQL injection
        sqlite3_bind_int64(stmt, 2, id); // Bind integer ID/quantity parameter to prepared SQL query
        bool ok = (sqlite3_step(stmt) == SQLITE_DONE); // Step statement execution to evaluate row or complete write operation
        sqlite3_finalize(stmt); // Deallocate statement handle and release virtual machine resources
        return ok; // Return computed result to caller
    } // Close active scope block
    return false; // Return computed result to caller
} // Close active scope block

std::pair<bool, std::string> Database::processSale(const SaleCheckoutRequest& req, SaleReceipt& outReceipt) { // Execute clinical workflow step
    if (req.items.empty()) { // Evaluate conditional expression to branch execution flow
        return {false, "Sale must contain at least one item"}; // Return computed result to caller
    } // Close active scope block

    // Consolidate duplicate item IDs and validate quantities
    std::map<int64_t, int> consolidatedItems; // Execute clinical workflow step
    for (const auto& itemInput : req.items) { // Iterate through items in sequence or collection
        if (itemInput.inventory_item_id <= 0) { // Evaluate conditional expression to branch execution flow
            return {false, "Invalid inventory item ID"}; // Return computed result to caller
        } // Close active scope block
        if (itemInput.quantity <= 0) { // Evaluate conditional expression to branch execution flow
            return {false, "Item quantity must be greater than zero"}; // Return computed result to caller
        } // Close active scope block
        consolidatedItems[itemInput.inventory_item_id] += itemInput.quantity; // Assign value to target variable
    } // Close active scope block

    std::lock_guard<std::mutex> lock(dbMutex_); // Acquire RAII mutex lock ensuring thread-safe synchronized access
    execute("BEGIN IMMEDIATE TRANSACTION;"); // Execute clinical workflow step

    int64_t prescriptionId = 0; // Assign value to target variable
    if (!req.prescription_code.empty()) { // Evaluate conditional expression to branch execution flow
        sqlite3_stmt* stmt = nullptr; // Assign value to target variable
        const char* pSql = "SELECT id, status FROM prescriptions WHERE prescription_code = ?;"; // Assign value to target variable
        if (sqlite3_prepare_v2(db_, pSql, -1, &stmt, nullptr) == SQLITE_OK) { // Compile SQL query into a reusable prepared statement byte code
            sqlite3_bind_text(stmt, 1, req.prescription_code.c_str(), -1, SQLITE_TRANSIENT); // Safely bind text parameter to prepared statement preventing SQL injection
            if (sqlite3_step(stmt) == SQLITE_ROW) { // Step statement execution to evaluate row or complete write operation
                prescriptionId = sqlite3_column_int64(stmt, 0); // Extract integer value from active database result column
                std::string status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)); // Extract string text value from active database result column
                if (status == "DISPENSED") { // Evaluate conditional expression to branch execution flow
                    sqlite3_finalize(stmt); // Deallocate statement handle and release virtual machine resources
                    execute("ROLLBACK;"); // Execute clinical workflow step
                    return {false, "Prescription has already been dispensed"}; // Return computed result to caller
                } // Close active scope block
            } else { // Execute default fallback branch
                sqlite3_finalize(stmt); // Deallocate statement handle and release virtual machine resources
                execute("ROLLBACK;"); // Execute clinical workflow step
                return {false, "Prescription code not found"}; // Return computed result to caller
            } // Close active scope block
            sqlite3_finalize(stmt); // Deallocate statement handle and release virtual machine resources
        } // Close active scope block
    } // Close active scope block

    // Validate items and calculate total while checking stock
    double totalAmount = 0.0; // Assign value to target variable
    struct ItemCalc { // Execute clinical workflow step
        int64_t id; // Execute clinical workflow step
        std::string name; // Execute clinical workflow step
        int requestedQty; // Execute clinical workflow step
        int currentStock; // Execute clinical workflow step
        double unitPrice; // Execute clinical workflow step
        double subtotal; // Execute clinical workflow step
    }; // Scope delimiter
    std::vector<ItemCalc> validatedItems; // Execute clinical workflow step

    for (const auto& [itemId, requestedQty] : consolidatedItems) { // Iterate through items in sequence or collection
        sqlite3_stmt* stmt = nullptr; // Assign value to target variable
        const char* iSql = "SELECT name, unit_price, stock_quantity FROM inventory_items WHERE id = ?;"; // Assign value to target variable
        if (sqlite3_prepare_v2(db_, iSql, -1, &stmt, nullptr) == SQLITE_OK) { // Compile SQL query into a reusable prepared statement byte code
            sqlite3_bind_int64(stmt, 1, itemId); // Bind integer ID/quantity parameter to prepared SQL query
            if (sqlite3_step(stmt) == SQLITE_ROW) { // Step statement execution to evaluate row or complete write operation
                ItemCalc ic; // Execute clinical workflow step
                ic.id = itemId; // Assign value to target variable
                ic.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)); // Extract string text value from active database result column
                ic.unitPrice = sqlite3_column_double(stmt, 1); // Extract floating point numerical value from active result column
                ic.currentStock = sqlite3_column_int(stmt, 2); // Extract integer value from active database result column
                ic.requestedQty = requestedQty; // Assign value to target variable
                ic.subtotal = ic.unitPrice * ic.requestedQty; // Assign value to target variable

                if (ic.requestedQty > ic.currentStock) { // Evaluate conditional expression to branch execution flow
                    sqlite3_finalize(stmt); // Deallocate statement handle and release virtual machine resources
                    execute("ROLLBACK;"); // Execute clinical workflow step
                    return {false, "Insufficient stock for: " + ic.name + " (Available: " + std::to_string(ic.currentStock) + ")"}; // Return computed result to caller
                } // Close active scope block

                totalAmount += ic.subtotal; // Assign value to target variable
                validatedItems.push_back(ic); // Execute clinical workflow step
            } else { // Execute default fallback branch
                sqlite3_finalize(stmt); // Deallocate statement handle and release virtual machine resources
                execute("ROLLBACK;"); // Execute clinical workflow step
                return {false, "Inventory item ID " + std::to_string(itemId) + " does not exist"}; // Return computed result to caller
            } // Close active scope block
            sqlite3_finalize(stmt); // Deallocate statement handle and release virtual machine resources
        } // Close active scope block
    } // Close active scope block

    if (validatedItems.empty()) { // Evaluate conditional expression to branch execution flow
        execute("ROLLBACK;"); // Execute clinical workflow step
        return {false, "Sale must contain at least one item"}; // Return computed result to caller
    } // Close active scope block

    std::string txCode = generateUniqueCode("TX"); // Assign value to target variable
    std::string paymentStatus = "PAID"; // Assign value to target variable

    // Insert sales_transactions
    sqlite3_stmt* stmt = nullptr; // Assign value to target variable
    const char* txSql = "INSERT INTO sales_transactions (transaction_code, prescription_id, total_amount, payment_method, payment_status) VALUES (?, ?, ?, ?, ?);"; // Assign value to target variable
    int64_t txId = 0; // Assign value to target variable
    if (sqlite3_prepare_v2(db_, txSql, -1, &stmt, nullptr) == SQLITE_OK) { // Compile SQL query into a reusable prepared statement byte code
        sqlite3_bind_text(stmt, 1, txCode.c_str(), -1, SQLITE_TRANSIENT); // Safely bind text parameter to prepared statement preventing SQL injection
        if (prescriptionId > 0) { // Evaluate conditional expression to branch execution flow
            sqlite3_bind_int64(stmt, 2, prescriptionId); // Bind integer ID/quantity parameter to prepared SQL query
        } else { // Execute default fallback branch
            sqlite3_bind_null(stmt, 2); // Execute clinical workflow step
        } // Close active scope block
        sqlite3_bind_double(stmt, 3, totalAmount); // Bind floating-point numerical parameter to prepared query
        sqlite3_bind_text(stmt, 4, req.payment_method.c_str(), -1, SQLITE_TRANSIENT); // Safely bind text parameter to prepared statement preventing SQL injection
        sqlite3_bind_text(stmt, 5, paymentStatus.c_str(), -1, SQLITE_TRANSIENT); // Safely bind text parameter to prepared statement preventing SQL injection
        if (sqlite3_step(stmt) == SQLITE_DONE) { // Step statement execution to evaluate row or complete write operation
            txId = sqlite3_last_insert_rowid(db_); // Assign value to target variable
        } // Close active scope block
        sqlite3_finalize(stmt); // Deallocate statement handle and release virtual machine resources
    } // Close active scope block

    if (txId <= 0) { // Evaluate conditional expression to branch execution flow
        execute("ROLLBACK;"); // Execute clinical workflow step
        return {false, "Failed to record sales transaction"}; // Return computed result to caller
    } // Close active scope block

    // Insert transaction line items & deduct stock atomically
    for (const auto& item : validatedItems) { // Iterate through items in sequence or collection
        const char* liSql = "INSERT INTO sales_transaction_items (transaction_id, inventory_item_id, quantity, unit_price, subtotal) VALUES (?, ?, ?, ?, ?);"; // Assign value to target variable
        if (sqlite3_prepare_v2(db_, liSql, -1, &stmt, nullptr) == SQLITE_OK) { // Compile SQL query into a reusable prepared statement byte code
            sqlite3_bind_int64(stmt, 1, txId); // Bind integer ID/quantity parameter to prepared SQL query
            sqlite3_bind_int64(stmt, 2, item.id); // Bind integer ID/quantity parameter to prepared SQL query
            sqlite3_bind_int(stmt, 3, item.requestedQty); // Bind integer ID/quantity parameter to prepared SQL query
            sqlite3_bind_double(stmt, 4, item.unitPrice); // Bind floating-point numerical parameter to prepared query
            sqlite3_bind_double(stmt, 5, item.subtotal); // Bind floating-point numerical parameter to prepared query
            sqlite3_step(stmt); // Step statement execution to evaluate row or complete write operation
            sqlite3_finalize(stmt); // Deallocate statement handle and release virtual machine resources
        } // Close active scope block

        int newStock = item.currentStock - item.requestedQty; // Assign value to target variable
        const char* deductSql = "UPDATE inventory_items SET stock_quantity = ?, updated_at = CURRENT_TIMESTAMP WHERE id = ?;"; // Assign value to target variable
        if (sqlite3_prepare_v2(db_, deductSql, -1, &stmt, nullptr) == SQLITE_OK) { // Compile SQL query into a reusable prepared statement byte code
            sqlite3_bind_int(stmt, 1, newStock); // Bind integer ID/quantity parameter to prepared SQL query
            sqlite3_bind_int64(stmt, 2, item.id); // Bind integer ID/quantity parameter to prepared SQL query
            sqlite3_step(stmt); // Step statement execution to evaluate row or complete write operation
            sqlite3_finalize(stmt); // Deallocate statement handle and release virtual machine resources
        } // Close active scope block

        SaleReceiptItem ri; // Execute clinical workflow step
        ri.name = item.name; // Assign value to target variable
        ri.quantity = item.requestedQty; // Assign value to target variable
        ri.unit_price = item.unitPrice; // Assign value to target variable
        ri.subtotal = item.subtotal; // Assign value to target variable
        outReceipt.items.push_back(ri); // Execute clinical workflow step
    } // Close active scope block

    if (prescriptionId > 0) { // Evaluate conditional expression to branch execution flow
        const char* rxUpSql = "UPDATE prescriptions SET status = 'DISPENSED' WHERE id = ?;"; // Assign value to target variable
        if (sqlite3_prepare_v2(db_, rxUpSql, -1, &stmt, nullptr) == SQLITE_OK) { // Compile SQL query into a reusable prepared statement byte code
            sqlite3_bind_int64(stmt, 1, prescriptionId); // Bind integer ID/quantity parameter to prepared SQL query
            sqlite3_step(stmt); // Step statement execution to evaluate row or complete write operation
            sqlite3_finalize(stmt); // Deallocate statement handle and release virtual machine resources
        } // Close active scope block
    } // Close active scope block

    execute("COMMIT;"); // Execute clinical workflow step

    outReceipt.transaction_code = txCode; // Assign value to target variable
    outReceipt.total_amount = totalAmount; // Assign value to target variable
    outReceipt.payment_status = paymentStatus; // Assign value to target variable
    outReceipt.created_at = "JUST NOW"; // Assign value to target variable

    return {true, txCode}; // Return computed result to caller
} // Close active scope block

} // namespace aidans
