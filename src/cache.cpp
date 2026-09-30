#include "cache.hpp"
#include <algorithm>

namespace aidans {

static std::string makeSlotKey(int64_t doctorId, const std::string& date) {
    return std::to_string(doctorId) + ":" + date;
}

void CacheManager::cacheBookedSlot(int64_t doctorId, const std::string& date, const std::string& timeSlot) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    slotCache_[makeSlotKey(doctorId, date)].insert(timeSlot);
}

bool CacheManager::isSlotBooked(int64_t doctorId, const std::string& date, const std::string& timeSlot) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = slotCache_.find(makeSlotKey(doctorId, date));
    if (it != slotCache_.end()) {
        return it->second.find(timeSlot) != it->second.end();
    }
    return false;
}

void CacheManager::setBookedSlots(int64_t doctorId, const std::string& date, const std::vector<std::string>& slots) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto& set = slotCache_[makeSlotKey(doctorId, date)];
    set.clear();
    for (const auto& s : slots) {
        set.insert(s);
    }
}

void CacheManager::setLowStockItem(int64_t itemId, bool isLowStock) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (isLowStock) {
        lowStockSet_.insert(itemId);
    } else {
        lowStockSet_.erase(itemId);
    }
}

std::unordered_set<int64_t> CacheManager::getLowStockItemIds() {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return lowStockSet_;
}

void CacheManager::pushPendingPrescription(const std::string& rxCode) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (std::find(pendingPrescriptionQueue_.begin(), pendingPrescriptionQueue_.end(), rxCode) == pendingPrescriptionQueue_.end()) {
        pendingPrescriptionQueue_.push_back(rxCode);
    }
}

std::vector<std::string> CacheManager::getPendingPrescriptions() {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return std::vector<std::string>(pendingPrescriptionQueue_.begin(), pendingPrescriptionQueue_.end());
}

void CacheManager::removePendingPrescription(const std::string& rxCode) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    pendingPrescriptionQueue_.erase(
        std::remove(pendingPrescriptionQueue_.begin(), pendingPrescriptionQueue_.end(), rxCode),
        pendingPrescriptionQueue_.end()
    );
}

void CacheManager::setPaymentSession(const std::string& txCode, const std::string& jsonState) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    paymentSessions_[txCode] = jsonState;
}

std::optional<std::string> CacheManager::getPaymentSession(const std::string& txCode) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = paymentSessions_.find(txCode);
    if (it != paymentSessions_.end()) {
        return it->second;
    }
    return std::nullopt;
}

void CacheManager::clearPaymentSession(const std::string& txCode) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    paymentSessions_.erase(txCode);
}

} // namespace aidans
