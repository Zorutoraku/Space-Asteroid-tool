#pragma once
#include <string>
#include <vector>
#include <optional>

struct CloseApproach {
    std::string date;
    double dist_au;
    double v_rel_kms;
};

struct NhatsTraj {
    std::string launch_date;
    double dv_total_kms;
    int    dur_total_days;
    int    dur_out_days;
    int    dur_at_days;
    int    dur_ret_days;
    double c3;
};

struct HohmannResult {
    double transfer_a_au;       // Semi-major axis der Transferellipse
    double flight_time_days;    // Flugzeit (eine Richtung)
    double delta_v_kms;         // Grobe Delta-V Schätzung
    double synodic_period_days; // Wie oft kommt der Asteroid in Erdnähe
};

struct MissionData {
    std::string asteroid_des;   // Offizielle Designation (z.B. "101955")
    std::string full_name;

    // NHATS
    std::optional<NhatsTraj> min_dv;    // Optimale Trajektorie
    std::optional<NhatsTraj> min_dur;   // Kürzeste Mission
    bool nhats_available = false;

    // CAD
    std::vector<CloseApproach> close_approaches;

    // Hohmann (berechnet)
    std::optional<HohmannResult> hohmann;
};

void print_mission(const MissionData& m);
MissionData fetch_mission(const std::string& des, const std::string& full_name,
                          std::optional<double> semi_major_axis);
