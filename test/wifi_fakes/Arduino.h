#pragma once
#include <cstdint>
#include <string>
class String : public std::string {
public:
    using std::string::string;
    using std::string::operator=;
    bool isEmpty() const { return empty(); }
    void remove(unsigned int index) { erase(index); }
};
void delay(uint32_t milliseconds);
