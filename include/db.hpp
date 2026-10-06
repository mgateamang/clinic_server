#pragma once

#include <string>
#include <vector>
#include <optional>
#include <mutex>
#include <sqlite3.h>
#include "models.hpp"

namespace aidans {

class Database {
public:
    explicit Database(const std::string& dbPath);
    ~Database();

    // Disable copy
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    bool initSchema();
    void seedInitialDataIfEmpty();

    // Doctors
    std::vector<Doctor> getDoctors();
    std::optional<Doctor> getDoctorById(int64_t id);
    int64_t createDoctor(const Doctor& doc);

    // Patients
    int64_t findOrCreatePatient(const Patient& patient);
    std::optional<Patient> getPatientById(int64_t id);

    // Appointments
    std::vector<std::string> getBookedSlotsForDoctor(int64_t doctorId, const std::string& date);
    std::vector<Appointment> getAppointments(const std::optional<std::string>& date = std::nullopt,
                                            const std::optional<std::string>& status = std::nullopt);
    std::optional<Appointment> getAppointmentById(int64_t id);
    bool bookAppointment(Appointment& appt, const Patient& patientInfo);

    // Inventory
    std::vector<InventoryItem> getInventory(bool lowStockOnly = false, const std::string& search = "");
    std::optional<InventoryItem> getInventoryItem(int64_t id);
    std::optional<InventoryItem> getInventoryItemBySku(const std::string& sku);
    int64_t createInventoryItem(const InventoryItem& item);
    bool restockItem(int64_t id, int delta, const std::string& reason);
    std::vector<InventoryItem> getLowStockAlerts();

    // Prescriptions
    std::string createPrescription(const Prescription& rx);
    std::vector<Prescription> getPrescriptions(const std::optional<std::string>& status = std::nullopt,
                                              std::optional<int64_t> patientId = std::nullopt);
    std::optional<Prescription> getPrescriptionByCode(const std::string& code);
    bool updatePrescriptionStatus(int64_t id, const std::string& status);

    // POS & Sales (Atomic transaction)
    std::pair<bool, std::string> processSale(const SaleCheckoutRequest& req, SaleReceipt& outReceipt);

private:
    sqlite3* db_{nullptr};
    std::mutex dbMutex_;

    bool execute(const std::string& sql);
    std::string generateUniqueCode(const std::string& prefix);
};

} // namespace aidans
