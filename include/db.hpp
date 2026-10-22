#pragma once // Ensure header file is included only once per translation unit

/**
 * @file db.hpp
 * @brief Thread-Safe SQLite Persistence Layer for AIDANS Clinic Server.
 *
 * This class abstracts and encapsulates all relational database operations for the
 * clinic. It enforces ACID compliance, parameterized queries (protecting against
 * SQL injection), Write-Ahead Logging (WAL) for high concurrency, and atomic
 * transactions for critical operations like multi-item POS sales and stock deduction.
 *
 * Architecture Highlights:
 *  - Mutex Synchronized: A std::mutex guarantees thread-safety when multiple worker
 *    threads access the SQLite connection concurrently.
 *  - WAL Mode Enabled: Readers and writers run concurrently without lock starvation.
 *  - Foreign Key Constraints Enforced: Relational integrity between doctors, patients,
 *    appointments, prescriptions, and inventory items is strictly preserved.
 */

#include <string> // Include string for declarations and types
#include <vector> // Include vector for declarations and types
#include <optional> // Include optional for declarations and types
#include <mutex> // Include mutex for declarations and types
#include <sqlite3.h> // Include sqlite3.h for declarations and types
#include "models.hpp" // Include models.hpp for declarations and types

namespace aidans { // Execute clinical workflow step

class Database { // Execute clinical workflow step
public: // Execute clinical workflow step
    /**
     * @brief Opens or creates the SQLite database at the specified file path.
     * @param dbPath Absolute or relative filesystem path to the .db file.
     */
    explicit Database(const std::string& dbPath); // Execute clinical workflow step

    /**
     * @brief Closes the SQLite connection cleanly and flushes WAL buffers.
     */
    ~Database(); // Execute clinical workflow step

    // Disable copy semantics to ensure exclusive handle ownership
    Database(const Database&) = delete; // Assign value to target variable
    Database& operator=(const Database&) = delete; // Assign value to target variable

    /**
     * @brief Creates tables and indexes if they do not already exist.
     * Enables WAL journal mode, busy timeouts, and foreign key cascades.
     * @return true if initialization succeeded, false otherwise.
     */
    bool initSchema(); // Execute clinical workflow step

    /**
     * @brief Seeds initial doctor profiles and essential pharmaceutical inventory
     * if the database is newly created and empty.
     */
    void seedInitialDataIfEmpty(); // Execute clinical workflow step

    // -----------------------------------------------------------------------
    // Doctor Management
    // -----------------------------------------------------------------------

    /**
     * @brief Retrieves all active doctors in the clinic directory.
     * @return Vector of Doctor models ordered alphabetically by name.
     */
    std::vector<Doctor> getDoctors(); // Execute clinical workflow step

    /**
     * @brief Fetches a single doctor by primary key ID.
     * @param id Doctor unique ID.
     * @return std::optional containing the Doctor if found, or std::nullopt.
     */
    std::optional<Doctor> getDoctorById(int64_t id); // Execute clinical workflow step

    /**
     * @brief Inserts a new doctor record into the database.
     * @param doc Doctor details.
     * @return Newly generated doctor ID, or -1 on error.
     */
    int64_t createDoctor(const Doctor& doc); // Execute clinical workflow step

    // -----------------------------------------------------------------------
    // Patient Management
    // -----------------------------------------------------------------------

    /**
     * @brief Upsert helper: finds an existing patient by phone number or creates a new one.
     * Eliminates duplicate patient records while simplifying booking workflows.
     * @param patient Patient information.
     * @return Existing or newly created patient ID.
     */
    int64_t findOrCreatePatient(const Patient& patient); // Execute clinical workflow step

    /**
     * @brief Fetches a patient record by ID.
     * @param id Patient primary key.
     * @return std::optional containing Patient if found.
     */
    std::optional<Patient> getPatientById(int64_t id); // Execute clinical workflow step

    // -----------------------------------------------------------------------
    // Appointment Scheduling
    // -----------------------------------------------------------------------

    /**
     * @brief Retrieves all booked appointment slot times for a doctor on a specific date.
     * Used by the booking engine to calculate free time windows.
     * @param doctorId Doctor ID.
     * @param date Date formatted as YYYY-MM-DD.
     * @return Vector of booked start times (e.g. ["09:00", "09:30", "14:00"]).
     */
    std::vector<std::string> getBookedSlotsForDoctor(int64_t doctorId, const std::string& date); // Execute clinical workflow step

    /**
     * @brief Queries appointments with optional date and status filters.
     * @param date Optional date filter (YYYY-MM-DD).
     * @param status Optional lifecycle filter ('PENDING', 'CONFIRMED', etc.).
     * @return Vector of matching Appointment models with joined patient & doctor names.
     */
    std::vector<Appointment> getAppointments(const std::optional<std::string>& date = std::nullopt, // Assign value to target variable
                                            const std::optional<std::string>& status = std::nullopt); // Assign value to target variable

    /**
     * @brief Fetches a single appointment by ID.
     */
    std::optional<Appointment> getAppointmentById(int64_t id); // Execute clinical workflow step

    /**
     * @brief Atomically books an appointment.
     * Resolves the patient record, validates slot availability to prevent double-booking,
     * and inserts the appointment inside a protected critical section.
     * @param appt Appointment details (updated with new ID upon success).
     * @param patientInfo Patient contact and demographic info.
     * @return true if successfully booked; false if slot already taken or DB error.
     */
    bool bookAppointment(Appointment& appt, const Patient& patientInfo); // Execute clinical workflow step

    // -----------------------------------------------------------------------
    // Inventory & Supply Chain
    // -----------------------------------------------------------------------

    /**
     * @brief Lists inventory items, with optional search and low-stock filters.
     * @param lowStockOnly If true, returns only items where stock_quantity <= reorder_threshold.
     * @param search Substring filter applied against SKU, item name, or category.
     * @return Vector of InventoryItem models.
     */
    std::vector<InventoryItem> getInventory(bool lowStockOnly = false, const std::string& search = ""); // Assign value to target variable

    /**
     * @brief Fetches an inventory item by ID.
     */
    std::optional<InventoryItem> getInventoryItem(int64_t id); // Execute clinical workflow step

    /**
     * @brief Fetches an inventory item by its unique SKU code.
     */
    std::optional<InventoryItem> getInventoryItemBySku(const std::string& sku); // Execute clinical workflow step

    /**
     * @brief Inserts a new medical supply or medicine into the catalog.
     * @return Generated inventory item ID, or -1 on conflict/error.
     */
    int64_t createInventoryItem(const InventoryItem& item); // Execute clinical workflow step

    /**
     * @brief Increments or decrements inventory quantity and logs an audit trail.
     * @param id Inventory item ID.
     * @param delta Stock difference (+ positive for restock, - negative for adjustments).
     * @param reason Audit justification (e.g., "Supplier Delivery", "Damaged goods").
     * @return true on success, false if stock would become negative or item not found.
     */
    bool restockItem(int64_t id, int delta, const std::string& reason); // Execute clinical workflow step

    /**
     * @brief Shortcut to query all items currently below their safe reorder threshold.
     */
    std::vector<InventoryItem> getLowStockAlerts(); // Execute clinical workflow step

    // -----------------------------------------------------------------------
    // Digital Prescriptions (e-Prescribe)
    // -----------------------------------------------------------------------

    /**
     * @brief Creates a physician's digital prescription order with all line items.
     * Automatically generates a tamper-evident unique prescription code.
     * @param rx Prescription header and medication items.
     * @return Generated unique code (e.g. "RX-654321-1234"), or empty string on error.
     */
    std::string createPrescription(const Prescription& rx); // Execute clinical workflow step

    /**
     * @brief Queries prescriptions by status or patient ID.
     */
    std::vector<Prescription> getPrescriptions(const std::optional<std::string>& status = std::nullopt, // Assign value to target variable
                                              std::optional<int64_t> patientId = std::nullopt); // Assign value to target variable

    /**
     * @brief Looks up a prescription by its human-readable code.
     * Returns full nested details including medication line items.
     */
    std::optional<Prescription> getPrescriptionByCode(const std::string& code); // Execute clinical workflow step

    /**
     * @brief Updates prescription lifecycle status (e.g. to 'DISPENSED').
     */
    bool updatePrescriptionStatus(int64_t id, const std::string& status); // Execute clinical workflow step

    // -----------------------------------------------------------------------
    // Point-of-Sale (POS) & Billing (Atomic Transaction)
    // -----------------------------------------------------------------------

    /**
     * @brief Executes a POS sale checkout in an ACID-compliant database transaction.
     *
     * Business Logic Executed Atomically:
     *  1. Validates that every line item has sufficient shelf stock.
     *  2. Decrements inventory for all purchased medications.
     *  3. Records audit log entries for stock reductions.
     *  4. If a prescription_code is linked, transitions its status to 'DISPENSED'.
     *  5. Creates a sale order record and itemized sales lines.
     *  6. Commits transaction and constructs an official SaleReceipt.
     *
     * If any validation fails, the entire transaction is rolled back cleanly.
     *
     * @param req Checkout request details (items, prices, payment method, discounts).
     * @param outReceipt Populated with generated receipt and totals upon success.
     * @return Pair: {true, ""} on success, or {false, "Error reason"} on failure.
     */
    std::pair<bool, std::string> processSale(const SaleCheckoutRequest& req, SaleReceipt& outReceipt); // Execute clinical workflow step

private: // Execute clinical workflow step
    sqlite3* db_{nullptr};     ///< Native SQLite3 database connection handle
    std::mutex dbMutex_;       ///< Mutex ensuring serialized, thread-safe DB query execution

    /**
     * @brief Internal helper to execute non-query SQL statements.
     */
    bool execute(const std::string& sql); // Execute clinical workflow step

    /**
     * @brief Helper to generate cryptographically uniform, human-readable codes.
     * @param prefix Code prefix string (e.g. "RX", "RCP").
     * @return Formatted code (e.g. "PREFIX-123456-7890").
     */
    std::string generateUniqueCode(const std::string& prefix); // Execute clinical workflow step
}; // Scope delimiter

} // namespace aidans
