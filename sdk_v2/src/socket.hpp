#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
namespace fpga::net {
#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket invalid = INVALID_SOCKET;
inline void initialize() {
  struct Runtime {
    Runtime() {
      WSADATA d{};
      if (WSAStartup(MAKEWORD(2, 2), &d))
        throw std::runtime_error("WSAStartup failed");
    }
    ~Runtime() { WSACleanup(); }
  };
  static Runtime runtime;
}
inline void close(Socket s) {
  if (s != invalid)
    closesocket(s);
}
#else
using Socket = int;
constexpr Socket invalid = -1;
inline void initialize() {}
inline void close(Socket s) {
  if (s != invalid)
    ::close(s);
}
#endif
inline sockaddr_in address(const std::string &ip, uint16_t port) {
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(port);
  if (inet_pton(AF_INET, ip.c_str(), &a.sin_addr) != 1)
    throw std::invalid_argument("Invalid IPv4 address: " + ip);
  return a;
}
inline Socket bind(const std::string &ip, uint16_t port,
                   size_t buffer = 128 * 1024 * 1024) {
  initialize();
  auto a = address(ip, port);
  Socket s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s == invalid)
    throw std::runtime_error("Cannot create UDP socket");
  int n = static_cast<int>(std::min<size_t>(buffer, 0x7fffffff));
  setsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char *>(&n),
             sizeof(n));
#ifdef _WIN32
  BOOL exclusive = TRUE;
  setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
             reinterpret_cast<const char *>(&exclusive), sizeof(exclusive));
  BOOL reset = FALSE;
  DWORD returned = 0;
  WSAIoctl(s, 0x9800000C, &reset, sizeof(reset), nullptr, 0, &returned, nullptr,
           nullptr);
#endif
  if (::bind(s, reinterpret_cast<sockaddr *>(&a), sizeof(a)) != 0) {
    close(s);
    throw std::runtime_error("Cannot bind UDP " + ip + ":" +
                             std::to_string(port));
  }
  return s;
}
inline uint16_t port(Socket s) {
  sockaddr_in a{};
#ifdef _WIN32
  int len = sizeof(a);
#else
  socklen_t len = sizeof(a);
#endif
  if (getsockname(s, reinterpret_cast<sockaddr *>(&a), &len))
    throw std::runtime_error("getsockname failed");
  return ntohs(a.sin_port);
}
inline void nonblocking(Socket s) {
#ifdef _WIN32
  u_long enabled = 1;
  if (ioctlsocket(s, FIONBIO, &enabled) != 0)
    throw std::runtime_error("Cannot enable nonblocking UDP");
#else
  int flags = fcntl(s, F_GETFL, 0);
  if (flags < 0 || fcntl(s, F_SETFL, flags | O_NONBLOCK) < 0)
    throw std::runtime_error("Cannot enable nonblocking UDP");
#endif
}
inline bool send(Socket s, const sockaddr_in &a,
                 const std::vector<uint8_t> &b) {
  return sendto(s, reinterpret_cast<const char *>(b.data()),
                static_cast<int>(b.size()), 0,
                reinterpret_cast<const sockaddr *>(&a),
                sizeof(a)) == static_cast<int>(b.size());
}
inline bool ready(Socket s, std::chrono::milliseconds wait) {
  fd_set f;
  FD_ZERO(&f);
  FD_SET(s, &f);
  timeval t{};
  t.tv_sec = static_cast<long>(wait.count() / 1000);
  t.tv_usec = static_cast<long>((wait.count() % 1000) * 1000);
  return select(static_cast<int>(s) + 1, &f, nullptr, nullptr, &t) > 0;
}
inline std::vector<uint8_t> receive(Socket s, sockaddr_in &peer) {
  // recvfrom initializes only the actual datagram. Avoid clearing 64 KiB and
  // allocating a maximum-size vector for every 1.4 KiB image fragment.
  std::array<uint8_t, 65536> b;
#ifdef _WIN32
  int len = sizeof(peer);
#else
  socklen_t len = sizeof(peer);
#endif
  int n = recvfrom(s, reinterpret_cast<char *>(b.data()),
                   static_cast<int>(b.size()), 0,
                   reinterpret_cast<sockaddr *>(&peer), &len);
  if (n <= 0)
    return {};
  return {b.data(), b.data() + n};
}
inline bool same_peer(const sockaddr_in &a, const sockaddr_in &b) {
  return a.sin_addr.s_addr == b.sin_addr.s_addr && a.sin_port == b.sin_port;
}
} // namespace fpga::net
