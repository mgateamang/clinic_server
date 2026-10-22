# AIDANS Clinic Server (C++20 REST API)

## Executive Summary (For Engineering Leaders & Hiring Managers)

**`clinic_server`** is the high-performance core backend service for the **AIDANS** (Automated Integrated Distributed Architecture for Networked Surgeries / Systems) healthcare management platform.

Architected in modern **C++20** with an embedded **SQLite3** transactional storage engine in Write-Ahead Logging (WAL) mode, this microservice exposes an enterprise-grade RESTful API powering:
- **Doctor & Staff Scheduling:** Doctor roster management and slot availability calculations.
- **Patient Appointment Booking:** Atomic scheduling with concurrency protection and collision prevention.
- **Medical Inventory & Supply Chain:** Real-time stock tracking, SKUs, reorder threshold alerts, and audited restock operations.
- **Doctor e-Prescriptions:** Generation and verification of tamper-evident prescription codes (`RX-XXXXXX-XXXX`).
- **Point-of-Sale (POS) & Pharmacy Dispensing:** Atomic sales transactions that simultaneously decrement stock, dispense prescription items, calculate taxes/discounts, and emit timestamped financial audit receipts.

---

## Architectural Highlights

1. **Lightweight & Dependency-Free Native Binary:**
   - Powered by [cpp-httplib](include/httplib.h) for multi-threaded HTTP/1.1 request routing.
   - [nlohmann/json](subprojects/nlohmann_json-3.12.0) for idiomatic C++ JSON serialization.
   - Embedded [SQLite 3.45+](subprojects/sqlite-amalgamation-3530400) compiled in-tree. No external database server setup or cloud database dependency required.
2. **High-Performance Concurrency & Safety:**
   - SQLite WAL (Write-Ahead Logging) mode allows simultaneous readers without blocking writers.
   - Explicit thread-safe database mutex guards prevent data corruption.
   - Atomic database transactions (`BEGIN IMMEDIATE TRANSACTION` / `COMMIT`) ensure POS checkouts and medication dispensing never suffer from race conditions or overselling.
3. **CORS & Static Asset Hosting:**
   - Built-in pre-routing CORS headers allow web clients (`clinic_portal`) to interact seamlessly from any origin.
   - Built-in static web server mounts the patient portal directly at `/` if configured.

---

## REST API Specification

| Method | Endpoint | Description |
|---|---|---|
| `GET` | `/health` | Server health check and service readiness status |
| `GET` | `/api/v1/doctors` | List all available clinic doctors and specialties |
| `GET` | `/api/v1/doctors/:id/available-slots?date=YYYY-MM-DD` | Get free 30-minute booking slots for a doctor on a given day |
| `GET` | `/api/v1/appointments` | List appointments (filterable by `?date=YYYY-MM-DD` and `?status=...`) |
| `POST` | `/api/v1/appointments` | Book a new patient appointment (validates slot availability) |
| `GET` | `/api/v1/inventory` | List clinic catalog; support `?search=` and `?low_stock=true` |
| `POST` | `/api/v1/inventory` | Create a new stock item with SKU and reorder thresholds |
| `POST` | `/api/v1/inventory/:id/restock` | Restock medicine supplies with delta count and audit reason |
| `GET` | `/api/v1/prescriptions/:code` | Retrieve digital prescription details and item checklist |
| `POST` | `/api/v1/prescriptions` | Doctor issues a digital prescription with medication lines |
| `POST` | `/api/v1/pos/checkout` | Process pharmacy checkout, decrement inventory, and issue receipt |

---

## Building and Running

### Prerequisites
- C++20 compliant compiler (GCC 11+, Clang 13+, or MSVC 2022 / 2026)
- Meson build system (`pip install meson ninja`)

### Compile
```bash
# Setup build directory
meson setup build

# Compile release binary
meson compile -C build
```

### Run
```bash
./build/clinic_server --port 8080 --db aidans.db --static-dir ../clinic_portal
```
