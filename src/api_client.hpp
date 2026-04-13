#pragma once
#include "asteroid.hpp"
#include <string>
#include <vector>

class ApiClient {
public:
    explicit ApiClient(const std::string& api_key);

    std::vector<Asteroid> get_feed(const std::string& date);
    Asteroid get_by_id(const std::string& asteroid_id);
    Asteroid get_by_name(const std::string& name);

private:
    std::string m_api_key;
    std::string fetch(const std::string& url);
};
