#include "calculator.hpp"
#include <cmath>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
#include <algorithm>

static constexpr double G  = 6.674e-11;
static constexpr double PI = 3.14159265358979;

double estimate_density(const std::optional<std::string>& spec) {
    if (!spec.has_value()) return 2000.0;
    char c = std::toupper(spec.value()[0]);
    switch (c) {
        case 'C': return 1380.0;
        case 'S': return 2710.0;
        case 'M': return 5000.0;
        case 'V': return 2960.0;
        case 'B': return 1380.0;
        case 'D': return 1500.0;
        case 'X': return 3500.0;
        default:  return 2000.0;
    }
}

static double diameter_from_H(double H, double albedo = 0.15) {
    return 1329.0 / std::sqrt(albedo) * std::pow(10.0, -H / 5.0);
}

// Orbit-Klasse → Erreichbarkeits-Malus (0 = kein Malus, negativ = Strafe)
static double orbit_class_modifier(const std::string& orbit_class) {
    std::string oc = orbit_class;
    std::transform(oc.begin(), oc.end(), oc.begin(), ::tolower);

    if (oc.find("apollo")  != std::string::npos) return +5.0;  // Bester NEA-Typ
    if (oc.find("aten")    != std::string::npos) return +5.0;
    if (oc.find("amor")    != std::string::npos) return +3.0;
    if (oc.find("ares")    != std::string::npos) return +3.0;
    if (oc.find("atira")   != std::string::npos) return +2.0;
    if (oc.find("main-belt")!= std::string::npos) return -20.0; // Hauptgürtel
    if (oc.find("trojan")  != std::string::npos) return -30.0;  // Jupiter Trojaner
    if (oc.find("transnep") != std::string::npos) return -40.0; // TNO
    if (oc.find("centaur")  != std::string::npos) return -35.0;
    if (oc.find("scattered")!= std::string::npos) return -40.0;
    return 0.0; // Unbekannt
}

void compute(Asteroid& a) {
    if (!a.diameter_km.has_value() && a.abs_magnitude.has_value()) {
        double albedo = a.albedo.value_or(0.15);
        a.diameter_km = diameter_from_H(a.abs_magnitude.value(), albedo) / 1000.0;
    }

    if (!a.diameter_km.has_value()) return;

    double r_m    = (a.diameter_km.value() * 1000.0) / 2.0;
    double volume = (4.0 / 3.0) * PI * std::pow(r_m, 3);
    double density = estimate_density(a.spectral_type);
    double mass   = volume * density;

    a.surface_gravity  = (G * mass) / std::pow(r_m, 2);
    a.escape_velocity  = std::sqrt(2.0 * G * mass / r_m);
    a.mining_score_val = mining_score(a);
}

double mining_score(const Asteroid& a) {
    double score = 0.0;
    bool has_data = false;

    // ── Größe (0–20) ──────────────────────────────────────────────────────────
    // Gedeckelt bei 2 km — größer = kein weiterer Bonus (nicht erreichbar/sinnvoll)
    if (a.diameter_km.has_value()) {
        has_data = true;
        double d = std::min(a.diameter_km.value(), 2.0); // Cap bei 2 km
        if (d > 0)
            score += std::min(20.0, std::max(0.0, 6.67 * std::log10(d * 100.0)));
    }

    // ── Spektraltyp (0–25) ────────────────────────────────────────────────────
    if (a.spectral_type.has_value()) {
        has_data = true;
        char c = std::toupper(a.spectral_type.value()[0]);
        if      (c == 'M') score += 25.0;
        else if (c == 'X') score += 22.0;
        else if (c == 'C' || c == 'B') score += 20.0;
        else if (c == 'S') score += 18.0;
        else if (c == 'V') score += 12.0;
        else               score +=  8.0;
    }

    // ── Delta-V NHATS (0–25) ──────────────────────────────────────────────────
    if (a.delta_v_nhats.has_value()) {
        has_data = true;
        double dv = a.delta_v_nhats.value();
        // < 4.5 km/s = perfekt (25 Punkte), > 12 km/s = 0 Punkte
        score += std::max(0.0, 25.0 * (1.0 - (dv - 4.5) / 7.5));
    } else {
        // Kein NHATS-Eintrag → Unsicherheits-Malus -5
        // Fallback: Orbit-Parameter wenn vorhanden
        if (a.inclination.has_value() && a.eccentricity.has_value()) {
            double i_score = std::max(0.0, 10.0 - a.inclination.value());
            double e_score = std::max(0.0, 10.0 * (1.0 - a.eccentricity.value()));
            score += (i_score + e_score) / 2.0 - 5.0;
        } else {
            score -= 5.0; // Kein ΔV, keine Orbit-Daten → Malus
        }
    }

    // ── Rotation (0–10) ───────────────────────────────────────────────────────
    if (a.rotation_period_h.has_value()) {
        has_data = true;
        double p = a.rotation_period_h.value();
        if      (p < 2.0)  score += 0.0;
        else if (p < 4.0)  score += 4.0;
        else if (p < 8.0)  score += 7.0;
        else if (p < 24.0) score += 10.0;
        else               score += 8.0;
    }

    // ── Albedo Bonus (0–5) ────────────────────────────────────────────────────
    if (a.albedo.has_value()) {
        has_data = true;
        double al = a.albedo.value();
        if      (al < 0.06) score += 5.0;
        else if (al < 0.12) score += 3.0;
        else if (al < 0.20) score += 1.0;
    }

    // ── Orbit-Klasse Modifier ─────────────────────────────────────────────────
    score += orbit_class_modifier(a.orbit_class);

    if (!has_data) return 0.0;

    return std::max(0.0, std::min(100.0, score));
}

Resources estimate_resources(const Asteroid& a) {
    Resources r;
    if (!a.diameter_km.has_value()) return r;

    double radius_m = (a.diameter_km.value() * 1000.0) / 2.0;
    double volume   = (4.0 / 3.0) * PI * std::pow(radius_m, 3);
    double density  = estimate_density(a.spectral_type);
    r.mass_kg       = volume * density;

    char spec = 'U';
    if (a.spectral_type.has_value() && !a.spectral_type.value().empty())
        spec = std::toupper(a.spectral_type.value()[0]);

    switch (spec) {
        case 'C': case 'B': case 'D':
            r.water_kg = r.mass_kg * 0.10;
            r.metal_kg = r.mass_kg * 0.15;
            r.pgm_kg   = r.metal_kg * 500e-9;
            break;
        case 'S':
            r.water_kg = r.mass_kg * 0.001;
            r.metal_kg = r.mass_kg * 0.20;
            r.pgm_kg   = r.metal_kg * 1e-6;
            break;
        case 'M': case 'X':
            r.water_kg = 0.0;
            r.metal_kg = r.mass_kg * 0.80;
            r.pgm_kg   = r.metal_kg * 5e-6;
            break;
        case 'V':
            r.water_kg = 0.0;
            r.metal_kg = r.mass_kg * 0.25;
            r.pgm_kg   = r.metal_kg * 0.5e-6;
            break;
        default:
            r.water_kg = r.mass_kg * 0.02;
            r.metal_kg = r.mass_kg * 0.10;
            r.pgm_kg   = r.metal_kg * 1e-6;
    }

    constexpr double WATER_USD_PER_KG = 1.0;
    constexpr double IRON_USD_PER_KG  = 0.0001;
    constexpr double PGM_USD_PER_KG   = 30000.0;

    r.estimated_value_usd = (r.water_kg * WATER_USD_PER_KG)
                          + (r.metal_kg * IRON_USD_PER_KG)
                          + (r.pgm_kg   * PGM_USD_PER_KG);

    return r;
}

static std::string format_mass(double kg) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(3);
    if      (kg >= 1e18) ss << kg / 1e18 << " Eg";
    else if (kg >= 1e15) ss << kg / 1e15 << " Pg";
    else if (kg >= 1e12) ss << kg / 1e12 << " Tg";
    else if (kg >= 1e9)  ss << kg / 1e9  << " Gg";
    else if (kg >= 1e6)  ss << kg / 1e6  << " Mg";
    else                 ss << kg         << " kg";
    return ss.str();
}

static std::string format_usd(double usd) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);
    if      (usd >= 1e18) ss << "$" << usd / 1e18 << " Quintillion";
    else if (usd >= 1e15) ss << "$" << usd / 1e15 << " Quadrillion";
    else if (usd >= 1e12) ss << "$" << usd / 1e12 << " Trillion";
    else if (usd >= 1e9)  ss << "$" << usd / 1e9  << " Billion";
    else if (usd >= 1e6)  ss << "$" << usd / 1e6  << " Million";
    else                  ss << "$" << usd;
    return ss.str();
}

void print_resources(const Resources& r) {
    std::cout << "[ Rohstoffe ]\n"
              << "  Gesamtmasse:   " << format_mass(r.mass_kg)   << "\n"
              << "  Wasser (H₂O): " << format_mass(r.water_kg)  << "\n"
              << "  Metall:        " << format_mass(r.metal_kg)  << "\n"
              << "  PGM:           " << format_mass(r.pgm_kg)    << "\n"
              << "\n[ Geschätzter Wert ]\n"
              << "  " << format_usd(r.estimated_value_usd) << "\n"
              << "  (konservativ, Erdmarktpreise)\n\n";
}
