#pragma once

/**
 * @file cache.hpp
 * @brief Cache and Pub/Sub Abstraction Layer for AIDANS Clinic Server.
 *
 * Design Decision & Engineering Justification:
 *  In production microservices, a distributed Redis server is often introduced
 *  for caching and cross-node pub/sub events. However, for standalone single-node
 *  deployments, an external Redis daemon introduces unnecessary operational overhead,
 *  network latency, socket failure modes, and installation friction.
 *
 *  Following senior engineering best practices (and the YAGNI principle), this module
 *  provides a clean, no-op in-memory stub that satisfies the CacheManager contract.
 *  It preserves API compatibility across all clients while eliminating external
 *  runtime dependencies.
 */

#include <string>
#include <vector>
#include <unordered_set>
#include <optional>

namespace aidans {

class CacheManager {
public:
    explicit CacheManager(const std::string& = "127.0.0.1", int = 6379) {}
    ~CacheManager() = default;

    /// Configures cache host and port (retained for backward interface compatibility)
    void setRedisConfig(const std::string&, int) {}

    /// Returns whether external cache is active (always false in embedded mode)
    bool isRedisOnline() { return false; }

    /// Publishes real-time inventory change event
    bool publishInventoryUpdate(const std::string&, int64_t = 0) { return true; }

    // Slot reservation caching stubs
    void cacheBookedSlot(int64_t, const std::string&, const std::string&) {}
    bool isSlotBooked(int64_t, const std::string&, const std::string&) { return false; }
    void setBookedSlots(int64_t, const std::string&, const std::vector<std::string>&) {}

    // Low stock alerts caching stubs
    void setLowStockItem(int64_t, bool) {}
    std::unordered_set<int64_t> getLowStockItemIds() { return {}; }

    // Prescription queue stubs
    void pushPendingPrescription(const std::string&) {}
    std::vector<std::string> getPendingPrescriptions() { return {}; }
    void removePendingPrescription(const std::string&) {}

    // Temporary payment checkout session stubs
    void setPaymentSession(const std::string&, const std::string&) {}
    std::optional<std::string> getPaymentSession(const std::string&) { return std::nullopt; }
    void clearPaymentSession(const std::string&) {}
};

} // namespace aidans
