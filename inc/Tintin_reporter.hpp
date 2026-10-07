#ifndef TINTIN_REPORTER_HPP
#define TINTIN_REPORTER_HPP

#include <fstream>
#include <string>

class Tintin_reporter {
public:
    Tintin_reporter() = default;
    ~Tintin_reporter() = default;
    Tintin_reporter(const Tintin_reporter&) = delete;
    Tintin_reporter& operator=(const Tintin_reporter&) = delete;

    void open();
    void info(const std::string& message);
    void input(const std::string& message);
    void error(const std::string& message);

private:
    std::ofstream _stream;
    void write(const std::string& level, const std::string& message);
};

#endif
