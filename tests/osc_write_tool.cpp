#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"
#include "osc/OscOutboundPacketStream.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using NativeSocket = SOCKET;
constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
using NativeSocket = int;
constexpr NativeSocket kInvalidSocket = -1;
#endif

namespace {

void CloseNativeSocket(NativeSocket& sock) {
#if defined(_WIN32)
    if (sock != INVALID_SOCKET) {
        closesocket(sock);
        sock = INVALID_SOCKET;
    }
#else
    if (sock >= 0) {
        ::close(sock);
        sock = -1;
    }
#endif
}

std::string LoadWingIpFromConfig() {
    std::vector<std::string> candidate_paths;
    if (const char* home = std::getenv("HOME")) {
        candidate_paths.push_back(std::string(home) + "/.wingconnector/config.json");
    }
    candidate_paths.push_back("install/config.json");

    for (const auto& path : candidate_paths) {
        std::ifstream in(path);
        if (!in.is_open()) {
            continue;
        }

        nlohmann::json config;
        in >> config;
        const std::string ip = config.value("wing_ip", std::string());
        if (!ip.empty()) {
            return ip;
        }
    }

    return "192.168.1.100";
}

bool CreateUdpSocket(const std::string& wing_ip, uint16_t wing_port, NativeSocket& sock_out, sockaddr_in& dest_out) {
#if defined(_WIN32)
    NativeSocket sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        return false;
    }
#else
    NativeSocket sock = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        return false;
    }
#endif

    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(wing_port);
    if (inet_pton(AF_INET, wing_ip.c_str(), &dest.sin_addr) != 1) {
        CloseNativeSocket(sock);
        return false;
    }

    sock_out = sock;
    dest_out = dest;
    return true;
}

bool SendStringMessage(NativeSocket sock, const sockaddr_in& dest, const std::string& address, const std::string& value) {
    char buffer[256];
    osc::OutboundPacketStream packet(buffer, sizeof(buffer));
    packet << osc::BeginMessage(address.c_str()) << value.c_str() << osc::EndMessage;
    auto bytes_sent = sendto(sock, packet.Data(), static_cast<int>(packet.Size()), 0,
                             reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));
#if defined(_WIN32)
    return bytes_sent != SOCKET_ERROR;
#else
    if (bytes_sent < 0) {
        std::cerr << "sendto(" << address << ") failed: " << std::strerror(errno) << "\n";
    }
    return bytes_sent >= 0;
#endif
}

bool SendIntMessage(NativeSocket sock, const sockaddr_in& dest, const std::string& address, int value) {
    char buffer[256];
    osc::OutboundPacketStream packet(buffer, sizeof(buffer));
    packet << osc::BeginMessage(address.c_str()) << static_cast<int32_t>(value) << osc::EndMessage;
    auto bytes_sent = sendto(sock, packet.Data(), static_cast<int>(packet.Size()), 0,
                             reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));
#if defined(_WIN32)
    return bytes_sent != SOCKET_ERROR;
#else
    if (bytes_sent < 0) {
        std::cerr << "sendto(" << address << ") failed: " << std::strerror(errno) << "\n";
    }
    return bytes_sent >= 0;
#endif
}

}  // namespace

int main(int argc, char** argv) {
#if defined(_WIN32)
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }
#endif

    if (argc < 4) {
        std::cerr << "Usage: osc_write_tool <string|int> <address> <value>\n";
        return 1;
    }

    const std::string type = argv[1];
    const std::string address = argv[2];
    const std::string value = argv[3];

    NativeSocket sock = kInvalidSocket;
    sockaddr_in dest{};
    if (!CreateUdpSocket(LoadWingIpFromConfig(), 2223, sock, dest)) {
        std::cerr << "Could not create UDP socket\n";
        return 1;
    }

    bool ok = false;
    if (type == "string") {
        ok = SendStringMessage(sock, dest, address, value);
    } else if (type == "int") {
        ok = SendIntMessage(sock, dest, address, std::atoi(value.c_str()));
    } else {
        std::cerr << "Unknown type: " << type << "\n";
        CloseNativeSocket(sock);
        return 1;
    }

    CloseNativeSocket(sock);
#if defined(_WIN32)
    WSACleanup();
#endif

    if (!ok) {
        std::cerr << "Send failed\n";
        return 1;
    }
    std::cout << "Sent " << address << "\n";
    return 0;
}
