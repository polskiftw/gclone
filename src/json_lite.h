#pragma once

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>

inline std::string JsonEscape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 16);
    for (unsigned char ch : value) {
        switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (ch < 0x20) {
                    char buffer[7]{};
                    sprintf_s(buffer, "\\u%04x", ch);
                    out += buffer;
                } else {
                    out.push_back(static_cast<char>(ch));
                }
        }
    }
    return out;
}

inline size_t JsonValueStart(const std::string& json, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return pos;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return pos;
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    return pos;
}

inline std::optional<std::string> JsonGetString(const std::string& json, const std::string& key) {
    size_t pos = JsonValueStart(json, key);
    if (pos == std::string::npos || pos >= json.size() || json[pos] != '"') return std::nullopt;
    ++pos;
    std::string out;
    while (pos < json.size()) {
        char ch = json[pos++];
        if (ch == '"') return out;
        if (ch != '\\') {
            out.push_back(ch);
            continue;
        }
        if (pos >= json.size()) return std::nullopt;
        const char esc = json[pos++];
        switch (esc) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            default: return std::nullopt;
        }
    }
    return std::nullopt;
}

inline bool JsonGetBool(const std::string& json, const std::string& key, bool fallback = false) {
    size_t pos = JsonValueStart(json, key);
    if (pos == std::string::npos) return fallback;
    if (json.compare(pos, 4, "true") == 0) return true;
    if (json.compare(pos, 5, "false") == 0) return false;
    return fallback;
}

inline double JsonGetDouble(const std::string& json, const std::string& key, double fallback) {
    size_t pos = JsonValueStart(json, key);
    if (pos == std::string::npos) return fallback;
    char* end = nullptr;
    const double value = std::strtod(json.c_str() + pos, &end);
    return end == json.c_str() + pos ? fallback : value;
}
