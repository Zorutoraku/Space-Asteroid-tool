#pragma once
#include "asteroid.hpp"
#include "calculator.hpp"
#include <string>
#include <vector>
#include <optional>
#include <sqlite3.h>

class Database {
public:
    explicit Database(const std::string& path = "../data/asteroids.db");
    ~Database();

    void save_asteroid(const Asteroid& a, const Resources& r);
    std::optional<Asteroid> get_asteroid(const std::string& id);
    std::vector<Asteroid> list_asteroids(const std::string& sort_by = "mining_score",
                                         bool scored_only = false);

    void add_watchlist(const std::string& id);
    void remove_watchlist(const std::string& id);
    std::vector<Asteroid> get_watchlist();

    bool exists(const std::string& id);

private:
    sqlite3* m_db = nullptr;
    void exec(const std::string& sql);
    void create_tables();
};
