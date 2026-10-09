#pragma once
#include "Arduino.h"
#include <vector>
constexpr int WIFI_AUTH_OPEN = 0;
struct FakeNetwork {
    String ssid;
    int rssi;
    int auth;
};
struct FakeWifi {
    std::vector<FakeNetwork> entries;
    int scans = 0, released = 0;
    std::vector<int> scanResults;
    int scanNetworks() {
        ++scans;
        if (!scanResults.empty()) {
            int result = scanResults.front();
            scanResults.erase(scanResults.begin());
            return result;
        }
        return int(entries.size());
    }
    String SSID(int i) const { return entries.at(i).ssid; }
    int RSSI(int i) const { return entries.at(i).rssi; }
    int encryptionType(int i) const { return entries.at(i).auth; }
    void scanDelete() { ++released; }
};
extern FakeWifi WiFi;
