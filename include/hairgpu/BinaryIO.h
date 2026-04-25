#pragma once

#include <cstdint>
#include <fstream>
#include <limits>
#include <vector>

namespace hairgpu
{
    template <typename T>
    inline bool WriteValue(std::ostream& out, const T& value)
    {
        out.write(reinterpret_cast<const char*>(&value), sizeof(T));
        return static_cast<bool>(out);
    }

    template <typename T>
    inline bool ReadValue(std::istream& in, T& value)
    {
        in.read(reinterpret_cast<char*>(&value), sizeof(T));
        return static_cast<bool>(in);
    }

    template <typename T>
    inline bool WriteVector(std::ostream& out, const std::vector<T>& values)
    {
        const uint64_t count = static_cast<uint64_t>(values.size());
        if (!WriteValue(out, count))
        {
            return false;
        }
        if (!values.empty())
        {
            out.write(reinterpret_cast<const char*>(values.data()),
                      static_cast<std::streamsize>(sizeof(T) * values.size()));
        }
        return static_cast<bool>(out);
    }

    template <typename T>
    inline bool ReadVector(std::istream& in, std::vector<T>& values, uint64_t maxCount = (1ull << 31))
    {
        uint64_t count = 0u;
        if (!ReadValue(in, count) || count > maxCount)
        {
            return false;
        }
        if (count > static_cast<uint64_t>(std::numeric_limits<size_t>::max() / sizeof(T)))
        {
            return false;
        }
        values.resize(static_cast<size_t>(count));
        if (!values.empty())
        {
            in.read(reinterpret_cast<char*>(values.data()),
                    static_cast<std::streamsize>(sizeof(T) * values.size()));
        }
        return static_cast<bool>(in);
    }

    template <typename T>
    inline bool WriteRawVector(std::ostream& out, const std::vector<T>& values)
    {
        if (!values.empty())
        {
            out.write(reinterpret_cast<const char*>(values.data()),
                      static_cast<std::streamsize>(sizeof(T) * values.size()));
        }
        return static_cast<bool>(out);
    }

    template <typename T>
    inline bool ReadRawVector(std::istream& in, std::vector<T>& values)
    {
        if (!values.empty())
        {
            in.read(reinterpret_cast<char*>(values.data()),
                    static_cast<std::streamsize>(sizeof(T) * values.size()));
        }
        return static_cast<bool>(in);
    }
}
