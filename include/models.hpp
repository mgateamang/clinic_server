#pragma once

/**
 * @file models.hpp
 * @brief Core Domain Entities and Data Transfer Objects (DTOs) for AIDANS Clinic Server.
 *
 * This header defines the foundational data models exchanged across the REST API,
 * database persistence layer, and client applications. Each struct leverages
 * nlohmann::json intrusive serialization macros to provide automated, type-safe
 * JSON conversions with minimal boilerplate and zero runtime reflection overhead.
 *
 * Key Entities:
 *  - Doctor: Medical practitioner profile and clinical contact info.
 *  - Patient: Patient demographic record with medical identification.
 *  - Appointment: Time-slotted booking between a patient and a doctor.
 *  - InventoryItem: Medical supply/pharmaceutical catalog item with real-time stock levels.
 *  - Prescription & PrescriptionItem: Digital medical order issued by physicians.
 *  - SaleCheckoutRequest & SaleReceipt: Point-of-Sale transaction records with line items.
 */

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace aidans {

/**
 * @struct Doctor
 * @brief Represents a healthcare provider / doctor in the clinic system.
 */
struct Doctor {
    int64_t id{0};               ///< Unique database identifier
    std::string name;            ///< Full professional name (e.g., "Dr. Gregory House")
    std::string specialization;  ///< Clinical specialty (e.g., "Cardiology", "Pediatrics")
    std::string email;           ///< Staff work email address
    std::string phone;           ///< Direct extension or office phone

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(Doctor, id, name, specialization, email, phone)
};

/**
 * @struct Patient
 * @brief Represents a patient registered with the clinic.
 */
struct Patient {
    int64_t id{0};               ///< Unique patient identifier
    std::string first_name;      ///< Given name
    std::string last_name;       ///< Family surname
    std::string date_of_birth;   ///< ISO 8601 Date (YYYY-MM-DD)
    std::string phone;           ///< Primary phone number (used for quick appointment lookups)
    std::string email;           ///< Optional contact email
    std::string address;         ///< Residential address

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(Patient, id, first_name, last_name, date_of_birth, phone, email, address)
};

/**
 * @struct Appointment
 * @brief Represents an appointment booking slot between a doctor and patient.
 */
struct Appointment {
    int64_t id{0};               ///< Primary key
    int64_t patient_id{0};       ///< Foreign key referencing Patient
    int64_t doctor_id{0};        ///< Foreign key referencing Doctor
    std::string appointment_time;///< Scheduled start timestamp (YYYY-MM-DD HH:MM)
    std::string reason;          ///< Reason for consultation / chief complaint
    std::string status;          ///< Lifecycle status: 'PENDING', 'CONFIRMED', 'COMPLETED', 'CANCELLED', 'NO_SHOW'
    std::string created_at;      ///< Audit creation timestamp
    std::string patient_name;    ///< Denormalized patient display name for API convenience
    std::string patient_phone;   ///< Denormalized patient contact phone
    std::string doctor_name;     ///< Denormalized doctor display name

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Appointment, id, patient_id, doctor_id, appointment_time, reason, status, created_at, patient_name, patient_phone, doctor_name)
};

/**
 * @struct InventoryItem
 * @brief Represents a pharmaceutical or medical supply item in stock.
 */
struct InventoryItem {
    int64_t id{0};               ///< Unique item ID
    std::string sku;             ///< Stock Keeping Unit barcode / identifier (e.g., "MED-AMOX-500")
    std::string name;            ///< Commercial or generic medicine name (e.g., "Amoxicillin 500mg")
    std::string category;        ///< Classification (e.g., "Antibiotics", "Analgesics")
    double unit_price{0.0};      ///< Retail selling price per unit
    int stock_quantity{0};       ///< Physical units currently on shelves / in storeroom
    int reorder_threshold{10};   ///< Safety stock limit triggering low-inventory restock alerts
    std::string unit{"unit"};    ///< Measurement unit ("tablets", "vials", "boxes")
    std::string updated_at;      ///< Timestamp of last stock modification

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(InventoryItem, id, sku, name, category, unit_price, stock_quantity, reorder_threshold, unit, updated_at)
};

/**
 * @struct PrescriptionItem
 * @brief Single medication line item prescribed within an e-Prescription.
 */
struct PrescriptionItem {
    int64_t id{0};                    ///< Line item record ID
    int64_t prescription_id{0};       ///< Parent prescription ID
    int64_t inventory_item_id{0};     ///< Foreign key linking directly to inventory item
    std::string item_name;            ///< Medication name snapshot
    int quantity{0};                  ///< Prescribed quantity to dispense
    std::string dosage_instructions;  ///< Clinical administration guide (e.g., "1 tab PO q8h x 7d")

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(PrescriptionItem, id, prescription_id, inventory_item_id, item_name, quantity, dosage_instructions)
};

/**
 * @struct Prescription
 * @brief Represents a digital prescription order created by an authorized doctor.
 */
struct Prescription {
    int64_t id{0};                                  ///< Primary key
    std::string prescription_code;                  ///< Unique human-readable code (e.g. "RX-123456-7890")
    int64_t patient_id{0};                          ///< Patient receiving medication
    int64_t doctor_id{0};                           ///< Doctor prescribing medication
    std::string notes;                              ///< Diagnostic comments or special directions
    std::string status;                             ///< State: 'PENDING', 'FILLED', 'DISPENSED', 'CANCELLED'
    std::string created_at;                         ///< Issuance timestamp
    std::string patient_name;                       ///< Patient full name
    std::string doctor_name;                        ///< Prescribing physician full name
    std::vector<PrescriptionItem> items;            ///< List of prescribed medication lines

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Prescription, id, prescription_code, patient_id, doctor_id, notes, status, created_at, patient_name, doctor_name, items)
};

/**
 * @struct SaleLineItem
 * @brief Line item inside a Point-of-Sale checkout payload.
 */
struct SaleLineItem {
    int64_t inventory_item_id{0};     ///< Target inventory item to sell
    std::string item_name;            ///< Item description
    int quantity{0};                  ///< Quantity purchased
    double unit_price{0.0};           ///< Unit price applied
    double line_total{0.0};           ///< Calculated as quantity * unit_price

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(SaleLineItem, inventory_item_id, item_name, quantity, unit_price, line_total)
};

/**
 * @struct SaleCheckoutRequest
 * @brief Incoming payload received when cashier/pharmacist processes a sale.
 */
struct SaleCheckoutRequest {
    std::string prescription_code;    ///< Optional RX code if dispensing prescription
    int64_t patient_id{0};            ///< Patient account ID (if linked)
    std::string cashier_name{"Staff"};///< Staff member running the POS register
    std::string payment_method{"CASH"};///< Payment type: CASH, CARD, INSURANCE, QR_TRANSFER
    double discount_amount{0.0};      ///< Absolute discount applied
    double tax_rate{0.0};             ///< Percentage sales tax rate (e.g., 0.05 for 5%)
    std::vector<SaleLineItem> items;  ///< Array of sold items

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(SaleCheckoutRequest, prescription_code, patient_id, cashier_name, payment_method, discount_amount, tax_rate, items)
};

/**
 * @struct SaleReceipt
 * @brief Outgoing audited receipt generated upon successful atomic checkout.
 */
struct SaleReceipt {
    std::string receipt_code;         ///< Unique transaction reference (e.g. "RCP-XXXXXX-XXXX")
    std::string timestamp;            ///< ISO transaction completion timestamp
    double subtotal{0.0};             ///< Sum of all line item totals before adjustments
    double discount_amount{0.0};      ///< Discount deducted
    double tax_amount{0.0};           ///< Tax applied
    double grand_total{0.0};          ///< Final billed and collected amount
    std::string payment_method;       ///< Payment tender verified
    std::string prescription_code;    ///< Associated prescription code if dispensed
    std::vector<SaleLineItem> items;  ///< Finalized line items
    bool dispensed_prescription{false};///< True if this sale marked the linked prescription as DISPENSED

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(SaleReceipt, receipt_code, timestamp, subtotal, discount_amount, tax_amount, grand_total, payment_method, prescription_code, items, dispensed_prescription)
};

} // namespace aidans
