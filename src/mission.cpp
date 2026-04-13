#include "mission.hpp"
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

using json = nlohmann::json;

static constexpr double GM_SUN   = 1.327124e20; // m³/s²
static constexpr double AU_M     = 1.496e11;     // 1 AU in Metern
static constexpr double PI       = 3.14159265358979;
static constexpr double EARTH_A  = 1.0;          // AU

// ── curl helper ─────────────────────────────────────────────────────────────
static size_t write_cb(char* ptr, size_t size, size_t nmemb, std::string* data) {
    data->append(ptr, size * nmemb);
    return size * nmemb;
}

static std::string fetch(const std::string& url) {
    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("curl init failed");

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) throw std::runtime_error(curl_easy_strerror(res));
    return response;
}

// ── Hohmann Transfer Berechnung ──────────────────────────────────────────────
static HohmannResult compute_hohmann(double asteroid_a_au) {
    HohmannResult h;
    h.transfer_a_au = (EARTH_A + asteroid_a_au) / 2.0;

    // Flugzeit = halbe Umlaufzeit der Transferellipse
    double a_m = h.transfer_a_au * AU_M;
    double period_s = 2.0 * PI * std::sqrt(std::pow(a_m, 3) / GM_SUN);
    h.flight_time_days = (period_s / 2.0) / 86400.0;

    // Delta-V Schätzung (Vis-Viva, vereinfacht)
    double v_earth = std::sqrt(GM_SUN / (EARTH_A * AU_M));
    double v_transfer_peri = std::sqrt(GM_SUN * (2.0 / (EARTH_A * AU_M) - 1.0 / a_m));
    h.delta_v_kms = std::abs(v_transfer_peri - v_earth) / 1000.0;

    // Synodische Periode
    double P_ast = std::pow(asteroid_a_au, 1.5); // Jahre (Kepler)
    double P_earth = 1.0;
    h.synodic_period_days = std::abs(1.0 / (1.0 / P_earth - 1.0 / P_ast)) * 365.25;

    return h;
}

// ── NHATS Parser ─────────────────────────────────────────────────────────────
static std::optional<NhatsTraj> parse_traj(const json& t) {
    if (t.is_null()) return std::nullopt;
    NhatsTraj traj;
    traj.launch_date   = t.value("launch", "N/A");
    traj.dv_total_kms  = std::stod(t["dv_total"].get<std::string>());
    traj.dur_total_days = t["dur_total"].get<int>();
    traj.dur_out_days  = t["dur_out"].get<int>();
    traj.dur_at_days   = t["dur_at"].get<int>();
    traj.dur_ret_days  = t["dur_ret"].get<int>();
    traj.c3            = std::stod(t["c3"].get<std::string>());
    return traj;
}

// ── Haupt-Fetch Funktion ─────────────────────────────────────────────────────
MissionData fetch_mission(const std::string& des, const std::string& full_name,
                          std::optional<double> semi_major_axis) {
    MissionData m;
    m.asteroid_des = des;
    m.full_name    = full_name;

    // ── NHATS ────────────────────────────────────────────────────────────────
    try {
        std::string url = "https://ssd-api.jpl.nasa.gov/nhats.api?des=" + des;
        json data = json::parse(fetch(url));

        if (!data.contains("code")) { // kein Fehler
            m.nhats_available = true;
            if (data.contains("min_dv_traj"))  m.min_dv  = parse_traj(data["min_dv_traj"]);
            if (data.contains("min_dur_traj")) m.min_dur = parse_traj(data["min_dur_traj"]);
        }
    } catch (...) {
        m.nhats_available = false;
    }

    // ── CAD ──────────────────────────────────────────────────────────────────
    try {
        std::string url = "https://ssd-api.jpl.nasa.gov/cad.api?des=" + des +
                          "&date-min=2025-01-01&date-max=2100-01-01&sort=date";
        json data = json::parse(fetch(url));

        if (data.contains("data")) {
            for (auto& entry : data["data"]) {
                CloseApproach ca;
                ca.date     = entry[3].get<std::string>();
                ca.dist_au  = std::stod(entry[4].get<std::string>());
                ca.v_rel_kms = std::stod(entry[7].get<std::string>());
                m.close_approaches.push_back(ca);
                if (m.close_approaches.size() >= 5) break; // max 5
            }
        }
    } catch (...) {}

    // ── Hohmann ──────────────────────────────────────────────────────────────
    if (semi_major_axis.has_value())
        m.hohmann = compute_hohmann(semi_major_axis.value());

    return m;
}

// ── Print ────────────────────────────────────────────────────────────────────
static std::string days_to_str(int days) {
    int y = days / 365;
    int d = days % 365;
    std::ostringstream ss;
    if (y > 0) ss << y << "y ";
    ss << d << "d";
    return ss.str();
}

void print_mission(const MissionData& m) {
    std::cout << "\n╔══════════════════════════════════════════╗\n";
    std::cout << "  MISSION ANALYSIS: " << m.full_name << "\n";
    std::cout << "╚══════════════════════════════════════════╝\n";

    // Hohmann
    if (m.hohmann.has_value()) {
        auto& h = m.hohmann.value();
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "\n[ Hohmann Transfer (vereinfacht) ]\n"
                  << "  Transfer Orbit a:   " << h.transfer_a_au << " AU\n"
                  << "  Flugzeit (einfach): " << (int)h.flight_time_days << " Tage\n"
                  << "  Delta-V (grob):     " << h.delta_v_kms << " km/s\n"
                  << "  Synodische Periode: " << (int)h.synodic_period_days << " Tage ("
                  << std::setprecision(1) << h.synodic_period_days / 365.25 << " Jahre)\n";
    }

    // NHATS
    if (m.nhats_available) {
        auto print_traj = [](const std::string& label, const NhatsTraj& t) {
            std::cout << "\n[ " << label << " ]\n"
                      << "  Launch:       " << t.launch_date << "\n"
                      << "  Delta-V:      " << t.dv_total_kms << " km/s\n"
                      << "  Gesamtdauer:  " << days_to_str(t.dur_total_days) << "\n"
                      << "  Hinflug:      " << t.dur_out_days << " Tage\n"
                      << "  Am Asteroiden:" << t.dur_at_days  << " Tage\n"
                      << "  Rückflug:     " << t.dur_ret_days << " Tage\n"
                      << "  C3 (Launch):  " << t.c3 << " km²/s²\n";
        };

        if (m.min_dv.has_value())  print_traj("Optimale Trajektorie (min ΔV)", m.min_dv.value());
        if (m.min_dur.has_value()) print_traj("Kürzeste Mission (min Dauer)",  m.min_dur.value());

    } else {
        std::cout << "\n[ NHATS ] Nicht verfügbar (kein NEA oder nicht berechnet)\n";
    }

    // Close Approaches
    if (!m.close_approaches.empty()) {
        std::cout << "\n[ Nächste Erdvorbeiflüge ]\n";
        std::cout << std::fixed << std::setprecision(4);
        for (auto& ca : m.close_approaches) {
            double dist_km = ca.dist_au * 1.496e8;
            std::cout << "  " << ca.date
                      << "  |  " << ca.dist_au << " AU"
                      << "  (" << (long long)dist_km << " km)"
                      << "  |  " << std::setprecision(2) << ca.v_rel_kms << " km/s\n";
        }
    } else {
        std::cout << "\n[ Vorbeiflüge ] Keine bis 2100 gefunden\n";
    }

    std::cout << "\n";
}
