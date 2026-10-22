#pragma once // Ensure header file is included only once per translation unit

/**
 * @file models.hpp
 * @brief Core Domain Entities and Data Transfer Objects (DTOs) for AIDANS Clinic Server.
 *
 * This header defines all primary domain entities and DTOs exchanged across the REST API,
 * database persistence layer, and client desktop applications. Each struct leverages
 * nlohmann::json intrusive serialization macros to provide automated, type-safe
 * JSON conversions with minimal boilerplate and zero runtime reflection overhead.
 */

#include <string> // Include string for declarations and types
#include <vector> // Include vector for declarations and types
#include <nlohmann/json.hpp> // Include nlohmann/json.hpp for declarations and types

namespace aidans { // Execute clinical workflow step

/**
 * @struct Doctor
 * @brief Healthcare practitioner profile and professional contact details.
 */
struct Doctor { // Execute clinical workflow step
    int64_t id{0};               ///< Unique database identifier
    std::string name;            ///< Full professional name (e.g. "Dr. Gregory House")
    std::string specialization;  ///< Medical specialty (e.g. "Diagnostic Medicine", "Cardiology")
    std::string email;           ///< Professional clinic email address
    std::string phone;           ///< Clinic contact or office extension phone number

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(Doctor, id, name, specialization, email, phone) // Generate compile-time intrusive JSON serialization macros
}; // Scope delimiter

/**
 * @struct Patient
 * @brief Registered clinic patient demographic and contact profile.
 */
struct Patient { // Execute clinical workflow step
    int64_t id{0};               ///< Unique patient database identifier
    std::string first_name;      ///< Patient first / given name
    std::string last_name;       ///< Patient last / family name
    std::string date_of_birth;   ///< Date of birth in ISO 8601 format (YYYY-MM-DD)
    std::string phone;           ///< Primary contact phone number (indexed for fast lookups)
    std::string email;           ///< Contact email address
    std::string address;         ///< Residential home address

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(Patient, id, first_name, last_name, date_of_birth, phone, email, address) // Generate compile-time intrusive JSON serialization macros
}; // Scope delimiter

/**
 * @struct Appointment
 * @brief Scheduled consultation slot between a doctor and patient.
 */
struct Appointment { // Execute clinical workflow step
    int64_t id{0};               ///< Primary key identifier
    int64_t patient_id{0};       ///< Foreign key referencing patients table
    int64_t doctor_id{0};        ///< Foreign key referencing doctors table
    std::string appointment_time;///< Scheduled start timestamp (YYYY-MM-DD HH:MM)
    std::string reason;          ///< Chief complaint or visit reason
    std::string status;          ///< Appointment state: PENDING, CONFIRMED, COMPLETED, CANCELLED, NO_SHOW
    std::string created_at;      ///< Audit creation timestamp
    std::string patient_name;    ///< Denormalized patient full name for API responses
    std::string patient_phone;   ///< Denormalized patient phone number
    std::string doctor_name;     ///< Denormalized doctor display name

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Appointment, id, patient_id, doctor_id, appointment_time, reason, status, created_at, patient_name, patient_phone, doctor_name) // Generate compile-time intrusive JSON serialization macros
}; // Scope delimiter

/**
 * @struct InventoryItem
 * @brief Medical supply, consumable, or pharmaceutical catalog item with real-time stock levels.
 */
struct InventoryItem { // Execute clinical workflow step
    int64_t id{0};               ///< Unique item ID
    std::string sku;             ///< Barcode or unique Stock Keeping Unit (e.g. "MED-AMOX-500")
    std::string name;            ///< Trade or generic product name (e.g. "Amoxicillin 500mg")
    std::string category;        ///< Product category (e.g. "Antibiotics", "Analgesics")
    double unit_price{0.0};      ///< Retail selling price per unit
    int stock_quantity{0};       ///< Quantity currently physically available in stock
    int reorder_threshold{10};   ///< Minimum safety limit triggering replenishment alerts
    std::string unit{"unit"};    ///< Measurement unit (e.g. "tablets", "bottles", "boxes")
    std::string updated_at;      ///< Timestamp of last inventory transaction

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(InventoryItem, id, sku, name, category, unit_price, stock_quantity, reorder_threshold, unit, updated_at) // Generate compile-time intrusive JSON serialization macros
}; // Scope delimiter

/**
 * @struct PrescriptionItem
 * @brief Individual medication line item within a digital prescription order.
 */
struct PrescriptionItem { // Execute clinical workflow step
    int64_t id{0};                    ///< Primary key
    int64_t prescription_id{0};       ///< Foreign key linking to parent prescription
    int64_t inventory_item_id{0};     ///< Foreign key linking to formulary inventory item
    std::string item_name;            ///< Medication name snapshot
    int quantity{0};                  ///< Units prescribed to dispense
    std::string dosage_instructions;  ///< Clinical directions (e.g. "1 capsule bid pc for 7 days")

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(PrescriptionItem, id, prescription_id, inventory_item_id, item_name, quantity, dosage_instructions) // Generate compile-time intrusive JSON serialization macros
}; // Scope delimiter

/**
 * @struct Prescription
 * @brief Digital medical order authored by an attending physician.
 */
struct Prescription { // Execute clinical workflow step
    int64_t id{0};                                  ///< Primary key
    std::string prescription_code;                  ///< Unique reference code (e.g. "RX-123456-7890")
    int64_t patient_id{0};                          ///< Patient ID
    int64_t doctor_id{0};                           ///< Doctor ID
    std::string notes;                              ///< Diagnostic notes or clinical summary
    std::string status;                             ///< State: PENDING, FILLED, DISPENSED, CANCELLED
    std::string created_at;                         ///< Timestamp prescription was authorized
    std::string patient_name;                       ///< Patient full name
    std::string doctor_name;                        ///< Prescribing physician full name
    std::vector<PrescriptionItem> items;            ///< Array of prescribed medications

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Prescription, id, prescription_code, patient_id, doctor_id, notes, status, created_at, patient_name, doctor_name, items) // Generate compile-time intrusive JSON serialization macros
}; // Scope delimiter

/**
 * @struct SaleItemInput
 * @brief Input specification for a line item being purchased in a POS checkout.
 */
struct SaleItemInput { // Execute clinical workflow step
    int64_t inventory_item_id{0};     ///< Inventory item ID to sell
    int quantity{0};                  ///< Units being purchased

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(SaleItemInput, inventory_item_id, quantity) // Generate compile-time intrusive JSON serialization macros
}; // Scope delimiter

/**
 * @struct SaleCheckoutRequest
 * @brief Request payload submitted by POS cashier to process a checkout.
 */
struct SaleCheckoutRequest { // Execute clinical workflow step
    std::string prescription_code;    ///< Optional prescription code if dispensing an RX
    std::string payment_method;       ///< Payment method: CASH, CARD, E_WALLET, WEBHOOK_MOCK
    std::vector<SaleItemInput> items; ///< Array of items being purchased

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(SaleCheckoutRequest, prescription_code, payment_method, items) // Generate compile-time intrusive JSON serialization macros
}; // Scope delimiter

/**
 * @struct SaleReceiptItem
 * @brief Itemized line item detail returned inside a completed sale receipt.
 */
struct SaleReceiptItem { // Execute clinical workflow step
    std::string name;                 ///< Purchased item name
    int quantity{0};                  ///< Units purchased
    double unit_price{0.0};           ///< Unit price charged
    double subtotal{0.0};             ///< Total charged for this line item (quantity * unit_price)

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(SaleReceiptItem, name, quantity, unit_price, subtotal) // Generate compile-time intrusive JSON serialization macros
}; // Scope delimiter

/**
 * @struct SaleReceipt
 * @brief Finalized sale transaction record and audited billing receipt.
 */
struct SaleReceipt { // Execute clinical workflow step
    std::string transaction_code;     ///< Unique transaction identifier (e.g. "TXN-123456-7890")
    double total_amount{0.0};         ///< Grand total amount paid
    std::string payment_status;       ///< Payment status: PENDING, PAID, FAILED, REFUNDED
    std::string created_at;           ///< Timestamp transaction was settled
    std::vector<SaleReceiptItem> items;///< Array of itemized line items

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(SaleReceipt, transaction_code, total_amount, payment_status, created_at, items) // Generate compile-time intrusive JSON serialization macros
}; // Scope delimiter

/**
 * @struct PaymentWebhookPayload
 * @brief Asynchronous payment gateway confirmation notification payload.
 */
struct PaymentWebhookPayload { // Execute clinical workflow step
    std::string event_id;             ///< Unique payment provider event ID
    std::string transaction_code;     ///< Transaction reference code
    std::string status;               ///< Gateway settlement outcome (PAID, FAILED)
    double amount{0.0};               ///< Amount processed
    std::string gateway_reference;    ///< External gateway reference ID
    std::string timestamp;            ///< Gateway event timestamp

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(PaymentWebhookPayload, event_id, transaction_code, status, amount, gateway_reference, timestamp) // Generate compile-time intrusive JSON serialization macros
}; // Scope delimiter

/**
 * @struct DailySummaryReport
 * @brief Aggregated clinical and financial metrics for daily operations.
 */
struct DailySummaryReport { // Execute clinical workflow step
    std::string date;                             ///< Summary reporting date (YYYY-MM-DD)
    double total_sales_amount{0.0};               ///< Gross sales revenue
    int total_transactions{0};                    ///< Number of settled POS sales
    int total_prescriptions_dispensed{0};         ///< Number of completed RX dispensations
    int low_stock_items_count{0};                 ///< Items currently below reorder thresholds

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(DailySummaryReport, date, total_sales_amount, total_transactions, total_prescriptions_dispensed, low_stock_items_count) // Generate compile-time intrusive JSON serialization macros
}; // Scope delimiter

} // namespace aidans
