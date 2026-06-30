#include "../inc/RtsDataFile.h"

#include <cctype>
#include <fstream>
#include <utility>

std::vector<std::string> TokenizeRtsDataLine(const std::string& line)
{
    std::vector<std::string> tokens;
    std::string token;
    bool inQuote = false;

    for (char c : line)
    {
        if (!inQuote && c == '#')
            break;

        if (c == '"')
        {
            if (inQuote)
            {
                tokens.push_back(token);
                token.clear();
            }
            inQuote = !inQuote;
            continue;
        }

        if (!inQuote && std::isspace(static_cast<unsigned char>(c)))
        {
            if (!token.empty())
            {
                tokens.push_back(token);
                token.clear();
            }
            continue;
        }

        token.push_back(c);
    }

    if (!token.empty())
        tokens.push_back(token);

    return tokens;
}

RtsDataLines ReadRtsDataLines(const std::string& path)
{
    std::ifstream file(path);
    RtsDataLines lines;
    if (!file.is_open())
        return lines;

    std::string line;
    while (std::getline(file, line))
    {
        auto tokens = TokenizeRtsDataLine(line);
        if (!tokens.empty())
            lines.push_back(std::move(tokens));
    }

    return lines;
}
