#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace aidans {

struct Doctor {
    int64_t id{0};
    std::string name;
    std::string specialization;
    std::string email;
    std::string phone;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(Doctor, id, name, specialization, email, phone)
};

struct Patient {
    int64_t id{0};
    std::string first_name;
    std::string last_name;
    std::string date_of_birth;
    std::string phone;
    std::string email;
    std::string address;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(Patient, id, first_name, last_name, date_of_birth, phone, email, address)
};

struct Appointment {
    int64_t id{0};
    int64_t patient_id{0};
    int64_t doctor_id{0};
    std::string appointment_time;
    std::string reason;
    std::string status; // PENDING, CONFIRMED, COMPLETED, CANCELLED, NO_SHOW
    std::string created_at;
    std::string patient_name;
    std::string patient_phone;
    std::string doctor_name;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Appointment, id, patient_id, doctor_id, appointment_time, reason, status, created_at, patient_name, patient_phone, doctor_name)
};

struct InventoryItem {
    int64_t id{0};
    std::string sku;
    std::string name;
    std::string category;
    double unit_price{0.0};
    int stock_quantity{0};
    int reorder_threshold{10};
    std::string unit{"unit"};
    std::string updated_at;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(InventoryItem, id, sku, name, category, unit_price, stock_quantity, reorder_threshold, unit, updated_at)
};

struct PrescriptionItem {
    int64_t id{0};
    int64_t prescription_id{0};
    int64_t inventory_item_id{0};
    std::string item_name;
    int quantity{0};
    std::string dosage_instructions;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(PrescriptionItem, id, prescription_id, inventory_item_id, item_name, quantity, dosage_instructions)
};

struct Prescription {
    int64_t id{0};
    std::string prescription_code;
    int64_t patient_id{0};
    int64_t doctor_id{0};
    std::string notes;
    std::string status; // PENDING, FILLED, DISPENSED, CANCELLED
    std::string created_at;
    std::string patient_name;
    std::string doctor_name;
    std::vector<PrescriptionItem> items;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Prescription, id, prescription_code, patient_id, doctor_id, notes, status, created_at, patient_name, doctor_name, items)
};

struct SaleItemInput {
    int64_t inventory_item_id{0};
    int quantity{0};

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(SaleItemInput, inventory_item_id, quantity)
};

struct SaleCheckoutRequest {
    std::string prescription_code; // Optional, empty string if direct OTC sale
    std::string payment_method;    // CASH, CARD, E_WALLET, WEBHOOK_MOCK
    std::vector<SaleItemInput> items;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(SaleCheckoutRequest, prescription_code, payment_method, items)
};

struct SaleReceiptItem {
    std::string name;
    int quantity{0};
    double unit_price{0.0};
    double subtotal{0.0};

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(SaleReceiptItem, name, quantity, unit_price, subtotal)
};

struct SaleReceipt {
    std::string transaction_code;
    double total_amount{0.0};
    std::string payment_status; // PENDING, PAID, FAILED, REFUNDED
    std::string created_at;
    std::vector<SaleReceiptItem> items;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(SaleReceipt, transaction_code, total_amount, payment_status, created_at, items)
};

struct PaymentWebhookPayload {
    std::string event_id;
    std::string transaction_code;
    std::string status; // PAID, FAILED
    double amount{0.0};
    std::string gateway_reference;
    std::string timestamp;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(PaymentWebhookPayload, event_id, transaction_code, status, amount, gateway_reference, timestamp)
};

struct DailySummaryReport {
    std::string date;
    double total_sales_amount{0.0};
    int total_transactions{0};
    int total_prescriptions_dispensed{0};
    int low_stock_items_count{0};

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(DailySummaryReport, date, total_sales_amount, total_transactions, total_prescriptions_dispensed, low_stock_items_count)
};

} // namespace aidans
