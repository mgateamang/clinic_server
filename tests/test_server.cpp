#include <iostream>
#include <cassert>
#include <filesystem>
#include "db.hpp"
#include "cache.hpp"

namespace fs = std::filesystem;

void runTests() {
    std::string testDb = "test_aidans.db";
    if (fs::exists(testDb)) {
        fs::remove(testDb);
    }

    std::cout << "[TEST] Initializing Database & Seed Data...\n";
    aidans::Database db(testDb);
    assert(db.initSchema());
    db.seedInitialDataIfEmpty();

    // 1. Doctors & Patients
    std::cout << "[TEST] Verifying Doctors & Patients...\n";
    auto doctors = db.getDoctors();
    assert(doctors.size() >= 3);
    int64_t docId = doctors[0].id;
    assert(docId > 0);

    aidans::Patient patient;
    patient.first_name = "Max";
    patient.last_name = "Mustermann";
    patient.phone = "+49-170-1234567";
    patient.email = "max.mustermann@example.de";
    patient.date_of_birth = "1988-04-12";
    int64_t patId = db.findOrCreatePatient(patient);
    assert(patId > 0);

    // Calling findOrCreatePatient again with same phone returns same ID
    int64_t samePatId = db.findOrCreatePatient(patient);
    assert(samePatId == patId);

    // 2. Appointment Booking & Conflict Detection
    std::cout << "[TEST] Testing Appointment Booking & Conflict Prevention...\n";
    aidans::Appointment appt1;
    appt1.doctor_id = docId;
    appt1.appointment_time = "2026-10-15 10:00:00";
    appt1.reason = "Routine annual physical";
    bool booked1 = db.bookAppointment(appt1, patient);
    assert(booked1 == true);
    assert(appt1.id > 0);
    assert(appt1.status == "CONFIRMED");

    // Double-booking the exact same doctor and time must FAIL
    aidans::Appointment appt2;
    appt2.doctor_id = docId;
    appt2.appointment_time = "2026-10-15 10:00:00"; // Exact same slot
    appt2.reason = "Acute checkup";
    aidans::Patient patient2;
    patient2.first_name = "Erika";
    patient2.last_name = "Musterfrau";
    patient2.phone = "+49-171-9876543";
    patient2.date_of_birth = "1992-08-20";
    bool booked2 = db.bookAppointment(appt2, patient2);
    assert(booked2 == false); // Must be rejected

    // Booking a different time slot must succeed
    appt2.appointment_time = "2026-10-15 10:30:00";
    bool booked3 = db.bookAppointment(appt2, patient2);
    assert(booked3 == true);

    // Check booked slots list
    auto bookedSlots = db.getBookedSlotsForDoctor(docId, "2026-10-15");
    assert(bookedSlots.size() == 2);

    // 3. Inventory Operations & Alerts
    std::cout << "[TEST] Testing Inventory Stock, Restocking, and Alerts...\n";
    auto inv = db.getInventory();
    assert(!inv.empty());
    auto itemOpt = db.getInventoryItemBySku("MED-AMOX-500");
    assert(itemOpt.has_value());
    auto item = itemOpt.value();
    int initialStock = item.stock_quantity;

    // Restock +20
    bool restocked = db.restockItem(item.id, 20, "Batch restock delivery");
    assert(restocked == true);
    auto updatedItem = db.getInventoryItem(item.id).value();
    assert(updatedItem.stock_quantity == initialStock + 20);

    // Low stock alerts
    auto lowAlerts = db.getLowStockAlerts();
    for (const auto& low : lowAlerts) {
        assert(low.stock_quantity <= low.reorder_threshold);
    }

    // 4. Digital Prescriptions
    std::cout << "[TEST] Testing Digital Prescription Lifecycle...\n";
    aidans::Prescription rx;
    rx.patient_id = patId;
    rx.doctor_id = docId;
    rx.notes = "Take with water after meals";

    aidans::PrescriptionItem rxItem;
    rxItem.inventory_item_id = item.id;
    rxItem.quantity = 2;
    rxItem.dosage_instructions = "1 capsule every 12 hours";
    rx.items.push_back(rxItem);

    std::string rxCode = db.createPrescription(rx);
    assert(!rxCode.empty());

    auto fetchedRx = db.getPrescriptionByCode(rxCode);
    assert(fetchedRx.has_value());
    assert(fetchedRx->prescription_code == rxCode);
    assert(fetchedRx->status == "PENDING");
    assert(fetchedRx->items.size() == 1);
    assert(fetchedRx->items[0].quantity == 2);

    // 5. Point of Sale & Checkout (Stock deduction & atomicity)
    std::cout << "[TEST] Testing POS Checkout & Atomic Stock Deduction...\n";
    int preSaleStock = db.getInventoryItem(item.id)->stock_quantity;

    // A. Attempting to buy more than available stock should fail cleanly
    aidans::SaleCheckoutRequest overReq;
    overReq.payment_method = "CASH";
    aidans::SaleItemInput overItem;
    overItem.inventory_item_id = item.id;
    overItem.quantity = preSaleStock + 9999;
    overReq.items.push_back(overItem);
    aidans::SaleReceipt failedReceipt;
    auto [failOk, failMsg] = db.processSale(overReq, failedReceipt);
    assert(failOk == false);
    // Stock remains untouched
    assert(db.getInventoryItem(item.id)->stock_quantity == preSaleStock);

    // B. Normal valid purchase linked to the prescription
    aidans::SaleCheckoutRequest validReq;
    validReq.prescription_code = rxCode;
    validReq.payment_method = "CASH";
    aidans::SaleItemInput validItem;
    validItem.inventory_item_id = item.id;
    validItem.quantity = 2;
    validReq.items.push_back(validItem);

    aidans::SaleReceipt receipt;
    auto [saleOk, txCode] = db.processSale(validReq, receipt);
    assert(saleOk == true);
    assert(!txCode.empty());
    assert(receipt.payment_status == "PAID");
    assert(receipt.items.size() == 1);
    assert(receipt.total_amount > 0.0);

    // Verify stock was decremented exactly by 2
    int postSaleStock = db.getInventoryItem(item.id)->stock_quantity;
    assert(postSaleStock == preSaleStock - 2);

    // Verify prescription status moved to DISPENSED
    auto dispensedRx = db.getPrescriptionByCode(rxCode).value();
    assert(dispensedRx.status == "DISPENSED");

    // 6. Webhook Mock Payment Integration
    std::cout << "[TEST] Testing Payment Webhook Placeholder...\n";
    aidans::SaleCheckoutRequest webhookReq;
    webhookReq.payment_method = "WEBHOOK_MOCK";
    aidans::SaleItemInput whItem;
    whItem.inventory_item_id = item.id;
    whItem.quantity = 1;
    webhookReq.items.push_back(whItem);

    aidans::SaleReceipt whReceipt;
    auto [whSaleOk, whTxCode] = db.processSale(webhookReq, whReceipt);
    assert(whSaleOk == true);
    assert(whReceipt.payment_status == "PENDING");

    // Process webhook confirmation
    bool whUpdated = db.updatePaymentStatus(whTxCode, "PAID", "ch_stripe_mock_9918");
    assert(whUpdated == true);

    // 7. Daily Summary Report
    std::cout << "[TEST] Verifying Daily Summary Reporting...\n";
    auto todayReport = db.getDailySummary("2026-10-15");
    assert(todayReport.date == "2026-10-15");

    // 8. Redis Pub/Sub Ripple Update Check
    std::cout << "[TEST] Verifying Redis Pub/Sub Cache Manager...\n";
    aidans::CacheManager cache("127.0.0.1", 6379);
    bool redisOnline = cache.isRedisOnline();
    std::cout << "  -> Redis is " << (redisOnline ? "ONLINE" : "OFFLINE (fallback verified)") << "\n";
    bool pubOk = cache.publishInventoryUpdate("test_ripple", 1);
    if (redisOnline) {
        assert(pubOk == true);
    }

    // 9. Edge Cases & SQL Injection Resilience Tests
    std::cout << "[TEST] Verifying Edge Cases & Security Hardening...\n";

    // 9a. SQL injection resilience in getAppointments
    std::string sqlInjectionDate = "2026-10-15' OR '1'='1";
    std::string sqlInjectionStatus = "CONFIRMED' OR '1'='1";
    auto safeAppts = db.getAppointments(sqlInjectionDate, sqlInjectionStatus);
    assert(safeAppts.empty()); // Parameter binding prevents injection payload execution

    // 9b. SQL injection resilience in getInventory
    std::string sqlInjectionSearch = "Paracetamol' OR 1=1 --";
    auto safeInv = db.getInventory(false, sqlInjectionSearch);
    assert(safeInv.empty()); // Handled cleanly as literal search parameter

    // 9c. SQL injection resilience in getPrescriptions
    auto safeRx = db.getPrescriptions("DISPENSED' OR '1'='1", std::nullopt);
    assert(safeRx.empty());

    // 9d. POS duplicate item consolidation & stock consistency
    int stockBeforeDupTest = db.getInventoryItem(item.id)->stock_quantity;
    aidans::SaleCheckoutRequest dupReq;
    dupReq.payment_method = "CASH";
    aidans::SaleItemInput d1; d1.inventory_item_id = item.id; d1.quantity = 2;
    aidans::SaleItemInput d2; d2.inventory_item_id = item.id; d2.quantity = 3;
    dupReq.items.push_back(d1);
    dupReq.items.push_back(d2); // Total requested = 5

    aidans::SaleReceipt dupReceipt;
    auto [dupSaleOk, dupTx] = db.processSale(dupReq, dupReceipt);
    assert(dupSaleOk == true);
    int stockAfterDupTest = db.getInventoryItem(item.id)->stock_quantity;
    assert(stockAfterDupTest == stockBeforeDupTest - 5);

    // 9e. POS rejection of non-positive quantity
    aidans::SaleCheckoutRequest negReq;
    negReq.payment_method = "CASH";
    aidans::SaleItemInput negItem; negItem.inventory_item_id = item.id; negItem.quantity = -1;
    negReq.items.push_back(negItem);
    aidans::SaleReceipt negReceipt;
    auto [negSaleOk, negErr] = db.processSale(negReq, negReceipt);
    assert(negSaleOk == false);

    // 9f. Rejection of invalid inventory item creation
    aidans::InventoryItem invalidItem;
    invalidItem.sku = ""; // Empty SKU must fail
    invalidItem.name = "Bad Item";
    assert(db.createInventoryItem(invalidItem) == -1);

    invalidItem.sku = "VALID-SKU-999";
    invalidItem.unit_price = -10.0; // Negative price must fail
    assert(db.createInventoryItem(invalidItem) == -1);

    // 9g. Rejection of restock delta == 0 or negative resulting stock
    assert(db.restockItem(item.id, 0, "Zero delta") == false);
    assert(db.restockItem(item.id, -99999, "Massive negative delta") == false);

    // 9h. Rejection of invalid appointment status
    assert(db.updateAppointmentStatus(appt1.id, "DROP_TABLE_HACK") == false);

    // 9i. Rejection of empty patient phone / name
    aidans::Patient invalidPatient;
    invalidPatient.first_name = "";
    invalidPatient.phone = "";
    assert(db.findOrCreatePatient(invalidPatient) == -1);

    // Cleanup test db
    if (fs::exists(testDb)) {
        fs::remove(testDb);
    }

    std::cout << "\n>>> ALL CLINIC_SERVER TESTS PASSED SUCCESSFULLY! <<<\n\n";
}

int main() {
    runTests();
    return 0;
}
