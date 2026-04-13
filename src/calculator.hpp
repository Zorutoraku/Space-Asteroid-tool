#pragma once
#include "asteroid.hpp"

struct Resources {
    double mass_kg             = 0.0;
    double water_kg            = 0.0;
    double metal_kg            = 0.0;
    double pgm_kg              = 0.0;
    double estimated_value_usd = 0.0;
};

double    estimate_density(const std::optional<std::string>& spectral_type);
void      compute(Asteroid& a);
double    mining_score(const Asteroid& a);
Resources estimate_resources(const Asteroid& a);
void      print_resources(const Resources& r);
