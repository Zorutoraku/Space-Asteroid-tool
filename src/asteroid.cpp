#include "asteroid.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>

static std::string opt_d(const std::optional<double>& v, const std::string& unit, int precision = 3) {
    if (!v.has_value()) return "N/A";
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(precision) << v.value();
    if (!unit.empty()) ss << " " << unit;
    return ss.str();
}

static std::string opt_s(const std::optional<std::string>& v) {
    return v.has_value() ? v.value() : "N/A";
}

static std::string opt_b(const std::optional<bool>& v) {
    if (!v.has_value()) return "N/A";
    return v.value() ? "⚠  YES" : "No";
}

static std::string score_bar(double score) {
    int filled = (int)(score / 5.0);
    std::string bar = "[";
    for (int i = 0; i < 20; i++)
        bar += (i < filled) ? "█" : "░";
    bar += "]";
    return bar;
}

void print_asteroid(const Asteroid& a) {
    std::cout << "\n╔══════════════════════════════════════════╗\n";
    std::cout << "  " << a.full_name << "\n";
    std::cout << "  ID: " << a.id << "  |  " << a.orbit_class << "\n";
    std::cout << "╚══════════════════════════════════════════╝\n";

    std::cout << "\n[ Physik ]\n"
              << "  Diameter:       " << opt_d(a.diameter_km, "km") << "\n"
              << "  Albedo:         " << opt_d(a.albedo, "", 4) << "\n"
              << "  Abs. Magnitude: " << opt_d(a.abs_magnitude, "") << "\n"
              << "  Rotation:       " << opt_d(a.rotation_period_h, "h") << "\n"
              << "  Spektraltyp:    " << opt_s(a.spectral_type) << "\n";

    std::cout << "\n[ Orbit ]\n"
              << "  Semi-major axis: " << opt_d(a.semi_major_axis, "AU") << "\n"
              << "  Exzentrizität:   " << opt_d(a.eccentricity, "", 4) << "\n"
              << "  Inklination:     " << opt_d(a.inclination, "°") << "\n"
              << "  MOID:            " << opt_d(a.moid, "AU") << "\n";

    if (a.surface_gravity.has_value() || a.escape_velocity.has_value()) {
        std::cout << "\n[ Berechnungen ]\n"
                  << "  Surface Gravity: " << opt_d(a.surface_gravity, "m/s²", 6) << "\n"
                  << "  Escape Velocity: " << opt_d(a.escape_velocity, "m/s", 3) << "\n";
    }

    if (a.mining_score_val.has_value()) {
        double s = a.mining_score_val.value();
        std::cout << "\n[ Mining Score ]\n"
                  << "  " << score_bar(s) << "  "
                  << std::fixed << std::setprecision(1) << s << " / 100\n";
    }

    if (a.miss_distance_km.has_value() || a.relative_velocity_kmh.has_value()) {
        std::cout << "\n[ Annäherung ]\n"
                  << "  Miss Distance: " << opt_d(a.miss_distance_km, "km", 0) << "\n"
                  << "  Velocity:      " << opt_d(a.relative_velocity_kmh, "km/h", 0) << "\n"
                  << "  Hazardous:     " << opt_b(a.is_hazardous) << "\n";
    }

    std::cout << "\n";
}
