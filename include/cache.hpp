#pragma once

#include <string>
#include <vector>
#include <unordered_set>
#include <optional>

namespace aidans {

// ponytail: in-memory stub; cut external Redis socket dependency
class CacheManager {
public:
    explicit CacheManager(const std::string& = "127.0.0.1", int = 6379) {}
    ~CacheManager() = default;

    void setRedisConfig(const std::string&, int) {}
    bool isRedisOnline() { return false; }
    bool publishInventoryUpdate(const std::string&, int64_t = 0) { return true; }

    void cacheBookedSlot(int64_t, const std::string&, const std::string&) {}
    bool isSlotBooked(int64_t, const std::string&, const std::string&) { return false; }
    void setBookedSlots(int64_t, const std::string&, const std::vector<std::string>&) {}

    void setLowStockItem(int64_t, bool) {}
    std::unordered_set<int64_t> getLowStockItemIds() { return {}; }

    void pushPendingPrescription(const std::string&) {}
    std::vector<std::string> getPendingPrescriptions() { return {}; }
    void removePendingPrescription(const std::string&) {}

    void setPaymentSession(const std::string&, const std::string&) {}
    std::optional<std::string> getPaymentSession(const std::string&) { return std::nullopt; }
    void clearPaymentSession(const std::string&) {}
};

} // namespace aidans
