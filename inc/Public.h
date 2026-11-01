#pragma once

#include <string>

class Buffer : public std::string
{
public:
    Buffer() = default;
    Buffer(size_t size) : std::string(size, '\0')
    {
    }
    Buffer(std::string const& value) : std::string(value)
    {
    }
    Buffer(char const* value) : std::string(value ? value : "")
    {
    }
    Buffer(char const* value, size_t size) : std::string(value, size)
    {
    }
    Buffer(char const* begin, char const* end) : std::string(begin, end)
    {
    }

    operator void*()
    {
        return data();
    }
    operator char*()
    {
        return data();
    }
    operator unsigned char*()
    {
        return reinterpret_cast<unsigned char*>(data());
    }
    operator char*() const
    {
        return const_cast<char*>(data());
    }
    operator char const*() const
    {
        return c_str();
    }
    operator void const*() const
    {
        return data();
    }
};
