#ifndef CORE_LOG_H
#define CORE_LOG_H

#include <sstream>
#include <string>
#include <utility>

// Thread-safe logger writing tagged messages to stdout and logs/rts.log.
class Log
{
public:
    template <typename... Args>
    static void Msg(std::string tag, Args&&... args)
    {
        std::ostringstream line;
        line << tag << " | ";
        (line << ... << std::forward<Args>(args));
        WriteLine(line.str());
    }

    static std::string CurrentTime();

private:
    static void WriteLine(std::string line);
};

#endif
