#include "cache.hpp"
#include <algorithm>
#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

namespace aidans {

static std::string makeSlotKey(int64_t doctorId, const std::string& date) {
    return std::to_string(doctorId) + ":" + date;
}

CacheManager::CacheManager(const std::string& redisHost, int redisPort)
    : redisHost_(redisHost), redisPort_(redisPort) {
}

CacheManager::~CacheManager() {
    std::lock_guard<std::mutex> sockLock(socketMutex_);
    if (persistentSock_ >= 0) {
        close(persistentSock_);
        persistentSock_ = -1;
    }
}

void CacheManager::setRedisConfig(const std::string& host, int port) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    std::lock_guard<std::mutex> sockLock(socketMutex_);
    redisHost_ = host;
    redisPort_ = port;
    if (persistentSock_ >= 0) {
        close(persistentSock_);
        persistentSock_ = -1;
    }
}

bool CacheManager::sendRedisCommand(const std::string& cmd, std::string* replyOut) {
    std::lock_guard<std::mutex> sockLock(socketMutex_);

    auto connectSocket = [this]() -> bool {
        if (persistentSock_ >= 0) {
            close(persistentSock_);
            persistentSock_ = -1;
        }

        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) return false;

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 400000; // 400ms timeout
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof tv);

        struct sockaddr_in serv_addr;
        std::memset(&serv_addr, 0, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(redisPort_);

        if (inet_pton(AF_INET, redisHost_.c_str(), &serv_addr.sin_addr) <= 0) {
            struct hostent* he = gethostbyname(redisHost_.c_str());
            if (!he) {
                close(sock);
                return false;
            }
            std::memcpy(&serv_addr.sin_addr, he->h_addr_list[0], he->h_length);
        }

        if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
            close(sock);
            return false;
        }

        persistentSock_ = sock;
        return true;
    };

    if (persistentSock_ < 0) {
        if (!connectSocket()) return false;
    }

    // Try sending command on existing socket
    ssize_t sent = send(persistentSock_, cmd.c_str(), cmd.size(), MSG_NOSIGNAL);
    if (sent != static_cast<ssize_t>(cmd.size())) {
        // Reconnect once and retry
        if (!connectSocket()) return false;
        sent = send(persistentSock_, cmd.c_str(), cmd.size(), MSG_NOSIGNAL);
        if (sent != static_cast<ssize_t>(cmd.size())) {
            close(persistentSock_);
            persistentSock_ = -1;
            return false;
        }
    }

    if (replyOut) {
        char buf[256];
        ssize_t n = recv(persistentSock_, buf, sizeof(buf) - 1, 0);
        if (n > 0) {
            buf[n] = '\0';
            *replyOut = std::string(buf, n);
        } else {
            close(persistentSock_);
            persistentSock_ = -1;
            return false;
        }
    }
    return true;
}

bool CacheManager::isRedisOnline() {
    std::string reply;
    bool ok = sendRedisCommand("PING\r\n", &reply);
    return ok && (reply.find("PONG") != std::string::npos);
}

bool CacheManager::publishInventoryUpdate(const std::string& action, int64_t itemId) {
    std::string payload = "{\"event\":\"inventory_updated\",\"action\":\"" + action + "\",\"item_id\":" + std::to_string(itemId) + "}";
    std::string cmd = "PUBLISH inventory_updates " + payload + "\r\n";
    std::string reply;
    bool ok = sendRedisCommand(cmd, &reply);
    if (ok) {
        std::cout << "[Redis Pub/Sub] Published to 'inventory_updates': " << action << " (item " << itemId << ")" << std::endl;
    }
    return ok;
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
