#include "tui.hpp"
#include "mission.hpp"
#include <ncurses.h>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>

#define C_NORMAL    1
#define C_HEADER    2
#define C_SELECTED  3
#define C_SCORE_HI  4
#define C_SCORE_MED 5
#define C_SCORE_LO  6
#define C_TITLE     7
#define C_KEY       8
#define C_VALUE     9
#define C_BORDER    10
#define C_FOOTER    11
#define C_WATCHLIST 12

enum class ViewMode { LIST, MISSION };

struct TuiState {
    std::vector<Asteroid> all;
    std::vector<Asteroid> filtered;
    int  selected    = 0;
    int  scroll_off  = 0;
    std::string sort_by      = "score";
    bool scored_only         = false;
    bool watchlist_only      = false;
    std::string search_str   = "";
    bool searching           = false;
    std::string status_msg   = "";
    ViewMode view            = ViewMode::LIST;
    std::optional<MissionData> mission_cache;
    bool mission_loading     = false;
};

// ── Format helpers ────────────────────────────────────────────────────────────
static std::string fmt_usd(double usd) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1);
    if      (usd >= 1e18) ss << "$" << usd/1e18 << "Q";
    else if (usd >= 1e15) ss << "$" << usd/1e15 << "P";
    else if (usd >= 1e12) ss << "$" << usd/1e12 << "T";
    else if (usd >= 1e9)  ss << "$" << usd/1e9  << "B";
    else if (usd >= 1e6)  ss << "$" << usd/1e6  << "M";
    else if (usd > 0)     ss << "$" << (long long)usd;
    else                  ss << "N/A";
    return ss.str();
}

static std::string fmt_d(std::optional<double> v, int prec = 3, const std::string& unit = "") {
    if (!v.has_value()) return "N/A";
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(prec) << v.value();
    if (!unit.empty()) ss << " " << unit;
    return ss.str();
}

static std::string fmt_s(std::optional<std::string> v) { return v.value_or("N/A"); }

static std::string trunc_s(const std::string& s, int w) {
    if (w <= 0) return "";
    if ((int)s.size() <= w) return s;
    return s.substr(0, w - 1) + "~";
}

static std::string pad_r(const std::string& s, int w) {
    if ((int)s.size() >= w) return s.substr(0, w);
    return s + std::string(w - s.size(), ' ');
}

static int score_color(double score) {
    if (score >= 50) return C_SCORE_HI;
    if (score >= 25) return C_SCORE_MED;
    return C_SCORE_LO;
}

static void draw_score_bar(WINDOW* w, int y, int x, double score, int width) {
    int filled = (int)(score / 100.0 * width);
    wattron(w, COLOR_PAIR(C_VALUE));
    for (int i = 0; i < width; i++)
        mvwaddch(w, y, x + i, i < filled ? '#' : '.');
    wattroff(w, COLOR_PAIR(C_VALUE));
    mvwprintw(w, y, x + width + 1, "%.0f/100", score);
}

static std::string extract_des(const std::string& spkid) {
    if (spkid.size() > 2 && spkid.substr(0, 2) == "20")
        return spkid.substr(2);
    return spkid;
}

// ── Filters ───────────────────────────────────────────────────────────────────
static void apply_filters(TuiState& st) {
    st.filtered.clear();
    for (auto& a : st.all) {
        if (st.scored_only && (!a.mining_score_val.has_value() || a.mining_score_val.value() <= 0))
            continue;
        if (!st.search_str.empty()) {
            std::string name = a.full_name, srch = st.search_str;
            std::transform(name.begin(), name.end(), name.begin(), ::tolower);
            std::transform(srch.begin(), srch.end(), srch.begin(), ::tolower);
            if (name.find(srch) == std::string::npos) continue;
        }
        st.filtered.push_back(a);
    }
    std::sort(st.filtered.begin(), st.filtered.end(), [&](const Asteroid& a, const Asteroid& b) {
        if (st.sort_by == "score")    return a.mining_score_val.value_or(-1) > b.mining_score_val.value_or(-1);
        if (st.sort_by == "value")    return a.miss_distance_km.value_or(0)  > b.miss_distance_km.value_or(0);
        if (st.sort_by == "dv")       return a.delta_v_nhats.value_or(999)   < b.delta_v_nhats.value_or(999);
        if (st.sort_by == "diameter") return a.diameter_km.value_or(0)       > b.diameter_km.value_or(0);
        if (st.sort_by == "name")     return a.full_name < b.full_name;
        return false;
    });
    if (st.selected >= (int)st.filtered.size())
        st.selected = std::max(0, (int)st.filtered.size() - 1);
}

// ── Draw Header ───────────────────────────────────────────────────────────────
static void draw_header(WINDOW* w, const TuiState& st) {
    int width = getmaxx(w);
    wattron(w, COLOR_PAIR(C_HEADER) | A_BOLD);
    mvwhline(w, 0, 0, ' ', width);
    mvwprintw(w, 0, 1, " * ASTEROID MINER  ");
    std::string info = "";
    if (st.view == ViewMode::MISSION) info += "[MISSION] ";
    if (st.scored_only)              info += "[TOP] ";
    if (st.watchlist_only)           info += "[WATCH] ";
    if (!st.search_str.empty())      info += "[/" + st.search_str + "] ";
    info += "sort:" + st.sort_by + "  " + std::to_string(st.filtered.size()) + " obj";
    mvwprintw(w, 0, 20, "%s", info.c_str());
    wattroff(w, COLOR_PAIR(C_HEADER) | A_BOLD);
}

// ── Draw List Panel ───────────────────────────────────────────────────────────
static void draw_list(WINDOW* w, TuiState& st, int body_h) {
    int width  = getmaxx(w);
    int height = body_h - 2;

    wattron(w, COLOR_PAIR(C_BORDER) | A_BOLD);
    mvwprintw(w, 0, 0, "%s", pad_r(" Sc  Name", width).c_str());
    wattroff(w, COLOR_PAIR(C_BORDER) | A_BOLD);

    if (st.selected < st.scroll_off)
        st.scroll_off = st.selected;
    if (st.selected >= st.scroll_off + height)
        st.scroll_off = st.selected - height + 1;

    for (int i = 0; i < height; i++) {
        int idx = st.scroll_off + i;
        int row = i + 1;
        wmove(w, row, 0);
        wclrtoeol(w);
        if (idx >= (int)st.filtered.size()) continue;

        const auto& a = st.filtered[idx];
        bool is_sel   = (idx == st.selected);
        double score  = a.mining_score_val.value_or(0);

        if (is_sel) wattron(w, COLOR_PAIR(C_SELECTED) | A_BOLD);
        else        wattron(w, COLOR_PAIR(score_color(score)));

        std::string sc   = a.mining_score_val.has_value() ? std::to_string((int)score) : "?";
        std::string name = trunc_s(a.full_name, width - 6);
        mvwprintw(w, row, 0, " %3s  %s", sc.c_str(), name.c_str());

        if (is_sel) wattroff(w, COLOR_PAIR(C_SELECTED) | A_BOLD);
        else        wattroff(w, COLOR_PAIR(score_color(score)));
    }

    wattron(w, COLOR_PAIR(C_FOOTER) | A_BOLD);
    if (st.scroll_off > 0)
        mvwprintw(w, 1, width - 4, " /\\ ");
    if (st.scroll_off + height < (int)st.filtered.size())
        mvwprintw(w, height, width - 4, " \\/ ");
    mvwprintw(w, body_h - 1, 1, " %d/%d ",
              st.filtered.empty() ? 0 : st.selected + 1,
              (int)st.filtered.size());
    wattroff(w, COLOR_PAIR(C_FOOTER) | A_BOLD);
}

// ── Draw Detail Panel (normal view) ──────────────────────────────────────────
static void draw_detail_normal(WINDOW* w, const TuiState& st) {
    werase(w);
    int width   = getmaxx(w);
    int max_row = getmaxy(w) - 1;

    wattron(w, COLOR_PAIR(C_BORDER));
    for (int r = 0; r < getmaxy(w); r++)
        mvwaddch(w, r, 0, ACS_VLINE);
    wattroff(w, COLOR_PAIR(C_BORDER));

    if (st.filtered.empty()) {
        mvwprintw(w, 2, 2, "Keine Asteroiden.");
        return;
    }

    const Asteroid& a = st.filtered[st.selected];
    int row = 0;

    wattron(w, COLOR_PAIR(C_TITLE) | A_BOLD);
    mvwprintw(w, row++, 2, "%s", trunc_s(a.full_name, width - 3).c_str());
    wattroff(w, A_BOLD);
    mvwprintw(w, row++, 2, "ID: %s  |  %s", a.id.c_str(), a.orbit_class.c_str());
    wattroff(w, COLOR_PAIR(C_TITLE));

    auto label = [&](const std::string& l, const std::string& v) {
        if (row >= max_row) return;
        wattron(w, COLOR_PAIR(C_KEY));
        mvwprintw(w, row, 2, "%-20s", l.c_str());
        wattroff(w, COLOR_PAIR(C_KEY));
        wattron(w, COLOR_PAIR(C_VALUE));
        mvwprintw(w, row, 22, "%s", trunc_s(v, width - 24).c_str());
        wattroff(w, COLOR_PAIR(C_VALUE));
        row++;
    };

    auto section = [&](const std::string& title) {
        if (row + 1 >= max_row) return;
        row++;
        wattron(w, COLOR_PAIR(C_BORDER) | A_BOLD);
        mvwhline(w, row, 2, ACS_HLINE, width - 3);
        mvwprintw(w, row, 3, " %s ", title.c_str());
        wattroff(w, COLOR_PAIR(C_BORDER) | A_BOLD);
        row++;
    };

    section("PHYSIK");
    label("Diameter",        fmt_d(a.diameter_km, 3, "km"));
    label("Albedo",          fmt_d(a.albedo, 4));
    label("Abs. Magnitude",  fmt_d(a.abs_magnitude, 2));
    label("Rotation",        fmt_d(a.rotation_period_h, 2, "h"));
    label("Spektraltyp",     fmt_s(a.spectral_type));

    section("ORBIT");
    label("Semi-major axis", fmt_d(a.semi_major_axis, 3, "AU"));
    label("Exzentrizitaet",  fmt_d(a.eccentricity, 4));
    label("Inklination",     fmt_d(a.inclination, 2, "deg"));
    label("MOID",            fmt_d(a.moid, 4, "AU"));

    section("BERECHNUNGEN");
    label("Surface Gravity", fmt_d(a.surface_gravity, 6, "m/s2"));
    label("Escape Velocity", fmt_d(a.escape_velocity, 3, "m/s"));

    section("NHATS (DB)");
    label("Delta-V",         fmt_d(a.delta_v_nhats, 3, "km/s"));
    if (a.mission_dur_days.has_value()) {
        int d = a.mission_dur_days.value();
        label("Missionsdauer", std::to_string(d) + "d (" +
              std::to_string(d/365) + "y " + std::to_string(d%365) + "d)");
    } else {
        label("Missionsdauer", "N/A");
    }

    section("ROHSTOFFE & WERT");
    if (a.miss_distance_km.has_value())
        label("Geschaetzter Wert", fmt_usd(a.miss_distance_km.value()));
    else
        label("Geschaetzter Wert", "N/A");

    if (row < max_row - 2 && a.mining_score_val.has_value()) {
        section("MINING SCORE");
        if (row < max_row) {
            double score = a.mining_score_val.value();
            wattron(w, COLOR_PAIR(score_color(score)) | A_BOLD);
            draw_score_bar(w, row, 2, score, std::min(30, width - 16));
            wattroff(w, COLOR_PAIR(score_color(score)) | A_BOLD);
            row++;
        }
    }

    // Hint
    if (row < max_row) {
        wattron(w, COLOR_PAIR(C_FOOTER));
        mvwprintw(w, max_row, 2, " m=Mission laden ");
        wattroff(w, COLOR_PAIR(C_FOOTER));
    }
}

// ── Draw Mission Panel ────────────────────────────────────────────────────────
static void draw_detail_mission(WINDOW* w, const TuiState& st) {
    werase(w);
    int width   = getmaxx(w);
    int max_row = getmaxy(w) - 1;

    wattron(w, COLOR_PAIR(C_BORDER));
    for (int r = 0; r < getmaxy(w); r++)
        mvwaddch(w, r, 0, ACS_VLINE);
    wattroff(w, COLOR_PAIR(C_BORDER));

    int row = 0;

    if (st.mission_loading) {
        wattron(w, COLOR_PAIR(C_TITLE) | A_BOLD);
        mvwprintw(w, row + 2, 2, "Lade Mission-Daten...");
        wattroff(w, COLOR_PAIR(C_TITLE) | A_BOLD);
        return;
    }

    if (!st.mission_cache.has_value()) {
        mvwprintw(w, row + 2, 2, "Keine Mission-Daten. Druecke m.");
        return;
    }

    const MissionData& m = st.mission_cache.value();

    wattron(w, COLOR_PAIR(C_TITLE) | A_BOLD);
    mvwprintw(w, row++, 2, "MISSION: %s", trunc_s(m.full_name, width - 12).c_str());
    wattroff(w, COLOR_PAIR(C_TITLE) | A_BOLD);
    row++;

    auto label = [&](const std::string& l, const std::string& v) {
        if (row >= max_row) return;
        wattron(w, COLOR_PAIR(C_KEY));
        mvwprintw(w, row, 2, "%-22s", l.c_str());
        wattroff(w, COLOR_PAIR(C_KEY));
        wattron(w, COLOR_PAIR(C_VALUE));
        mvwprintw(w, row, 24, "%s", trunc_s(v, width - 26).c_str());
        wattroff(w, COLOR_PAIR(C_VALUE));
        row++;
    };

    auto section = [&](const std::string& title) {
        if (row + 1 >= max_row) return;
        row++;
        wattron(w, COLOR_PAIR(C_BORDER) | A_BOLD);
        mvwhline(w, row, 2, ACS_HLINE, width - 3);
        mvwprintw(w, row, 3, " %s ", title.c_str());
        wattroff(w, COLOR_PAIR(C_BORDER) | A_BOLD);
        row++;
    };

    // Hohmann
    if (m.hohmann.has_value()) {
        section("HOHMANN TRANSFER");
        auto& h = m.hohmann.value();
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2);
        ss << h.transfer_a_au; label("Transfer Orbit a", ss.str() + " AU");
        ss.str(""); ss << (int)h.flight_time_days; label("Flugzeit (einfach)", ss.str() + " Tage");
        ss.str(""); ss << h.delta_v_kms; label("Delta-V (grob)", ss.str() + " km/s");
        ss.str(""); ss << (int)h.synodic_period_days;
        label("Synod. Periode", ss.str() + " Tage (" +
              std::to_string((int)(h.synodic_period_days/365.25)) + "y)");
    }

    // NHATS Optimal
    if (m.nhats_available && m.min_dv.has_value()) {
        section("OPTIMALE TRAJEKTORIE (min DV)");
        auto& t = m.min_dv.value();
        label("Launch",       t.launch_date);
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(3);
        ss << t.dv_total_kms; label("Delta-V total",   ss.str() + " km/s");
        label("Gesamtdauer",  std::to_string(t.dur_total_days) + " Tage");
        label("Hinflug",      std::to_string(t.dur_out_days)   + " Tage");
        label("Am Asteroiden",std::to_string(t.dur_at_days)    + " Tage");
        label("Rueckflug",    std::to_string(t.dur_ret_days)   + " Tage");
        ss.str(""); ss << t.c3; label("C3 (Launch)",   ss.str() + " km2/s2");
    }

    // NHATS Kuerzeste
    if (m.nhats_available && m.min_dur.has_value()) {
        section("KUERZESTE MISSION (min Dauer)");
        auto& t = m.min_dur.value();
        label("Launch",       t.launch_date);
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(3);
        ss << t.dv_total_kms; label("Delta-V total",   ss.str() + " km/s");
        label("Gesamtdauer",  std::to_string(t.dur_total_days) + " Tage");
        label("Hinflug",      std::to_string(t.dur_out_days)   + " Tage");
        label("Am Asteroiden",std::to_string(t.dur_at_days)    + " Tage");
        label("Rueckflug",    std::to_string(t.dur_ret_days)   + " Tage");
    }

    if (!m.nhats_available) {
        if (row < max_row) {
            wattron(w, COLOR_PAIR(C_SCORE_LO));
            mvwprintw(w, row++, 2, "NHATS: nicht verfuegbar (kein NEA)");
            wattroff(w, COLOR_PAIR(C_SCORE_LO));
        }
    }

    // Close Approaches
    if (!m.close_approaches.empty()) {
        section("NAECHSTE ERDVORBEIFLUEGE");
        for (auto& ca : m.close_approaches) {
            if (row >= max_row) break;
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(4);
            long long km = (long long)(ca.dist_au * 1.496e8);
            ss << ca.date << "  " << ca.dist_au << " AU  ("
               << km << " km)  " << std::setprecision(2) << ca.v_rel_kms << " km/s";
            wattron(w, COLOR_PAIR(C_VALUE));
            mvwprintw(w, row++, 2, "%s", trunc_s(ss.str(), width - 4).c_str());
            wattroff(w, COLOR_PAIR(C_VALUE));
        }
    } else if (m.nhats_available) {
        if (row < max_row) {
            mvwprintw(w, row++, 2, "Keine Vorbeifluege bis 2100.");
        }
    }

    // Hint
    wattron(w, COLOR_PAIR(C_FOOTER));
    mvwprintw(w, max_row, 2, " m/ESC=zurueck zu Details ");
    wattroff(w, COLOR_PAIR(C_FOOTER));
}

// ── Draw Footer ───────────────────────────────────────────────────────────────
static void draw_footer(WINDOW* w, const TuiState& st) {
    int width = getmaxx(w);
    wattron(w, COLOR_PAIR(C_FOOTER));
    mvwhline(w, 0, 0, ' ', width);
    if (st.searching) {
        mvwprintw(w, 0, 1, " /%s_", st.search_str.c_str());
    } else if (!st.status_msg.empty()) {
        mvwprintw(w, 0, 1, " %s", st.status_msg.c_str());
    } else if (st.view == ViewMode::MISSION) {
        mvwprintw(w, 0, 1,
            " UP/DOWN=navigate  m/ESC=Details  s=score v=wert d=dv  t=top w=watch W=add/rm  q=quit");
    } else {
        mvwprintw(w, 0, 1,
            " UP/DOWN j/k=nav  PgUp/Dn  g/G=top/end  "
            "s=score v=wert d=dv z=groesse n=name  "
            "t=top w=watch W=add/rm  m=mission  /=suche  ESC=reset  q=quit");
    }
    wattroff(w, COLOR_PAIR(C_FOOTER));
}

// ── Main TUI ──────────────────────────────────────────────────────────────────
void run_tui(Database& db) {
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    set_escdelay(50);

    if (!has_colors()) { endwin(); return; }
    start_color();
    use_default_colors();

    init_pair(C_NORMAL,    COLOR_WHITE,   -1);
    init_pair(C_HEADER,    COLOR_BLACK,   COLOR_CYAN);
    init_pair(C_SELECTED,  COLOR_BLACK,   COLOR_YELLOW);
    init_pair(C_SCORE_HI,  COLOR_GREEN,   -1);
    init_pair(C_SCORE_MED, COLOR_YELLOW,  -1);
    init_pair(C_SCORE_LO,  COLOR_RED,     -1);
    init_pair(C_TITLE,     COLOR_CYAN,    -1);
    init_pair(C_KEY,       COLOR_WHITE,   -1);
    init_pair(C_VALUE,     COLOR_CYAN,    -1);
    init_pair(C_BORDER,    COLOR_YELLOW,  -1);
    init_pair(C_FOOTER,    COLOR_BLACK,   COLOR_WHITE);
    init_pair(C_WATCHLIST, COLOR_MAGENTA, -1);

    refresh(); // stdscr initial flush

    TuiState st;
    st.all = db.list_asteroids("score", false);
    apply_filters(st);

    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    int left_w  = cols * 38 / 100;
    int right_w = cols - left_w;
    int body_h  = rows - 2;

    WINDOW* header_w = newwin(1,      cols,    0,      0);
    WINDOW* footer_w = newwin(1,      cols,    rows-1, 0);
    WINDOW* list_w   = newwin(body_h, left_w,  1,      0);
    WINDOW* detail_w = newwin(body_h, right_w, 1,      left_w);

    keypad(list_w,   TRUE);
    keypad(detail_w, TRUE);

    bool running = true;
    while (running) {
        // Resize
        int nr, nc;
        getmaxyx(stdscr, nr, nc);
        if (nr != rows || nc != cols) {
            rows = nr; cols = nc;
            left_w  = cols * 38 / 100;
            right_w = cols - left_w;
            body_h  = rows - 2;
            wresize(header_w, 1,      cols);
            wresize(footer_w, 1,      cols);
            wresize(list_w,   body_h, left_w);
            wresize(detail_w, body_h, right_w);
            mvwin(footer_w, rows-1, 0);
            mvwin(detail_w, 1,      left_w);
            clear(); refresh();
        }

        // Draw
        draw_header(header_w, st);
        draw_list(list_w, st, body_h);
        if (st.view == ViewMode::MISSION)
            draw_detail_mission(detail_w, st);
        else
            draw_detail_normal(detail_w, st);
        draw_footer(footer_w, st);

        wnoutrefresh(header_w);
        wnoutrefresh(list_w);
        wnoutrefresh(detail_w);
        wnoutrefresh(footer_w);
        doupdate();

        int ch = getch();

        if (st.searching) {
            if (ch == '\n' || ch == KEY_ENTER) {
                st.searching = false;
                curs_set(0);
                apply_filters(st);
                st.status_msg = st.search_str.empty() ? "" : "Suche: " + st.search_str;
            } else if (ch == 27) {
                st.searching  = false;
                curs_set(0);
                st.search_str = "";
                apply_filters(st);
                st.status_msg = "";
            } else if (ch == KEY_BACKSPACE || ch == 127) {
                if (!st.search_str.empty()) st.search_str.pop_back();
                apply_filters(st);
            } else if (ch >= 32 && ch < 127) {
                st.search_str += (char)ch;
                apply_filters(st);
            }
            continue;
        }

        st.status_msg = "";

        switch (ch) {
            case 'q': case 'Q': running = false; break;

            case KEY_UP:   case 'k':
                if (st.selected > 0) {
                    st.selected--;
                    st.mission_cache = std::nullopt;
                    st.view = ViewMode::LIST;
                }
                break;
            case KEY_DOWN: case 'j':
                if (st.selected < (int)st.filtered.size() - 1) {
                    st.selected++;
                    st.mission_cache = std::nullopt;
                    st.view = ViewMode::LIST;
                }
                break;
            case KEY_PPAGE:
                st.selected = std::max(0, st.selected - (body_h - 4));
                st.mission_cache = std::nullopt;
                st.view = ViewMode::LIST;
                break;
            case KEY_NPAGE:
                st.selected = std::min((int)st.filtered.size() - 1, st.selected + (body_h - 4));
                st.mission_cache = std::nullopt;
                st.view = ViewMode::LIST;
                break;
            case 'g': case KEY_HOME:
                st.selected = 0;
                st.mission_cache = std::nullopt;
                st.view = ViewMode::LIST;
                break;
            case 'G': case KEY_END:
                st.selected = std::max(0, (int)st.filtered.size() - 1);
                st.mission_cache = std::nullopt;
                st.view = ViewMode::LIST;
                break;

            // Mission toggle
            case 'm':
                if (st.filtered.empty()) break;
                if (st.view == ViewMode::MISSION) {
                    st.view = ViewMode::LIST;
                } else {
                    st.view = ViewMode::MISSION;
                    if (!st.mission_cache.has_value()) {
                        const Asteroid& a = st.filtered[st.selected];
                        st.status_msg = "Lade Mission-Daten...";

                        // Draw loading state
                        st.mission_loading = true;
                        draw_detail_mission(detail_w, st);
                        wnoutrefresh(detail_w);
                        doupdate();

                        try {
                            std::string des = extract_des(a.id);
                            st.mission_cache = fetch_mission(des, a.full_name, a.semi_major_axis);
                            st.status_msg = "Mission geladen: " + a.full_name;
                        } catch (const std::exception& ex) {
                            st.status_msg = "Fehler: ";
                            st.status_msg += ex.what();
                        }
                        st.mission_loading = false;
                    }
                }
                break;

            case 's': st.sort_by = "score";    apply_filters(st); st.status_msg = "Sort: Score";   break;
            case 'v': st.sort_by = "value";    apply_filters(st); st.status_msg = "Sort: Wert";    break;
            case 'd': st.sort_by = "dv";       apply_filters(st); st.status_msg = "Sort: DV";      break;
            case 'z': st.sort_by = "diameter"; apply_filters(st); st.status_msg = "Sort: Groesse"; break;
            case 'n': st.sort_by = "name";     apply_filters(st); st.status_msg = "Sort: Name";    break;

            case 't':
                st.scored_only = !st.scored_only;
                apply_filters(st);
                st.status_msg = st.scored_only ? "Filter: nur mit Score" : "Filter: alle";
                break;

            case 'w':
                st.watchlist_only = !st.watchlist_only;
                st.all = st.watchlist_only ? db.get_watchlist() : db.list_asteroids("score", false);
                apply_filters(st);
                st.status_msg = st.watchlist_only ? "Watchlist Ansicht" : "Alle Asteroiden";
                break;

            case 'W':
                if (!st.filtered.empty()) {
                    auto& a = st.filtered[st.selected];
                    if (st.watchlist_only) {
                        db.remove_watchlist(a.id);
                        st.status_msg = "- Entfernt: " + a.full_name;
                        st.all = db.get_watchlist();
                        apply_filters(st);
                    } else {
                        db.add_watchlist(a.id);
                        st.status_msg = "+ Watchlist: " + a.full_name;
                    }
                }
                break;

            case '/':
                st.searching  = true;
                st.search_str = "";
                curs_set(1);
                break;

            case 27: // ESC
                if (st.view == ViewMode::MISSION) {
                    st.view = ViewMode::LIST;
                } else {
                    st.search_str     = "";
                    st.scored_only    = false;
                    st.watchlist_only = false;
                    st.all = db.list_asteroids("score", false);
                    apply_filters(st);
                    st.status_msg = "Filter zurueckgesetzt";
                }
                break;

            case '\n': case KEY_ENTER:
                st.all = st.watchlist_only ? db.get_watchlist() : db.list_asteroids("score", false);
                apply_filters(st);
                st.status_msg = "Aktualisiert";
                break;
        }
    }

    delwin(header_w);
    delwin(footer_w);
    delwin(list_w);
    delwin(detail_w);
    endwin();
}
