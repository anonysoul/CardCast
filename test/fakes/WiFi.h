#pragma once
struct FakeWifi { int RSSI() const { return -50; } };
extern FakeWifi WiFi;
