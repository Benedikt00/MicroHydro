String serialize(const bool bits[32]) {
    uint32_t packed = 0;
    for (int i = 0; i < 32; ++i) {
        if (bits[i])
            packed |= (1u << i);
    }

    // Convert to "00101..." string, MSB first (bit 31 → char 0)
    String result(32, '0');
    for (int i = 0; i < 32; ++i)
        result[31 - i] = (packed >> i) & 1 ? '1' : '0';

    return result;
}

// Overload: serialize directly from a uint32_t
String serialize(uint32_t packed) {
    String result(32, '0');
    for (int i = 0; i < 32; ++i)
        result[31 - i] = (packed >> i) & 1 ? '1' : '0';
    return result;
}

// Deserialize: parse a 32-char binary string back into a bool array
// Throws std::invalid_argument if the string is malformed
void deserialize(const String& s, bool bits[32]) {
    if (s.size() != 32){
        Serial.println("Expected exactly 32 characters, got " + to_string(s.size()));
        return;
        }
    for (int i = 0; i < 32; ++i) {
        char c = s[31 - i];    // MSB is at index 0
        if (c != '0' && c != '1')
            Serial.println("Invalid character " + c + ' at position ' + to_string(31 - i));
        bits[i] = (c == '1');
    }
}

// Overload: deserialize into a uint32_t
uint32_t deserializeToInt(const String& s) {
    bool bits[32];
    deserialize(s, bits);
    uint32_t result = 0;
    for (int i = 0; i < 32; ++i)
        if (bits[i]) result |= (1u << i);
    return result;
}