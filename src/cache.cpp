#include "cache.hpp"
#include <algorithm>
#include <iostream>
#include <cstring>
#ifdef _WIN32
#ifndef _WINSOCK_DEPRECATED_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#define CLOSE_SOCK(s) closesocket(s)
#define MSG_NOSIGNAL 0
using socket_size_t = int;
#else
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#define CLOSE_SOCK(s) close(s)
using socket_size_t = ssize_t;
#endif

#ifdef _WIN32
static struct WSAInitializer {
    WSAInitializer() {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
    }
} g_wsaInit;
#endif

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
        CLOSE_SOCK(persistentSock_);
        persistentSock_ = -1;
    }
}

void CacheManager::setRedisConfig(const std::string& host, int port) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    std::lock_guard<std::mutex> sockLock(socketMutex_);
    redisHost_ = host;
    redisPort_ = port;
    if (persistentSock_ >= 0) {
        CLOSE_SOCK(persistentSock_);
        persistentSock_ = -1;
    }
}

bool CacheManager::sendRedisCommand(const std::string& cmd, std::string* replyOut) {
    std::lock_guard<std::mutex> sockLock(socketMutex_);

    auto connectSocket = [this]() -> bool {
        if (persistentSock_ >= 0) {
            CLOSE_SOCK(persistentSock_);
            persistentSock_ = -1;
        }

        int sock = static_cast<int>(socket(AF_INET, SOCK_STREAM, 0));
        if (sock < 0) return false;

#ifdef _WIN32
        DWORD tv = 400; // 400ms timeout
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));
#else
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 400000; // 400ms timeout
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof tv);
#endif

        struct sockaddr_in serv_addr;
        std::memset(&serv_addr, 0, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(redisPort_);

        if (inet_pton(AF_INET, redisHost_.c_str(), &serv_addr.sin_addr) <= 0) {
            struct hostent* he = gethostbyname(redisHost_.c_str());
            if (!he) {
                CLOSE_SOCK(sock);
                return false;
            }
            std::memcpy(&serv_addr.sin_addr, he->h_addr_list[0], he->h_length);
        }

#ifdef _WIN32
        u_long nonblock = 1;
        ioctlsocket(sock, FIONBIO, &nonblock);
#endif

        int connectRes = connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
        if (connectRes < 0) {
#ifdef _WIN32
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) {
                fd_set wFds, eFds;
                FD_ZERO(&wFds);
                FD_ZERO(&eFds);
                FD_SET(static_cast<SOCKET>(sock), &wFds);
                FD_SET(static_cast<SOCKET>(sock), &eFds);
                struct timeval tvConn;
                tvConn.tv_sec = 0;
                tvConn.tv_usec = 200000; // 200ms timeout
                int sel = select(0, nullptr, &wFds, &eFds, &tvConn);
                if (sel <= 0 || FD_ISSET(static_cast<SOCKET>(sock), &eFds)) {
                    CLOSE_SOCK(sock);
                    return false;
                }
                int soError = 0;
                int len = sizeof(soError);
                getsockopt(static_cast<SOCKET>(sock), SOL_SOCKET, SO_ERROR, (char*)&soError, &len);
                if (soError != 0) {
                    CLOSE_SOCK(sock);
                    return false;
                }
            } else {
                CLOSE_SOCK(sock);
                return false;
            }
#else
            CLOSE_SOCK(sock);
            return false;
#endif
        }

#ifdef _WIN32
        nonblock = 0;
        ioctlsocket(sock, FIONBIO, &nonblock);
#endif

        persistentSock_ = sock;
        return true;
    };

    if (persistentSock_ < 0) {
        if (!connectSocket()) return false;
    }

    // Try sending command on existing socket
    socket_size_t sent = send(persistentSock_, cmd.c_str(), static_cast<int>(cmd.size()), MSG_NOSIGNAL);
    if (sent != static_cast<socket_size_t>(cmd.size())) {
        // Reconnect once and retry
        if (!connectSocket()) return false;
        sent = send(persistentSock_, cmd.c_str(), static_cast<int>(cmd.size()), MSG_NOSIGNAL);
        if (sent != static_cast<socket_size_t>(cmd.size())) {
            CLOSE_SOCK(persistentSock_);
            persistentSock_ = -1;
            return false;
        }
    }

    if (replyOut) {
        char buf[256];
        socket_size_t n = recv(persistentSock_, buf, sizeof(buf) - 1, 0);
        if (n > 0) {
            buf[n] = '\0';
            *replyOut = std::string(buf, n);
        } else {
            CLOSE_SOCK(persistentSock_);
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
