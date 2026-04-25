#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace hairgpu
{
    inline std::string Trim(std::string value)
    {
        const auto notSpace = [](unsigned char c) { return std::isspace(c) == 0; };
        value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
        value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
        return value;
    }

    inline std::string ToLower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    }

    inline std::vector<std::string> SplitCsvLine(const std::string& line, bool trimTokens = true)
    {
        std::vector<std::string> tokens;
        std::string current;
        bool inQuotes = false;
        for (size_t i = 0u; i < line.size(); ++i)
        {
            const char c = line[i];
            if (c == '"')
            {
                if (inQuotes && i + 1u < line.size() && line[i + 1u] == '"')
                {
                    current.push_back('"');
                    ++i;
                }
                else
                {
                    inQuotes = !inQuotes;
                }
            }
            else if (c == ',' && !inQuotes)
            {
                tokens.push_back(trimTokens ? Trim(current) : current);
                current.clear();
            }
            else
            {
                current.push_back(c);
            }
        }
        tokens.push_back(trimTokens ? Trim(current) : current);
        return tokens;
    }

    inline std::vector<std::string> SplitSimpleCsv(const std::string& line)
    {
        return SplitCsvLine(line, false);
    }

    inline std::string EscapeCsvToken(const std::string& token)
    {
        if (token.find_first_of(",\"\n\r") == std::string::npos)
        {
            return token;
        }
        std::string escaped = "\"";
        for (char c : token)
        {
            if (c == '"')
            {
                escaped += "\"\"";
            }
            else
            {
                escaped.push_back(c);
            }
        }
        escaped.push_back('"');
        return escaped;
    }
}
