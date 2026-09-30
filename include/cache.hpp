#pragma once

#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <deque>
#include <mutex>
#include <shared_mutex>
#include <optional>

namespace aidans {

class CacheManager {
public:
    CacheManager() = default;
    ~CacheManager() = default;

    // Doctor booked slots cache: doctor:{id}:slots:{date}
    void cacheBookedSlot(int64_t doctorId, const std::string& date, const std::string& timeSlot);
    bool isSlotBooked(int64_t doctorId, const std::string& date, const std::string& timeSlot);
    void setBookedSlots(int64_t doctorId, const std::string& date, const std::vector<std::string>& slots);

    // Low stock items: inventory:low_stock_set
    void setLowStockItem(int64_t itemId, bool isLowStock);
    std::unordered_set<int64_t> getLowStockItemIds();

    // Prescription FIFO queue: prescription:queue:pending
    void pushPendingPrescription(const std::string& rxCode);
    std::vector<std::string> getPendingPrescriptions();
    void removePendingPrescription(const std::string& rxCode);

    // Payment ephemeral state: payment:tx:{code}
    void setPaymentSession(const std::string& txCode, const std::string& jsonState);
    std::optional<std::string> getPaymentSession(const std::string& txCode);
    void clearPaymentSession(const std::string& txCode);

private:
    std::shared_mutex mutex_;
    // doctor_id -> map of date -> set of booked time strings
    std::unordered_map<std::string, std::unordered_set<std::string>> slotCache_;
    std::unordered_set<int64_t> lowStockSet_;
    std::deque<std::string> pendingPrescriptionQueue_;
    std::unordered_map<std::string, std::string> paymentSessions_;
};

} // namespace aidans
