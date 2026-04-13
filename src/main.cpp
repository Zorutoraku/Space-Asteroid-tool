#include "api_client.hpp"
#include "asteroid.hpp"
#include "calculator.hpp"
#include "mission.hpp"
#include "database.hpp"
#include "tui.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstdlib>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <curl/curl.h>

using json = nlohmann::json;

// ── Env / Key ────────────────────────────────────────────────────────────────
void load_env(const std::string& path = "../.env") {
    std::ifstream file(path);
    if (!file.is_open()) return;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        setenv(line.substr(0, eq).c_str(), line.substr(eq + 1).c_str(), 0);
    }
}

std::string get_api_key() {
    const char* key = std::getenv("NASA_API_KEY");
    if (!key) throw std::runtime_error("NASA_API_KEY not set");
    return key;
}

std::string extract_des(const std::string& spkid) {
    if (spkid.size() > 2 && spkid.substr(0, 2) == "20")
        return spkid.substr(2);
    return spkid;
}

static std::string url_encode_spaces(const std::string& s) {
    std::string r = s;
    size_t p = 0;
    while ((p = r.find(' ', p)) != std::string::npos)
        r.replace(p, 1, "%20"), p += 3;
    return r;
}

static std::string format_usd_short(double usd) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1);
    if      (usd >= 1e18) ss << "$" << usd/1e18 << "Q";
    else if (usd >= 1e15) ss << "$" << usd/1e15 << "P";
    else if (usd >= 1e12) ss << "$" << usd/1e12 << "T";
    else if (usd >= 1e9)  ss << "$" << usd/1e9  << "B";
    else if (usd >= 1e6)  ss << "$" << usd/1e6  << "M";
    else                  ss << "$" << usd;
    return ss.str();
}

void print_help() {
    std::cout << R"(
╔══════════════════════════════════════════════════════════╗
  ASTEROID MINER v0.9 — Commands
╠══════════════════════════════════════════════════════════╣
  ui                     Interaktive TUI starten

  search <n>             Asteroid per Name suchen
  mission <n>            Vollanalyse + Missionsdaten
  get <id>               Asteroid per NeoWs-ID
  feed <YYYY-MM-DD>      NEOs für ein Datum
  batch <n>              Top-N erreichbare NEAs laden

  list [sort] [limit]    Asteroiden anzeigen
    Sortierung: score | value | diameter | dv | name
    Filter:     top | scored
    Beispiele:
      list top 10
      list dv 20
      list value 5

  watchlist              Watchlist anzeigen
  watchlist add <n>      Zur Watchlist hinzufügen
  watchlist rm <n>       Von Watchlist entfernen

  help                   Diese Hilfe
  quit                   Beenden
╚══════════════════════════════════════════════════════════╝
)";
}

void print_list(const std::vector<Asteroid>& list) {
    if (list.empty()) { std::cout << "  (leer)\n"; return; }

    std::cout << "\n"
              << std::left
              << std::setw(8)  << "Score"
              << std::setw(32) << "Name"
              << std::setw(6)  << "Typ"
              << std::setw(11) << "Diameter"
              << std::setw(10) << "ΔV km/s"
              << "Wert\n"
              << std::string(82, '-') << "\n";

    for (auto& a : list) {
        std::string score = a.mining_score_val.has_value()
            ? std::to_string((int)a.mining_score_val.value()) : "N/A";

        std::ostringstream diam_ss;
        diam_ss << std::fixed << std::setprecision(3);
        std::string diam = a.diameter_km.has_value()
            ? (diam_ss << a.diameter_km.value(), diam_ss.str() + " km") : "N/A";

        std::string spec = a.spectral_type.value_or("?");

        std::string dv = "N/A";
        if (a.delta_v_nhats.has_value()) {
            std::ostringstream dvss;
            dvss << std::fixed << std::setprecision(2) << a.delta_v_nhats.value();
            dv = dvss.str();
        }

        std::string val = a.miss_distance_km.has_value()
            ? format_usd_short(a.miss_distance_km.value()) : "N/A";

        std::cout << std::left
                  << std::setw(8)  << score
                  << std::setw(32) << a.full_name.substr(0, 31)
                  << std::setw(6)  << spec
                  << std::setw(11) << diam
                  << std::setw(10) << dv
                  << val << "\n";
    }
    std::cout << "\n";
}

static size_t write_cb(char* ptr, size_t size, size_t nmemb, std::string* data) {
    data->append(ptr, size * nmemb);
    return size * nmemb;
}

static std::string http_get(const std::string& url) {
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

void batch_download(ApiClient& client, Database& db, int count) {
    std::cout << "  Lade NHATS-Liste...\n";
    json data = json::parse(http_get("https://ssd-api.jpl.nasa.gov/nhats.api"));

    if (!data.contains("data")) {
        std::cout << "  Keine Daten von NHATS.\n";
        return;
    }

    auto& entries = data["data"];
    int total = std::min((int)entries.size(), count);
    std::cout << "  " << entries.size() << " erreichbare NEAs. Lade Top-" << total << "...\n\n";

    int success = 0, skipped = 0;
    for (int i = 0; i < total; i++) {
        std::string des  = entries[i]["des"].get<std::string>();
        std::string name = entries[i].value("fullname", des);

        std::cout << "  [" << (i+1) << "/" << total << "] " << name << "... ";
        std::cout.flush();

        try {
            auto a = client.get_by_name(url_encode_spaces(des));
            compute(a);

            if (entries[i].contains("min_dv")) {
                auto& mdv = entries[i]["min_dv"];
                if (mdv.contains("dv"))
                    a.delta_v_nhats = std::stod(mdv["dv"].get<std::string>());
                if (mdv.contains("dur"))
                    a.mission_dur_days = mdv["dur"].get<int>();
                a.mining_score_val = mining_score(a);
            }

            auto r = estimate_resources(a);
            db.save_asteroid(a, r);
            std::cout << "✓ Score: " << (int)a.mining_score_val.value_or(0)
                      << "  ΔV: " << (a.delta_v_nhats.has_value()
                          ? std::to_string(a.delta_v_nhats.value()).substr(0,5) : "N/A")
                      << " km/s\n";
            success++;
        } catch (const std::exception& ex) {
            std::cout << "✗ " << ex.what() << "\n";
            skipped++;
        }
    }

    std::cout << "\n  Fertig: " << success << " gespeichert, "
              << skipped << " übersprungen.\n"
              << "  Starte TUI mit 'ui'.\n";
}

int main() {
    load_env();
    ApiClient client(get_api_key());
    Database  db;

    std::cout << "=== ASTEROID MINER v0.9 ===\n";
    std::cout << "Tippe 'help' oder 'ui' für die interaktive Ansicht.\n";

    std::string line, cmd;
    while (true) {
        std::cout << "\n> ";
        if (!std::getline(std::cin, line)) break;

        std::istringstream iss(line);
        iss >> cmd;
        if (cmd.empty()) continue;
        if (cmd == "quit") break;

        try {
            if (cmd == "ui") {
                run_tui(db);

            } else if (cmd == "help") {
                print_help();

            } else if (cmd == "search") {
                std::string arg; iss >> arg;
                auto a = client.get_by_name(arg);
                compute(a);
                auto r = estimate_resources(a);
                db.save_asteroid(a, r);
                print_asteroid(a);
                print_resources(r);
                std::cout << "  ✓ Gespeichert\n";

            } else if (cmd == "mission") {
                std::string arg; iss >> arg;
                auto a = client.get_by_name(arg);
                compute(a);
                auto r = estimate_resources(a);
                db.save_asteroid(a, r);
                print_asteroid(a);
                print_resources(r);
                auto mission = fetch_mission(extract_des(a.id), a.full_name, a.semi_major_axis);
                print_mission(mission);
                std::cout << "  ✓ Gespeichert\n";

            } else if (cmd == "get") {
                std::string arg; iss >> arg;
                auto a = client.get_by_id(arg);
                compute(a);
                auto r = estimate_resources(a);
                db.save_asteroid(a, r);
                print_asteroid(a);
                print_resources(r);
                std::cout << "  ✓ Gespeichert\n";

            } else if (cmd == "feed") {
                std::string arg; iss >> arg;
                auto asteroids = client.get_feed(arg);
                std::cout << "Found " << asteroids.size() << " asteroids:\n";
                for (auto& a : asteroids) {
                    compute(a);
                    auto r = estimate_resources(a);
                    db.save_asteroid(a, r);
                    print_asteroid(a);
                    print_resources(r);
                }

            } else if (cmd == "batch") {
                std::string arg; iss >> arg;
                int n = arg.empty() ? 20 : std::stoi(arg);
                batch_download(client, db, n);

            } else if (cmd == "list") {
                std::string sub; iss >> sub;
                std::string lim_str; iss >> lim_str;

                bool sub_is_num = !sub.empty() && std::all_of(sub.begin(), sub.end(), ::isdigit);
                if (sub_is_num) { lim_str = sub; sub = "score"; }

                int limit = lim_str.empty() ? 0 : std::stoi(lim_str);
                bool scored_only = (sub == "top" || sub == "scored");
                std::string sort = sub.empty() ? "score" : sub;
                if (sort == "top" || sort == "scored") sort = "score";

                auto asteroids = db.list_asteroids(sort, scored_only);
                if (limit > 0 && (int)asteroids.size() > limit)
                    asteroids.resize(limit);

                std::string label = scored_only ? " (mit Score)" : "";
                std::cout << "\n  " << asteroids.size() << " Asteroiden" << label << ":\n";
                print_list(asteroids);

            } else if (cmd == "watchlist") {
                std::string sub; iss >> sub;
                if (sub.empty()) {
                    auto wl = db.get_watchlist();
                    std::cout << "\n  Watchlist (" << wl.size() << " Einträge):\n";
                    print_list(wl);
                } else if (sub == "add") {
                    std::string arg; iss >> arg;
                    auto a = client.get_by_name(arg);
                    compute(a);
                    auto r = estimate_resources(a);
                    db.save_asteroid(a, r);
                    db.add_watchlist(a.id);
                    std::cout << "  ✓ " << a.full_name << " zur Watchlist hinzugefügt\n";
                } else if (sub == "rm") {
                    std::string arg; iss >> arg;
                    auto a = client.get_by_name(arg);
                    db.remove_watchlist(a.id);
                    std::cout << "  ✓ " << a.full_name << " von Watchlist entfernt\n";
                } else {
                    std::cout << "  Nutze: watchlist | watchlist add <n> | watchlist rm <n>\n";
                }

            } else {
                std::cout << "  Unbekannter Befehl. Tippe 'help'.\n";
            }

        } catch (const std::exception& ex) {
            std::cerr << "  Error: " << ex.what() << "\n";
        }
    }
}
