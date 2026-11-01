// SPDX-License-Identifier: GPL-3.0-only
#include "nat.h"
#include "net_socket.h"
#include <cctype>
#include <chrono>
#include <cstring>
#include <random>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using Handle = SOCKET;
#define NAT_INVALID INVALID_SOCKET
#define NAT_CLOSE closesocket
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
using Handle = int;
#define NAT_INVALID (-1)
#define NAT_CLOSE ::close
#endif

namespace openkai2::nat {
namespace {
bool wait(Handle handle, int timeout_ms) {
#ifdef _WIN32
    WSAPOLLFD entry{};
    entry.fd = handle;
    entry.events = POLLRDNORM;
    return WSAPoll(&entry, 1, timeout_ms) > 0;
#else
    pollfd entry{};
    entry.fd = handle;
    entry.events = POLLIN;
    return ::poll(&entry, 1, timeout_ms) > 0;
#endif
}

bool resolve(const std::string &host, uint16_t port, sockaddr_in &out) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo *found = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &found) != 0 || !found)
        return false;
    std::memcpy(&out, found->ai_addr, sizeof out);
    freeaddrinfo(found);
    return true;
}

struct Url {
    std::string host, path;
    uint16_t port = 80;
};

bool parse_url(const std::string &url, Url &out) {
    if (url.rfind("http://", 0) != 0)
        return false;
    const auto rest = url.substr(7);
    const auto slash = rest.find('/');
    const std::string authority = rest.substr(0, slash);
    out.path = slash == std::string::npos ? "/" : rest.substr(slash);
    const auto colon = authority.find(':');
    out.host = authority.substr(0, colon);
    if (colon != std::string::npos)
        out.port = uint16_t(std::strtoul(authority.c_str() + colon + 1, nullptr, 10));
    return !out.host.empty() && out.port;
}

std::string http(const std::string &url, const std::string &action, const std::string &body) {
    Url target;
    if (!parse_url(url, target))
        return {};
    std::string error;
    auto socket = net::Socket::connect(target.host, target.port, error);
    if (!socket.valid())
        return {};
    std::string request = (body.empty() ? "GET " : "POST ") + target.path + " HTTP/1.1\r\nHost: " + target.host + ":" +
                          std::to_string(target.port) + "\r\nConnection: close\r\n";
    if (!body.empty())
        request += "Content-Type: text/xml; charset=\"utf-8\"\r\nSOAPAction: \"" + action +
                   "\"\r\nContent-Length: " + std::to_string(body.size()) + "\r\n";
    request += "\r\n" + body;
    if (!socket.send_text(request))
        return {};
    std::string response;
    char buffer[2048];
    while (response.size() < 262144 && socket.readable(3000)) {
        const size_t before = response.size();
        if (!socket.receive_all(buffer, 1))
            break;
        response.push_back(buffer[0]);
        if (response.size() == before)
            break;
    }
    const auto split = response.find("\r\n\r\n");
    return split == std::string::npos ? std::string() : response.substr(split + 4);
}

std::string soap(const std::string &service, const std::string &action, const std::string &arguments) {
    return "<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
           "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:" +
           action + " xmlns:u=\"" + service + "\">" + arguments + "</u:" + action + "></s:Body></s:Envelope>";
}
}

std::string local_address() {
    net::ensure_network();
    const Handle handle = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (handle == NAT_INVALID)
        return {};
    sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_port = htons(53);
    inet_pton(AF_INET, "8.8.8.8", &target.sin_addr);
    std::string out;
    if (::connect(handle, reinterpret_cast<sockaddr *>(&target), sizeof target) == 0) {
        sockaddr_in self{};
#ifdef _WIN32
        int size = sizeof self;
#else
        socklen_t size = sizeof self;
#endif
        if (getsockname(handle, reinterpret_cast<sockaddr *>(&self), &size) == 0) {
            char text[INET_ADDRSTRLEN]{};
            inet_ntop(AF_INET, &self.sin_addr, text, sizeof text);
            out = text;
        }
    }
    NAT_CLOSE(handle);
    return out;
}

std::vector<uint8_t> stun_request(const uint8_t transaction[12]) {
    std::vector<uint8_t> out = {0x00, 0x01, 0x00, 0x00, 0x21, 0x12, 0xa4, 0x42};
    out.insert(out.end(), transaction, transaction + 12);
    return out;
}

std::optional<std::string> stun_mapped_address(const std::vector<uint8_t> &reply, const uint8_t transaction[12]) {
    if (reply.size() < 20 || reply[0] != 0x01 || reply[1] != 0x01 || std::memcmp(reply.data() + 8, transaction, 12) != 0)
        return std::nullopt;
    size_t at = 20;
    const size_t end = std::min(reply.size(), size_t(20 + (reply[2] << 8 | reply[3])));
    while (at + 4 <= end) {
        const unsigned type = unsigned(reply[at] << 8 | reply[at + 1]), length = unsigned(reply[at + 2] << 8 | reply[at + 3]);
        const size_t value = at + 4;
        if (value + length > end)
            break;
        if ((type == 0x0020 || type == 0x0001) && length >= 8 && reply[value + 1] == 0x01) {
            uint8_t ip[4];
            for (int i = 0; i < 4; ++i)
                ip[i] = type == 0x0020 ? uint8_t(reply[value + 4 + size_t(i)] ^ reply[4 + size_t(i)]) : reply[value + 4 + size_t(i)];
            return std::to_string(ip[0]) + "." + std::to_string(ip[1]) + "." + std::to_string(ip[2]) + "." +
                   std::to_string(ip[3]);
        }
        at = value + ((length + 3) & ~3u);
    }
    return std::nullopt;
}

std::optional<std::string> public_address(std::string &error) {
    net::ensure_network();
    std::random_device device;
    for (const auto &[host, port] : {std::pair<const char *, uint16_t>{"stun.l.google.com", 19302},
                                     {"stun.cloudflare.com", 3478}, {"stun1.l.google.com", 19302}}) {
        sockaddr_in target{};
        if (!resolve(host, port, target))
            continue;
        const Handle handle = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (handle == NAT_INVALID)
            continue;
        uint8_t transaction[12];
        for (auto &byte : transaction)
            byte = uint8_t(device());
        const auto request = stun_request(transaction);
        std::optional<std::string> found;
        for (int attempt = 0; attempt < 2 && !found; ++attempt) {
            ::sendto(handle, reinterpret_cast<const char *>(request.data()), int(request.size()), 0,
                     reinterpret_cast<sockaddr *>(&target), sizeof target);
            if (!wait(handle, 1500))
                continue;
            std::vector<uint8_t> reply(1024);
            const int got = int(::recv(handle, reinterpret_cast<char *>(reply.data()), int(reply.size()), 0));
            if (got > 0) {
                reply.resize(size_t(got));
                found = stun_mapped_address(reply, transaction);
            }
        }
        NAT_CLOSE(handle);
        if (found)
            return found;
    }
    error = "could not find your public address";
    return std::nullopt;
}

std::string find_tag(const std::string &xml, const std::string &tag, size_t from) {
    const auto open = xml.find("<" + tag + ">", from);
    if (open == std::string::npos)
        return {};
    const auto begin = open + tag.size() + 2;
    const auto close = xml.find("</" + tag + ">", begin);
    return close == std::string::npos ? std::string() : xml.substr(begin, close - begin);
}

std::optional<std::pair<std::string, std::string>> gateway_service(const std::string &description,
                                                                   const std::string &location) {
    for (const char *wanted : {"urn:schemas-upnp-org:service:WANIPConnection:2", "urn:schemas-upnp-org:service:WANIPConnection:1",
                               "urn:schemas-upnp-org:service:WANPPPConnection:1"}) {
        const auto at = description.find(std::string("<serviceType>") + wanted + "</serviceType>");
        if (at == std::string::npos)
            continue;
        std::string control = find_tag(description, "controlURL", at);
        if (control.empty())
            continue;
        if (control.rfind("http://", 0) != 0) {
            Url base;
            if (!parse_url(location, base))
                return std::nullopt;
            control = "http://" + base.host + ":" + std::to_string(base.port) + (control[0] == '/' ? "" : "/") + control;
        }
        return std::make_pair(control, std::string(wanted));
    }
    return std::nullopt;
}

PortMapping::~PortMapping() { close(); }

bool PortMapping::open(uint16_t port, std::string &external, std::string &error) {
    close();
    net::ensure_network();
    const std::string local = local_address();
    if (local.empty()) {
        error = "no network";
        return false;
    }
    const Handle handle = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (handle == NAT_INVALID) {
        error = "no network";
        return false;
    }
    sockaddr_in group{};
    group.sin_family = AF_INET;
    group.sin_port = htons(1900);
    inet_pton(AF_INET, "239.255.255.250", &group.sin_addr);
    const std::string search = "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\n"
                               "ST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n\r\n";
    std::vector<std::string> locations;
    for (int attempt = 0; attempt < 2 && locations.empty(); ++attempt) {
        ::sendto(handle, search.data(), int(search.size()), 0, reinterpret_cast<sockaddr *>(&group), sizeof group);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2000);
        while (std::chrono::steady_clock::now() < deadline && wait(handle, 300)) {
            char buffer[2048];
            const int got = int(::recv(handle, buffer, sizeof buffer - 1, 0));
            if (got <= 0)
                break;
            std::string reply(buffer, size_t(got)), lower = reply;
            for (char &c : lower)
                c = char(std::tolower(static_cast<unsigned char>(c)));
            const auto at = lower.find("location:");
            if (at == std::string::npos)
                continue;
            auto end = reply.find("\r\n", at);
            std::string location = reply.substr(at + 9, end == std::string::npos ? std::string::npos : end - at - 9);
            while (!location.empty() && location.front() == ' ')
                location.erase(location.begin());
            locations.push_back(location);
        }
    }
    NAT_CLOSE(handle);
    for (const auto &location : locations) {
        const auto service = gateway_service(http(location, "", ""), location);
        if (!service)
            continue;
        const std::string arguments =
            "<NewRemoteHost></NewRemoteHost><NewExternalPort>" + std::to_string(port) +
            "</NewExternalPort><NewProtocol>TCP</NewProtocol><NewInternalPort>" + std::to_string(port) +
            "</NewInternalPort><NewInternalClient>" + local +
            "</NewInternalClient><NewEnabled>1</NewEnabled><NewPortMappingDescription>OpenKai2</NewPortMappingDescription>"
            "<NewLeaseDuration>7200</NewLeaseDuration>";
        const auto added = http(service->first, service->second + "#AddPortMapping",
                                soap(service->second, "AddPortMapping", arguments));
        if (added.find("AddPortMappingResponse") == std::string::npos)
            continue;
        control_ = service->first;
        service_ = service->second;
        port_ = port;
        const auto address = http(control_, service_ + "#GetExternalIPAddress", soap(service_, "GetExternalIPAddress", ""));
        external = find_tag(address, "NewExternalIPAddress");
        return true;
    }
    error = locations.empty() ? "no UPnP router found" : "the router did not open the port";
    return false;
}

void PortMapping::close() {
    if (control_.empty())
        return;
    http(control_, service_ + "#DeletePortMapping",
         soap(service_, "DeletePortMapping",
              "<NewRemoteHost></NewRemoteHost><NewExternalPort>" + std::to_string(port_) +
                  "</NewExternalPort><NewProtocol>TCP</NewProtocol>"));
    control_.clear();
}

}
