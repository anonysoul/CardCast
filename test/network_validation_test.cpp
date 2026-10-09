#include "../src/NetworkValidation.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main() {
    uint32_t address;
    assert(parseIPv4("192.168.1.42", address) && address == 0xc0a8012a);
    const char* invalid[] = {"", "1", "1.2.3", "1.2.3.", ".1.2.3", "1..2.3",
        "256.1.2.3", "1.2.3.4.5", "1.2.3.4 ", " 1.2.3.4", "-1.2.3.4", "1.2.3.a", "0000.1.2.3"};
    for (const char* value : invalid) assert(!parseIPv4(value, address));
    assert(!validateStaticNetwork("192.168.1.42", "255.255.255.0", "192.168.1.1", "", ""));
    assert(!validateStaticNetwork("10.0.0.2", "255.0.0.0", "10.0.0.1", "10.0.0.53", "10.0.0.54"));
    assert(!validateStaticNetwork("192.168.1.2", "255.255.255.252", "192.168.1.1", "", "192.168.1.1"));
    const char* badIPs[] = {"0.0.0.0", "127.0.0.1", "224.0.0.1", "255.255.255.255", "192.168.1.0", "192.168.1.255"};
    for (const char* ip : badIPs)
        assert(validateStaticNetwork(ip, "255.255.255.0", "192.168.1.1", "", ""));
    const char* badMasks[] = {"0.0.0.0", "255.0.255.0", "255.255.255.254", "255.255.255.255", "255.255.255"};
    for (const char* mask : badMasks)
        assert(validateStaticNetwork("192.168.1.42", mask, "192.168.1.1", "", ""));
    const char* badGateways[] = {"", "192.168.2.1", "192.168.1.42", "192.168.1.0", "192.168.1.255", "0.0.0.0"};
    for (const char* gateway : badGateways)
        assert(validateStaticNetwork("192.168.1.42", "255.255.255.0", gateway, "", ""));
    assert(strcmp(validateStaticNetwork("192.168.1.42", "255.255.255.0", "192.168.1.1", "bad", ""), "Invalid primary DNS") == 0);
    assert(strcmp(validateStaticNetwork("192.168.1.42", "255.255.255.0", "192.168.1.1", "", "0.0.0.0"), "Invalid secondary DNS") == 0);
    puts("Network validation tests passed");
}
