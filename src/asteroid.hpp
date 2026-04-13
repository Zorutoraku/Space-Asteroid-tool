#pragma once
#include <string>
#include <optional>

struct Asteroid {
    // Identität
    std::string id;
    std::string full_name;
    std::string orbit_class;

    // Physikalische Parameter
    std::optional<double> diameter_km;
    std::optional<double> albedo;
    std::optional<double> abs_magnitude;
    std::optional<double> rotation_period_h;
    std::optional<std::string> spectral_type;

    // Orbitparameter
    std::optional<double> semi_major_axis;
    std::optional<double> eccentricity;
    std::optional<double> inclination;
    std::optional<double> moid;

    // NeoWs-spezifisch
    std::optional<double> miss_distance_km;
    std::optional<double> relative_velocity_kmh;
    std::optional<bool>   is_hazardous;

    // Berechnete Werte
    std::optional<double> surface_gravity;
    std::optional<double> escape_velocity;
    std::optional<double> mining_score_val;

    // NHATS Mission Delta-V (aus Batch)
    std::optional<double> delta_v_nhats;    // km/s (min_dv Trajektorie)
    std::optional<int>    mission_dur_days; // Gesamtdauer in Tagen
};

void print_asteroid(const Asteroid& a);
