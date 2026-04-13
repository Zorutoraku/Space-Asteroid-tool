#include "api_client.hpp"
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <stdexcept>

using json = nlohmann::json;

static size_t write_callback(char* ptr, size_t size, size_t nmemb, std::string* data) {
    data->append(ptr, size * nmemb);
    return size * nmemb;
}

ApiClient::ApiClient(const std::string& api_key) : m_api_key(api_key) {}

std::string ApiClient::fetch(const std::string& url) {
    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("curl init failed");

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK)
        throw std::runtime_error(curl_easy_strerror(res));

    return response;
}

std::vector<Asteroid> ApiClient::get_feed(const std::string& date) {
    std::string url = "https://api.nasa.gov/neo/rest/v1/feed?start_date=" + date +
                      "&end_date=" + date + "&api_key=" + m_api_key;

    json data = json::parse(fetch(url));
    std::vector<Asteroid> result;

    for (auto& [day, asteroids] : data["near_earth_objects"].items()) {
        for (auto& a : asteroids) {
            Asteroid ast;
            ast.id          = a["id"];
            ast.full_name   = a["name"];
            ast.orbit_class = "NEO";

            if (a.contains("estimated_diameter"))
                ast.diameter_km = a["estimated_diameter"]["kilometers"]["estimated_diameter_min"];

            ast.is_hazardous = a["is_potentially_hazardous_asteroid"].get<bool>();

            auto& approach = a["close_approach_data"][0];
            ast.miss_distance_km      = std::stod(approach["miss_distance"]["kilometers"].get<std::string>());
            ast.relative_velocity_kmh = std::stod(approach["relative_velocity"]["kilometers_per_hour"].get<std::string>());

            result.push_back(ast);
        }
    }
    return result;
}

Asteroid ApiClient::get_by_id(const std::string& asteroid_id) {
    std::string url = "https://api.nasa.gov/neo/rest/v1/neo/" + asteroid_id +
                      "?api_key=" + m_api_key;

    json a = json::parse(fetch(url));
    Asteroid ast;
    ast.id          = a["id"];
    ast.full_name   = a["name"];
    ast.orbit_class = "NEO";

    if (a.contains("estimated_diameter"))
        ast.diameter_km = a["estimated_diameter"]["kilometers"]["estimated_diameter_min"];

    ast.is_hazardous = a["is_potentially_hazardous_asteroid"].get<bool>();

    if (!a["close_approach_data"].empty()) {
        auto& approach = a["close_approach_data"].back();
        ast.miss_distance_km      = std::stod(approach["miss_distance"]["kilometers"].get<std::string>());
        ast.relative_velocity_kmh = std::stod(approach["relative_velocity"]["kilometers_per_hour"].get<std::string>());
    }

    return ast;
}

Asteroid ApiClient::get_by_name(const std::string& name) {
    std::string url = "https://ssd-api.jpl.nasa.gov/sbdb.api?sstr=" + name +
                      "&phys-par=1";

    json data = json::parse(fetch(url));

    if (data.contains("message"))
        throw std::runtime_error("SBDB: " + data["message"].get<std::string>());

    Asteroid ast;
    ast.id          = data["object"]["spkid"].get<std::string>();
    ast.full_name   = data["object"]["fullname"].get<std::string>();
    ast.orbit_class = data["object"]["orbit_class"]["name"].get<std::string>();

    if (data.contains("phys_par")) {
        for (auto& p : data["phys_par"]) {
            std::string pname = p["name"].get<std::string>();
            std::string val   = p.value("value", "");
            if (val.empty()) continue;

            if      (pname == "diameter") ast.diameter_km       = std::stod(val);
            else if (pname == "albedo")   ast.albedo            = std::stod(val);
            else if (pname == "H")        ast.abs_magnitude     = std::stod(val);
            else if (pname == "rot_per")  ast.rotation_period_h = std::stod(val);
            else if (pname == "spec_T" || pname == "spec_B")
                                          ast.spectral_type     = val;
        }
    }

    if (data.contains("orbit") && data["orbit"].contains("elements")) {
        for (auto& el : data["orbit"]["elements"]) {
            std::string ename = el["name"].get<std::string>();
            std::string val   = el.value("value", "");
            if (val.empty()) continue;

            if      (ename == "a")    ast.semi_major_axis = std::stod(val);
            else if (ename == "e")    ast.eccentricity    = std::stod(val);
            else if (ename == "i")    ast.inclination     = std::stod(val);
            else if (ename == "moid") ast.moid            = std::stod(val);
        }
    }

    return ast;
}
