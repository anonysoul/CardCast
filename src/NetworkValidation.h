#pragma once

#include <stdint.h>

// Parse dotted-decimal IPv4 strictly, without accepting shortened addresses.
inline bool parseIPv4(const char* text, uint32_t& address) {
    address = 0;
    for (int part = 0; part < 4; ++part) {
        unsigned value = 0;
        int digits = 0;
        while (*text >= '0' && *text <= '9') {
            value = value * 10 + (*text++ - '0');
            if (++digits > 3 || value > 255) return false;
        }
        if (!digits) return false;
        address = (address << 8) | value;
        if (part < 3) {
            if (*text++ != '.') return false;
        } else if (*text != '\0') {
            return false;
        }
    }
    return true;
}

inline bool isUnicastIPv4(uint32_t address) {
    const uint32_t first = address >> 24;
    return first != 0 && first != 127 && first < 224;
}

inline const char* validateStaticNetwork(const char* ipText, const char* maskText,
                                       const char* gatewayText, const char* dns1Text,
                                       const char* dns2Text) {
    uint32_t ip, mask, gateway, dns;
    if (!parseIPv4(ipText, ip) || !isUnicastIPv4(ip)) return "Invalid IP address";
    if (!parseIPv4(maskText, mask)) return "Invalid subnet mask";
    const uint32_t hostMask = ~mask;
    // Require a contiguous /1 through /30 subnet for a normal Wi-Fi LAN.
    if (mask == 0 || hostMask < 3 || (hostMask & (hostMask + 1)) != 0)
        return "Invalid subnet mask";
    if ((ip & hostMask) == 0 || (ip & hostMask) == hostMask)
        return "IP is network/broadcast";
    if (!parseIPv4(gatewayText, gateway) || !isUnicastIPv4(gateway))
        return "Invalid gateway";
    if ((ip & mask) != (gateway & mask)) return "Gateway outside subnet";
    if (gateway == ip || (gateway & hostMask) == 0 || (gateway & hostMask) == hostMask)
        return "Invalid gateway host";
    if (*dns1Text && (!parseIPv4(dns1Text, dns) || !isUnicastIPv4(dns)))
        return "Invalid primary DNS";
    if (*dns2Text && (!parseIPv4(dns2Text, dns) || !isUnicastIPv4(dns)))
        return "Invalid secondary DNS";
    return nullptr;
}
