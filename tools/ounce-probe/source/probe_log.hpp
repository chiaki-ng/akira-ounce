#pragma once

#include <cstdio>
#include <format>
#include <memory>
#include <string>

class ProbeLog
{
public:
    explicit ProbeLog(const char* path) : m_file(std::fopen(path, "w"), &std::fclose) {}

    template <class... Args>
    void line(std::format_string<Args...> format, Args&&... args)
    {
        const std::string text = std::format(format, std::forward<Args>(args)...);
        std::printf("%s\n", text.c_str());
        if (m_file)
        {
            std::fprintf(m_file.get(), "%s\n", text.c_str());
            std::fflush(m_file.get());
        }
    }

private:
    std::unique_ptr<std::FILE, decltype(&std::fclose)> m_file;
};
