#ifndef RTS_DATA_FILE_H
#define RTS_DATA_FILE_H

#include <string>
#include <vector>

// Tokenized representation of a lightweight .rtsdata file.
using RtsDataLine = std::vector<std::string>;
using RtsDataLines = std::vector<RtsDataLine>;

// Splits a .rtsdata line into tokens, preserving quoted text and trimming comments.
std::vector<std::string> TokenizeRtsDataLine(const std::string& line);

// Reads non-empty tokenized .rtsdata lines from disk. Missing files return an empty list.
RtsDataLines ReadRtsDataLines(const std::string& path);

#endif
