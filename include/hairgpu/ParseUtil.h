#pragma once

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

#include "hairgpu/CsvUtil.h"

namespace hairgpu
{
    inline bool TryParseFloat(const std::string& text, float& outValue)
    {
        const std::string trimmed = Trim(text);
        if (trimmed.empty())
        {
            return false;
        }

        char* end = nullptr;
        errno = 0;
        const float value = std::strtof(trimmed.c_str(), &end);
        if (end == trimmed.c_str() || *end != '\0' || errno == ERANGE)
        {
            return false;
        }

        outValue = value;
        return true;
    }

    inline bool TryParseUInt(const std::string& text, uint32_t& outValue)
    {
        const std::string trimmed = Trim(text);
        if (trimmed.empty() || trimmed.front() == '-')
        {
            return false;
        }

        char* end = nullptr;
        errno = 0;
        const unsigned long value = std::strtoul(trimmed.c_str(), &end, 10);
        if (end == trimmed.c_str() || *end != '\0' || errno == ERANGE || value > std::numeric_limits<uint32_t>::max())
        {
            return false;
        }

        outValue = static_cast<uint32_t>(value);
        return true;
    }

    inline bool TryParseInt(const std::string& text, int32_t& outValue)
    {
        const std::string trimmed = Trim(text);
        if (trimmed.empty())
        {
            return false;
        }

        char* end = nullptr;
        errno = 0;
        const long value = std::strtol(trimmed.c_str(), &end, 10);
        if (end == trimmed.c_str() || *end != '\0' || errno == ERANGE ||
            value < std::numeric_limits<int32_t>::min() || value > std::numeric_limits<int32_t>::max())
        {
            return false;
        }

        outValue = static_cast<int32_t>(value);
        return true;
    }

    inline bool TryParseBool(const std::string& text, bool& outValue)
    {
        const std::string lower = ToLower(Trim(text));
        if (lower == "1" || lower == "true" || lower == "yes" || lower == "on")
        {
            outValue = true;
            return true;
        }
        if (lower == "0" || lower == "false" || lower == "no" || lower == "off")
        {
            outValue = false;
            return true;
        }
        return false;
    }

    inline float ParseFloatOr(const std::string& text, float fallback)
    {
        float value = fallback;
        return TryParseFloat(text, value) ? value : fallback;
    }

    inline int32_t ParseIntOr(const std::string& text, int32_t fallback)
    {
        int32_t value = fallback;
        return TryParseInt(text, value) ? value : fallback;
    }

    inline uint32_t ParseUIntOr(const std::string& text, uint32_t fallback)
    {
        uint32_t value = fallback;
        return TryParseUInt(text, value) ? value : fallback;
    }

    inline bool ParseBoolOr(const std::string& text, bool fallback)
    {
        bool value = fallback;
        return TryParseBool(text, value) ? value : fallback;
    }

    inline std::vector<float> ParseFloatArrayText(const std::string& text)
    {
        std::vector<float> values;
        std::string token;
        for (char c : text)
        {
            if (c == ',' || c == ';' || c == ' ' || c == '\t' || c == '\n' || c == '\r')
            {
                float value = 0.0f;
                if (!token.empty() && TryParseFloat(token, value))
                {
                    values.push_back(value);
                }
                token.clear();
            }
            else
            {
                token.push_back(c);
            }
        }
        float value = 0.0f;
        if (!token.empty() && TryParseFloat(token, value))
        {
            values.push_back(value);
        }
        return values;
    }
}
