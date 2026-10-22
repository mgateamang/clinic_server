#include <iostream> // Include iostream for declarations and types
#include <cassert> // Include cassert for declarations and types
#include <filesystem> // Include filesystem for declarations and types
#include "db.hpp" // Include db.hpp for declarations and types
#include "cache.hpp" // Include cache.hpp for declarations and types

namespace fs = std::filesystem; // Assign value to target variable

void runTests() { // Execute clinical workflow step
    std::cout << std::unitbuf; // Print diagnostic informational message to standard output
    std::string testDb = "test_aidans.db"; // Assign value to target variable
    std::error_code ec; // Execute clinical workflow step
    fs::remove(testDb, ec); // Execute clinical workflow step
    fs::remove(testDb + "-wal", ec); // Execute clinical workflow step
    fs::remove(testDb + "-shm", ec); // Execute clinical workflow step

    { // Scope delimiter
        std::cout << "[TEST] Initializing Database & Seed Data...\n"; // Print diagnostic informational message to standard output
        aidans::Database db(testDb); // Execute clinical workflow step
    assert(db.initSchema()); // Execute clinical workflow step
    db.seedInitialDataIfEmpty(); // Execute clinical workflow step

    // 1. Doctors & Patients
    std::cout << "[TEST] Verifying Doctors & Patients...\n"; // Print diagnostic informational message to standard output
    auto doctors = db.getDoctors(); // Assign value to target variable
    assert(doctors.size() >= 3); // Assign value to target variable
    int64_t docId = doctors[0].id; // Assign value to target variable
    assert(docId > 0); // Execute clinical workflow step

    aidans::Patient patient; // Execute clinical workflow step
    patient.first_name = "Max"; // Assign value to target variable
    patient.last_name = "Mustermann"; // Assign value to target variable
    patient.phone = "+49-170-1234567"; // Assign value to target variable
    patient.email = "max.mustermann@example.de"; // Assign value to target variable
    patient.date_of_birth = "1988-04-12"; // Assign value to target variable
    int64_t patId = db.findOrCreatePatient(patient); // Assign value to target variable
    assert(patId > 0); // Execute clinical workflow step

    // Calling findOrCreatePatient again with same phone returns same ID
    int64_t samePatId = db.findOrCreatePatient(patient); // Assign value to target variable
    assert(samePatId == patId); // Assign value to target variable

    // 2. Appointment Booking & Conflict Detection
    std::cout << "[TEST] Testing Appointment Booking & Conflict Prevention...\n"; // Print diagnostic informational message to standard output
    aidans::Appointment appt1; // Execute clinical workflow step
    appt1.doctor_id = docId; // Assign value to target variable
    appt1.appointment_time = "2026-10-15 10:00:00"; // Assign value to target variable
    appt1.reason = "Routine annual physical"; // Assign value to target variable
    bool booked1 = db.bookAppointment(appt1, patient); // Assign value to target variable
    assert(booked1 == true); // Assign value to target variable
    assert(appt1.id > 0); // Execute clinical workflow step
    assert(appt1.status == "CONFIRMED"); // Assign value to target variable

    // Double-booking the exact same doctor and time must FAIL
    aidans::Appointment appt2; // Execute clinical workflow step
    appt2.doctor_id = docId; // Assign value to target variable
    appt2.appointment_time = "2026-10-15 10:00:00"; // Exact same slot
    appt2.reason = "Acute checkup"; // Assign value to target variable
    aidans::Patient patient2; // Execute clinical workflow step
    patient2.first_name = "Erika"; // Assign value to target variable
    patient2.last_name = "Musterfrau"; // Assign value to target variable
    patient2.phone = "+49-171-9876543"; // Assign value to target variable
    patient2.date_of_birth = "1992-08-20"; // Assign value to target variable
    bool booked2 = db.bookAppointment(appt2, patient2); // Assign value to target variable
    assert(booked2 == false); // Must be rejected

    // Booking a different time slot must succeed
    appt2.appointment_time = "2026-10-15 10:30:00"; // Assign value to target variable
    bool booked3 = db.bookAppointment(appt2, patient2); // Assign value to target variable
    assert(booked3 == true); // Assign value to target variable

    // Check booked slots list
    auto bookedSlots = db.getBookedSlotsForDoctor(docId, "2026-10-15"); // Assign value to target variable
    assert(bookedSlots.size() == 2); // Assign value to target variable

    // 3. Inventory Operations & Alerts
    std::cout << "[TEST] Testing Inventory Stock, Restocking, and Alerts...\n"; // Print diagnostic informational message to standard output
    auto inv = db.getInventory(); // Assign value to target variable
    assert(!inv.empty()); // Execute clinical workflow step
    auto itemOpt = db.getInventoryItemBySku("MED-AMOX-500"); // Assign value to target variable
    assert(itemOpt.has_value()); // Execute clinical workflow step
    auto item = itemOpt.value(); // Assign value to target variable
    int initialStock = item.stock_quantity; // Assign value to target variable

    // Restock +20
    bool restocked = db.restockItem(item.id, 20, "Batch restock delivery"); // Assign value to target variable
    assert(restocked == true); // Assign value to target variable
    auto updatedItem = db.getInventoryItem(item.id).value(); // Assign value to target variable
    assert(updatedItem.stock_quantity == initialStock + 20); // Assign value to target variable

    // Low stock alerts
    auto lowAlerts = db.getLowStockAlerts(); // Assign value to target variable
    for (const auto& low : lowAlerts) { // Iterate through items in sequence or collection
        assert(low.stock_quantity <= low.reorder_threshold); // Assign value to target variable
    } // Close active scope block

    // 4. Digital Prescriptions
    std::cout << "[TEST] Testing Digital Prescription Lifecycle...\n"; // Print diagnostic informational message to standard output
    aidans::Prescription rx; // Execute clinical workflow step
    rx.patient_id = patId; // Assign value to target variable
    rx.doctor_id = docId; // Assign value to target variable
    rx.notes = "Take with water after meals"; // Assign value to target variable

    aidans::PrescriptionItem rxItem; // Execute clinical workflow step
    rxItem.inventory_item_id = item.id; // Assign value to target variable
    rxItem.quantity = 2; // Assign value to target variable
    rxItem.dosage_instructions = "1 capsule every 12 hours"; // Assign value to target variable
    rx.items.push_back(rxItem); // Execute clinical workflow step

    std::string rxCode = db.createPrescription(rx); // Assign value to target variable
    assert(!rxCode.empty()); // Execute clinical workflow step

    auto fetchedRx = db.getPrescriptionByCode(rxCode); // Assign value to target variable
    assert(fetchedRx.has_value()); // Execute clinical workflow step
    assert(fetchedRx->prescription_code == rxCode); // Assign value to target variable
    assert(fetchedRx->status == "PENDING"); // Assign value to target variable
    assert(fetchedRx->items.size() == 1); // Assign value to target variable
    assert(fetchedRx->items[0].quantity == 2); // Assign value to target variable

    // 5. Point of Sale & Checkout (Stock deduction & atomicity)
    std::cout << "[TEST] Testing POS Checkout & Atomic Stock Deduction...\n"; // Print diagnostic informational message to standard output
    int preSaleStock = db.getInventoryItem(item.id)->stock_quantity; // Assign value to target variable

    // A. Attempting to buy more than available stock should fail cleanly
    aidans::SaleCheckoutRequest overReq; // Execute clinical workflow step
    overReq.payment_method = "CASH"; // Assign value to target variable
    aidans::SaleItemInput overItem; // Execute clinical workflow step
    overItem.inventory_item_id = item.id; // Assign value to target variable
    overItem.quantity = preSaleStock + 9999; // Assign value to target variable
    overReq.items.push_back(overItem); // Execute clinical workflow step
    aidans::SaleReceipt failedReceipt; // Execute clinical workflow step
    auto [failOk, failMsg] = db.processSale(overReq, failedReceipt); // Assign value to target variable
    assert(failOk == false); // Assign value to target variable
    // Stock remains untouched
    assert(db.getInventoryItem(item.id)->stock_quantity == preSaleStock); // Assign value to target variable

    // B. Normal valid purchase linked to the prescription
    aidans::SaleCheckoutRequest validReq; // Execute clinical workflow step
    validReq.prescription_code = rxCode; // Assign value to target variable
    validReq.payment_method = "CASH"; // Assign value to target variable
    aidans::SaleItemInput validItem; // Execute clinical workflow step
    validItem.inventory_item_id = item.id; // Assign value to target variable
    validItem.quantity = 2; // Assign value to target variable
    validReq.items.push_back(validItem); // Execute clinical workflow step

    aidans::SaleReceipt receipt; // Execute clinical workflow step
    auto [saleOk, txCode] = db.processSale(validReq, receipt); // Assign value to target variable
    assert(saleOk == true); // Assign value to target variable
    assert(!txCode.empty()); // Execute clinical workflow step
    assert(receipt.payment_status == "PAID"); // Assign value to target variable
    assert(receipt.items.size() == 1); // Assign value to target variable
    assert(receipt.total_amount > 0.0); // Execute clinical workflow step

    // Verify stock was decremented exactly by 2
    int postSaleStock = db.getInventoryItem(item.id)->stock_quantity; // Assign value to target variable
    assert(postSaleStock == preSaleStock - 2); // Assign value to target variable

    // Verify prescription status moved to DISPENSED
    auto dispensedRx = db.getPrescriptionByCode(rxCode).value(); // Assign value to target variable
    assert(dispensedRx.status == "DISPENSED"); // Assign value to target variable

    // 6. Edge Cases & SQL Injection Resilience Tests
    std::cout << "[TEST] Verifying Edge Cases & Security Hardening...\n"; // Print diagnostic informational message to standard output

    // 9a. SQL injection resilience in getAppointments
    std::string sqlInjectionDate = "2026-10-15' OR '1'='1"; // Assign value to target variable
    std::string sqlInjectionStatus = "CONFIRMED' OR '1'='1"; // Assign value to target variable
    auto safeAppts = db.getAppointments(sqlInjectionDate, sqlInjectionStatus); // Assign value to target variable
    assert(safeAppts.empty()); // Parameter binding prevents injection payload execution

    // 9b. SQL injection resilience in getInventory
    std::string sqlInjectionSearch = "Paracetamol' OR 1=1 --"; // Assign value to target variable
    auto safeInv = db.getInventory(false, sqlInjectionSearch); // Assign value to target variable
    assert(safeInv.empty()); // Handled cleanly as literal search parameter

    // 9c. SQL injection resilience in getPrescriptions
    auto safeRx = db.getPrescriptions("DISPENSED' OR '1'='1", std::nullopt); // Assign value to target variable
    assert(safeRx.empty()); // Execute clinical workflow step

    // 9d. POS duplicate item consolidation & stock consistency
    int stockBeforeDupTest = db.getInventoryItem(item.id)->stock_quantity; // Assign value to target variable
    aidans::SaleCheckoutRequest dupReq; // Execute clinical workflow step
    dupReq.payment_method = "CASH"; // Assign value to target variable
    aidans::SaleItemInput d1; d1.inventory_item_id = item.id; d1.quantity = 2; // Assign value to target variable
    aidans::SaleItemInput d2; d2.inventory_item_id = item.id; d2.quantity = 3; // Assign value to target variable
    dupReq.items.push_back(d1); // Execute clinical workflow step
    dupReq.items.push_back(d2); // Total requested = 5

    aidans::SaleReceipt dupReceipt; // Execute clinical workflow step
    auto [dupSaleOk, dupTx] = db.processSale(dupReq, dupReceipt); // Assign value to target variable
    assert(dupSaleOk == true); // Assign value to target variable
    int stockAfterDupTest = db.getInventoryItem(item.id)->stock_quantity; // Assign value to target variable
    assert(stockAfterDupTest == stockBeforeDupTest - 5); // Assign value to target variable

    // 9e. POS rejection of non-positive quantity
    aidans::SaleCheckoutRequest negReq; // Execute clinical workflow step
    negReq.payment_method = "CASH"; // Assign value to target variable
    aidans::SaleItemInput negItem; negItem.inventory_item_id = item.id; negItem.quantity = -1; // Assign value to target variable
    negReq.items.push_back(negItem); // Execute clinical workflow step
    aidans::SaleReceipt negReceipt; // Execute clinical workflow step
    auto [negSaleOk, negErr] = db.processSale(negReq, negReceipt); // Assign value to target variable
    assert(negSaleOk == false); // Assign value to target variable

    // 9f. Rejection of invalid inventory item creation
    aidans::InventoryItem invalidItem; // Execute clinical workflow step
    invalidItem.sku = ""; // Empty SKU must fail
    invalidItem.name = "Bad Item"; // Assign value to target variable
    assert(db.createInventoryItem(invalidItem) == -1); // Assign value to target variable

    invalidItem.sku = "VALID-SKU-999"; // Assign value to target variable
    invalidItem.unit_price = -10.0; // Negative price must fail
    assert(db.createInventoryItem(invalidItem) == -1); // Assign value to target variable

    // 9g. Rejection of restock delta == 0 or negative resulting stock
    assert(db.restockItem(item.id, 0, "Zero delta") == false); // Assign value to target variable
    assert(db.restockItem(item.id, -99999, "Massive negative delta") == false); // Assign value to target variable

    // Rejection of empty patient phone / name
    aidans::Patient invalidPatient; // Execute clinical workflow step
    invalidPatient.first_name = ""; // Assign value to target variable
    invalidPatient.phone = ""; // Assign value to target variable
    assert(db.findOrCreatePatient(invalidPatient) == -1); // Assign value to target variable
    } // Close active scope block

    // Cleanup test db
    fs::remove(testDb, ec); // Execute clinical workflow step
    fs::remove(testDb + "-wal", ec); // Execute clinical workflow step
    fs::remove(testDb + "-shm", ec); // Execute clinical workflow step

    std::cout << "\n>>> ALL CLINIC_SERVER TESTS PASSED SUCCESSFULLY! <<<\n\n"; // Print diagnostic informational message to standard output
} // Close active scope block

int main() { // Execute clinical workflow step
    runTests(); // Execute clinical workflow step
    return 0; // Return computed result to caller
} // Close active scope block
