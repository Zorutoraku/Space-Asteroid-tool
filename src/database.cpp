#include "database.hpp"
#include <stdexcept>
#include <filesystem>
#include <ctime>

#define BIND_TEXT(stmt, idx, val) \
    sqlite3_bind_text(stmt, idx, (val).c_str(), -1, SQLITE_TRANSIENT)
#define BIND_REAL(stmt, idx, opt) \
    ((opt).has_value() ? sqlite3_bind_double(stmt, idx, (opt).value()) : sqlite3_bind_null(stmt, idx))
#define BIND_INT(stmt, idx, opt) \
    ((opt).has_value() ? sqlite3_bind_int(stmt, idx, (opt).value()) : sqlite3_bind_null(stmt, idx))

static std::string now_iso() {
    std::time_t t = std::time(nullptr);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", std::localtime(&t));
    return buf;
}

static std::optional<double> col_real(sqlite3_stmt* s, int i) {
    if (sqlite3_column_type(s, i) == SQLITE_NULL) return std::nullopt;
    return sqlite3_column_double(s, i);
}

static std::optional<int> col_int_opt(sqlite3_stmt* s, int i) {
    if (sqlite3_column_type(s, i) == SQLITE_NULL) return std::nullopt;
    return sqlite3_column_int(s, i);
}

static std::optional<std::string> col_text_opt(sqlite3_stmt* s, int i) {
    if (sqlite3_column_type(s, i) == SQLITE_NULL) return std::nullopt;
    return std::string(reinterpret_cast<const char*>(sqlite3_column_text(s, i)));
}

static std::string col_text(sqlite3_stmt* s, int i) {
    return col_text_opt(s, i).value_or("");
}

Database::Database(const std::string& path) {
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    if (sqlite3_open(path.c_str(), &m_db) != SQLITE_OK)
        throw std::runtime_error("Cannot open DB: " + std::string(sqlite3_errmsg(m_db)));
    create_tables();
}

Database::~Database() {
    if (m_db) sqlite3_close(m_db);
}

void Database::exec(const std::string& sql) {
    char* err = nullptr;
    if (sqlite3_exec(m_db, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err;
        sqlite3_free(err);
        throw std::runtime_error("SQL error: " + msg);
    }
}

void Database::create_tables() {
    exec(R"(
        CREATE TABLE IF NOT EXISTS asteroids (
            id                  TEXT PRIMARY KEY,
            full_name           TEXT NOT NULL,
            orbit_class         TEXT,
            diameter_km         REAL,
            albedo              REAL,
            abs_magnitude       REAL,
            rotation_period_h   REAL,
            spectral_type       TEXT,
            semi_major_axis     REAL,
            eccentricity        REAL,
            inclination         REAL,
            surface_gravity     REAL,
            escape_velocity     REAL,
            mining_score        REAL,
            mass_kg             REAL,
            water_kg            REAL,
            metal_kg            REAL,
            pgm_kg              REAL,
            estimated_value_usd REAL,
            delta_v_nhats       REAL,
            mission_dur_days    INTEGER,
            on_watchlist        INTEGER DEFAULT 0,
            last_updated        TEXT
        );
    )");

    // Migration: Spalten hinzufügen falls alte DB ohne diese Felder
    auto try_alter = [&](const std::string& sql) {
        char* err = nullptr;
        sqlite3_exec(m_db, sql.c_str(), nullptr, nullptr, &err);
        if (err) sqlite3_free(err); // Fehler ignorieren (Spalte existiert bereits)
    };
    try_alter("ALTER TABLE asteroids ADD COLUMN delta_v_nhats REAL;");
    try_alter("ALTER TABLE asteroids ADD COLUMN mission_dur_days INTEGER;");
    // Fehler werden ignoriert wenn Spalten schon existieren
}

void Database::save_asteroid(const Asteroid& a, const Resources& r) {
    const char* sql = R"(
        INSERT OR REPLACE INTO asteroids (
            id, full_name, orbit_class,
            diameter_km, albedo, abs_magnitude, rotation_period_h, spectral_type,
            semi_major_axis, eccentricity, inclination,
            surface_gravity, escape_velocity, mining_score,
            mass_kg, water_kg, metal_kg, pgm_kg, estimated_value_usd,
            delta_v_nhats, mission_dur_days,
            on_watchlist, last_updated
        ) VALUES (
            ?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,
            COALESCE((SELECT on_watchlist FROM asteroids WHERE id=?), 0),
            ?
        );
    )";

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);

    int i = 1;
    BIND_TEXT(stmt, i++, a.id);
    BIND_TEXT(stmt, i++, a.full_name);
    BIND_TEXT(stmt, i++, a.orbit_class);
    BIND_REAL(stmt, i++, a.diameter_km);
    BIND_REAL(stmt, i++, a.albedo);
    BIND_REAL(stmt, i++, a.abs_magnitude);
    BIND_REAL(stmt, i++, a.rotation_period_h);
    if (a.spectral_type.has_value())
        BIND_TEXT(stmt, i++, a.spectral_type.value());
    else
        sqlite3_bind_null(stmt, i++);
    BIND_REAL(stmt, i++, a.semi_major_axis);
    BIND_REAL(stmt, i++, a.eccentricity);
    BIND_REAL(stmt, i++, a.inclination);
    BIND_REAL(stmt, i++, a.surface_gravity);
    BIND_REAL(stmt, i++, a.escape_velocity);
    BIND_REAL(stmt, i++, a.mining_score_val);
    sqlite3_bind_double(stmt, i++, r.mass_kg);
    sqlite3_bind_double(stmt, i++, r.water_kg);
    sqlite3_bind_double(stmt, i++, r.metal_kg);
    sqlite3_bind_double(stmt, i++, r.pgm_kg);
    sqlite3_bind_double(stmt, i++, r.estimated_value_usd);
    BIND_REAL(stmt, i++, a.delta_v_nhats);
    BIND_INT (stmt, i++, a.mission_dur_days);
    BIND_TEXT(stmt, i++, a.id);
    std::string ts = now_iso();
    BIND_TEXT(stmt, i++, ts);

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

static const char* SELECT_COLS =
    "id, full_name, orbit_class, diameter_km, albedo, abs_magnitude, "
    "rotation_period_h, spectral_type, semi_major_axis, eccentricity, inclination, "
    "surface_gravity, escape_velocity, mining_score, estimated_value_usd, "
    "delta_v_nhats, mission_dur_days";

static Asteroid row_to_asteroid(sqlite3_stmt* s) {
    Asteroid a;
    a.id                = col_text(s, 0);
    a.full_name         = col_text(s, 1);
    a.orbit_class       = col_text(s, 2);
    a.diameter_km       = col_real(s, 3);
    a.albedo            = col_real(s, 4);
    a.abs_magnitude     = col_real(s, 5);
    a.rotation_period_h = col_real(s, 6);
    a.spectral_type     = col_text_opt(s, 7);
    a.semi_major_axis   = col_real(s, 8);
    a.eccentricity      = col_real(s, 9);
    a.inclination       = col_real(s, 10);
    a.surface_gravity   = col_real(s, 11);
    a.escape_velocity   = col_real(s, 12);
    a.mining_score_val  = col_real(s, 13);
    // estimated_value_usd — in miss_distance_km als Träger
    if (sqlite3_column_type(s, 14) != SQLITE_NULL)
        a.miss_distance_km = sqlite3_column_double(s, 14);
    a.delta_v_nhats     = col_real(s, 15);
    a.mission_dur_days  = col_int_opt(s, 16);
    return a;
}

std::optional<Asteroid> Database::get_asteroid(const std::string& id) {
    std::string sql = std::string("SELECT ") + SELECT_COLS +
                      " FROM asteroids WHERE id=?;";
    sqlite3_stmt* s;
    sqlite3_prepare_v2(m_db, sql.c_str(), -1, &s, nullptr);
    BIND_TEXT(s, 1, id);

    std::optional<Asteroid> result;
    if (sqlite3_step(s) == SQLITE_ROW)
        result = row_to_asteroid(s);
    sqlite3_finalize(s);
    return result;
}

std::vector<Asteroid> Database::list_asteroids(const std::string& sort_by, bool scored_only) {
    std::string col = "mining_score";
    if (sort_by == "name")     col = "full_name";
    if (sort_by == "diameter") col = "diameter_km";
    if (sort_by == "value")    col = "estimated_value_usd";
    if (sort_by == "dv")       col = "delta_v_nhats";

    std::string where = scored_only ? "WHERE mining_score > 0 " : "";
    std::string order = (sort_by == "dv")
        ? "ORDER BY " + col + " ASC NULLS LAST"   // Delta-V: niedrig zuerst
        : "ORDER BY " + col + " DESC NULLS LAST";

    std::string sql = std::string("SELECT ") + SELECT_COLS +
                      " FROM asteroids " + where + order + ";";

    sqlite3_stmt* s;
    sqlite3_prepare_v2(m_db, sql.c_str(), -1, &s, nullptr);

    std::vector<Asteroid> result;
    while (sqlite3_step(s) == SQLITE_ROW)
        result.push_back(row_to_asteroid(s));
    sqlite3_finalize(s);
    return result;
}

void Database::add_watchlist(const std::string& id) {
    sqlite3_stmt* s;
    sqlite3_prepare_v2(m_db, "UPDATE asteroids SET on_watchlist=1 WHERE id=?;", -1, &s, nullptr);
    BIND_TEXT(s, 1, id);
    sqlite3_step(s);
    sqlite3_finalize(s);
}

void Database::remove_watchlist(const std::string& id) {
    sqlite3_stmt* s;
    sqlite3_prepare_v2(m_db, "UPDATE asteroids SET on_watchlist=0 WHERE id=?;", -1, &s, nullptr);
    BIND_TEXT(s, 1, id);
    sqlite3_step(s);
    sqlite3_finalize(s);
}

std::vector<Asteroid> Database::get_watchlist() {
    std::string sql = std::string("SELECT ") + SELECT_COLS +
                      " FROM asteroids WHERE on_watchlist=1 ORDER BY mining_score DESC NULLS LAST;";
    sqlite3_stmt* s;
    sqlite3_prepare_v2(m_db, sql.c_str(), -1, &s, nullptr);

    std::vector<Asteroid> result;
    while (sqlite3_step(s) == SQLITE_ROW)
        result.push_back(row_to_asteroid(s));
    sqlite3_finalize(s);
    return result;
}

bool Database::exists(const std::string& id) {
    sqlite3_stmt* s;
    sqlite3_prepare_v2(m_db, "SELECT 1 FROM asteroids WHERE id=? LIMIT 1;", -1, &s, nullptr);
    BIND_TEXT(s, 1, id);
    bool found = (sqlite3_step(s) == SQLITE_ROW);
    sqlite3_finalize(s);
    return found;
}
