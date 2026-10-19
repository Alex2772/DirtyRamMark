#include "hw_logo.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define MAX_NAME_LEN 255

typedef struct Name {
    char s[MAX_NAME_LEN + 1];
} Name;

static Name normalize(const char* name) {
    // lowercase, drops "(r)", "(tm)" and collapses repeated spaces: "Intel(R) Core(TM) i7-8700K" -> "intel core i7-8700k"
    Name n = {};
    size_t len = 0;
    for (const char* p = name; *p && len < MAX_NAME_LEN; ++p) {
        if (*p == '(') {
            const char* end = strchr(p, ')');
            if (end && end - p <= 4) {
                p = end;
                continue;
            }
        }
        char c = (char) tolower((unsigned char) *p);
        if (c == ' ' && (len == 0 || n.s[len - 1] == ' ')) {
            continue;
        }
        n.s[len++] = c;
    }
    while (len > 0 && n.s[len - 1] == ' ') {
        n.s[--len] = '\0';
    }
    return n;
}

static inline bool has(const Name* n, const char* needle) {
    return strstr(n->s, needle) != NULL;
}

// Returns the number right after `needle`, or -1
static int numberAfter(const Name* n, const char* needle) {
    const char* p = strstr(n->s, needle);
    if (!p) {
        return -1;
    }
    p += strlen(needle);
    if (!isdigit((unsigned char) *p)) {
        return -1;
    }
    return atoi(p);
}

// Intel generation of a Core iX CPU from "13th gen" prefix or the model number (i7-8700K, i7-1165G7, i9-13900K)
static int intelCoreGen(const Name* n, const char* tier /* "i7" */) {
    const char* gen = strstr(n->s, "th gen");
    if (gen) {
        const char* p = gen;
        while (p > n->s && isdigit((unsigned char) p[-1])) {
            --p;
        }
        if (p != gen) {
            return atoi(p);
        }
    }

    char key[8];
    snprintf(key, sizeof(key), "%s-", tier);
    const char* p = strstr(n->s, key);
    if (!p) {
        return 1; // "core i5 m 520"
    }
    p += strlen(key);
    int digits = 0;
    while (isdigit((unsigned char) p[digits])) {
        ++digits;
    }
    switch (digits) {
        case 3: return 1;
        case 4: {
            // i7-1165G7 -> 11, i7-8700 -> 8
            const int firstTwo = atoi(p) / 100;
            return firstTwo >= 10 && firstTwo <= 15 ? firstTwo : atoi(p) / 1000;
        }
        case 5: return atoi(p) / 1000;
        default: return 1;
    }
}

static const char* intelCoreI(const Name* n) {
    static const char* tiers[] = {"i3", "i5", "i7", "i9"};
    int t = -1;
    for (int i = 0; i < 4; ++i) {
        char key[8];
        snprintf(key, sizeof(key), "core %s", tiers[i]);
        if (has(n, key)) {
            t = i;
            break;
        }
    }
    if (t < 0) {
        return NULL;
    }

    int gen = intelCoreGen(n, tiers[t]);
    // clang-format off
    static const char* const names[4][6] = {
        // gen1, gen2-3, gen4-5, gen6-8, gen9, gen10; gen11+ handled below
        {"intel_core_i3_gen1", "intel_core_i3_gen2", "intel_core_i3_gen4", "intel_core_i3_gen6", "intel_core_i3_gen9", "intel_core_i3_gen10"},
        {"intel_core_i5_gen1", "intel_core_i5_gen2", "intel_core_i5_gen4", "intel_core_i5_gen6", "intel_core_i5_gen9", "intel_core_i5_gen10"},
        {"intel_core_i7_gen1_blue", "intel_core_i7_gen2", "intel_core_i7_gen4", "intel_core_i7_gen6", "intel_core_i7_gen9", "intel_core_i7_gen10"},
        {"intel_core_i9_x",    "intel_core_i9_x",    "intel_core_i9_x",    "intel_core_i9_x",    "intel_core_i9_gen9", "intel_core_i9_gen10"},
    };
    static const char* const gen11[4] = {"intel_core_i3_gen11", "intel_core_i5_gen11", "intel_core_i7_gen11", "intel_core_i9_gen11"};
    // clang-format on
    if (gen >= 11) {
        return gen11[t];
    }
    int style = gen <= 1 ? 0 : gen <= 3 ? 1 : gen <= 5 ? 2 : gen <= 8 ? 3 : gen == 9 ? 4 : 5;
    return names[t][style];
}

static const char* intelXeon(const Name* n) {
    if (has(n, "platinum")) return "intel_xeon_platinum";
    if (has(n, "gold")) return "intel_xeon_gold";
    if (has(n, "silver")) return "intel_xeon_silver";
    if (has(n, "bronze")) return "intel_xeon_bronze";
    if (has(n, "phi")) return "intel_xeon_phi";
    if (has(n, "xeon 6")) return "intel_xeon_6";
    if (has(n, "e3-") || has(n, "e5-") || has(n, "e7-") || has(n, "w-")) return "intel_xeon_gen4";
    return "intel_xeon_gen2";
}

static const char* intelCPU(const Name* n) {
    if (has(n, "xeon")) return intelXeon(n);

    if (has(n, "core ultra")) {
        if (has(n, "ultra x9")) return "intel_core_ultra_x9_series3";
        if (has(n, "ultra x7")) return "intel_core_ultra_x7_series3";
        int tier = numberAfter(n, "ultra ");
        // Series 3 models are 3xx (e.g. Core Ultra 7 365), series 1 / 2 are 1xx / 2xx
        const char* p = strstr(n->s, "ultra ");
        const char* model = p ? strchr(p + 6, ' ') : NULL;
        bool series3 = model && model[1] == '3' && isdigit((unsigned char) model[2]) && isdigit((unsigned char) model[3]);
        switch (tier) {
            case 5: return series3 ? "intel_core_ultra_5_series3" : "intel_core_ultra_5";
            case 7: return series3 ? "intel_core_ultra_7_series3" : "intel_core_ultra_7";
            case 9: return series3 ? "intel_core_ultra_9_series3" : "intel_core_ultra_9";
            default: return "intel_core_ultra";
        }
    }

    const char* r = intelCoreI(n);
    if (r) return r;

    switch (numberAfter(n, "core ")) { // Intel Core 3/5/7/9 (2024+)
        case 3: return "intel_core_3";
        case 5: return "intel_core_5";
        case 7: return "intel_core_7";
        case 9: return "intel_core_9";
    }

    if (has(n, "core2") || has(n, "core 2")) {
        if (has(n, "extreme")) return "intel_core2_extreme";
        if (has(n, "quad")) return "intel_core2_quad";
        return "intel_core2_duo";
    }
    if (has(n, "core duo")) return "intel_core_duo";
    if (has(n, "core solo")) return "intel_core_solo";
    if (has(n, "core m")) return "intel_core_m";

    if (has(n, "atom")) {
        if (has(n, " x7")) return "intel_atom_x7";
        if (has(n, " x5")) return "intel_atom_x5";
        if (has(n, " x3")) return "intel_atom_x3";
        return "intel_atom_gen4";
    }

    if (has(n, "pentium")) {
        if (has(n, "gold") || has(n, "silver")) return "intel_pentium_gold";
        if (has(n, "pentium pro")) return "intel_pentium_pro";
        if (has(n, "pentium 4") && has(n, " ht")) return "intel_pentium_4_ht";
        if (has(n, "pentium 4")) return "intel_pentium_4";
        if (has(n, "pentium d")) return "intel_pentium_d";
        if (has(n, "pentium m")) return "intel_pentium_m";
        if (has(n, "pentium iii")) return "intel_pentium_iii";
        if (has(n, "pentium ii")) return "intel_pentium_ii";
        if (has(n, "mmx")) return "intel_pentium_mmx";
        if (has(n, " cpu g") || has(n, " cpu n") || has(n, " cpu j") || has(n, " gold")) return "intel_pentium_gen6";
        return "intel_pentium_inside";
    }
    if (has(n, "celeron")) {
        if (has(n, "celeron d")) return "intel_celeron_d";
        if (has(n, "celeron m")) return "intel_celeron_m";
        if (has(n, "dual-core") || has(n, "dual core")) return "intel_celeron_dual_core";
        if (has(n, " cpu g") || has(n, " cpu n") || has(n, " cpu j")) return "intel_celeron_gen6";
        return "intel_celeron_old";
    }
    if (has(n, "itanium")) return has(n, "itanium 2") ? "intel_itanium2" : "intel_itanium";
    if (has(n, "80486") || has(n, "i486")) return "intel_i486";
    if (has(n, "80386") || has(n, "i386")) return "intel_i386";
    return "intel";
}

static const char* amdRyzen(const Name* n) {
    int model = numberAfter(n, "ryzen "); // 5 in "ryzen 5 5600x"
    const char* p = strstr(n->s, "ryzen ");
    p += 6;
    // "ryzen 9 9950x" -> 9950
    const char* m = strchr(p, ' ');
    int series = m ? atoi(m + 1) : 0;
    bool isNew = series >= 9000;

    if (has(n, "ryzen ai")) {
        if (has(n, "max+")) return "amd_ryzen_ai_max_plus";
        if (has(n, "max")) return "amd_ryzen_ai_max";
        if (has(n, "pro")) return "amd_ryzen_ai_pro";
        switch (numberAfter(n, "ryzen ai ")) {
            case 9: return has(n, " hx") ? "amd_ryzen_ai_9_hx" : "amd_ryzen_ai_9";
            case 7: return "amd_ryzen_ai_7";
            case 5: return "amd_ryzen_ai_5";
        }
        return "amd_ryzen_ai_9";
    }
    if (has(n, "ryzen z2")) return has(n, "extreme") ? "amd_ryzen_z2_extreme" : "amd_ryzen_z2";
    if (has(n, "ryzen z1")) return has(n, "extreme") ? "amd_ryzen_z1_extreme" : "amd_ryzen_z1";
    if (has(n, "threadripper")) return isNew ? "amd_ryzen_threadripper_new" : "amd_ryzen_threadripper";
    switch (model) {
        case 9: return isNew ? "amd_ryzen_9_new" : "amd_ryzen_9";
        case 7: return isNew ? "amd_ryzen_7_new" : "amd_ryzen_7";
        case 5: return isNew ? "amd_ryzen_5_new" : "amd_ryzen_5";
        case 3: return isNew ? "amd_ryzen_3_new" : "amd_ryzen_3";
    }
    return isNew ? "amd_ryzen_new" : "amd_ryzen";
}

static const char* amdCPU(const Name* n) {
    if (has(n, "ryzen")) return amdRyzen(n);
    if (has(n, "epyc")) return numberAfter(n, "epyc ") >= 9000 ? "amd_epyc_new" : "amd_epyc";
    if (has(n, "opteron")) return has(n, "opteron x") ? "amd_opteron_x" : "amd_opteron_64";
    if (has(n, "athlon")) {
        if (has(n, "64 fx")) return "amd_athlon_64_fx";
        if (has(n, "64 x2")) return "amd_athlon_64_x2";
        if (has(n, "athlon 64")) return "amd_athlon_64";
        if (has(n, "ii x2")) return "amd_athlon_ii_x2";
        if (has(n, "athlon ii")) return "amd_athlon_ii";
        if (has(n, "athlon xp")) return "amd_athlon_xp";
        if (has(n, "athlon mp")) return "amd_athlon_mp";
        if (has(n, "neo")) return "amd_athlon_neo";
        if (has(n, "gold") || has(n, "silver") || has(n, "athlon 3") || has(n, "athlon 2") || has(n, "athlon 7")) return "amd_athlon_ryzen";
        return "amd_athlon";
    }
    if (has(n, "phenom")) {
        if (has(n, "ii x6")) return "amd_phenom_ii_x6";
        if (has(n, "ii x4")) return "amd_phenom_ii_x4";
        if (has(n, "ii x2")) return "amd_phenom_ii_x2";
        if (has(n, "phenom ii")) return "amd_phenom_ii";
        if (has(n, "phenom fx")) return "amd_phenom_fx";
        if (has(n, " x3")) return "amd_phenom_x3";
        return "amd_phenom";
    }
    if (has(n, "sempron")) return "amd_sempron";
    if (has(n, "turion")) {
        if (has(n, "ultra")) return "amd_turion_ultra";
        if (has(n, " x2")) return "amd_turion_64_x2";
        return "amd_turion_64";
    }
    if (has(n, "duron")) return "amd_duron";
    if (has(n, "fx-") || has(n, "fx ")) return "amd_fx";
    if (has(n, "k6-2")) return "amd_k6_2";
    if (has(n, "k6-3") || has(n, "k6-iii")) return "amd_k6_3";
    if (has(n, "k6")) return "amd_k6";
    if (has(n, "k5")) return "amd_k5";
    if (has(n, "am5x86")) return "amd_am5x86";
    if (has(n, "am486")) return "amd_am486";
    if (has(n, "am386")) return "amd_am386";
    static const struct {
        const char* key;
        const char* logo;
    } apu[] = {
        {" a12-", "amd_a12"}, {" a10-", "amd_a10"}, {" a8-", "amd_a8"}, {" a6-", "amd_a6"}, {" a4-", "amd_a4"},
        {" e2-", "amd_e2"},   {" e1-", "amd_e1"},   {"embedded g", "amd_embedded_g"}, {"embedded r", "amd_embedded_r"},
    };
    for (size_t i = 0; i < sizeof(apu) / sizeof(apu[0]); ++i) {
        if (has(n, apu[i].key)) return apu[i].logo;
    }
    if (has(n, "radeon")) return "amd_radeon_graphics";
    return "amd";
}

static const char* viaCPU(const Name* n) {
    if (has(n, "c7-m")) return "via_c7_m";
    if (has(n, "c7-d")) return "via_c7_d";
    if (has(n, "c7")) return "via_c7";
    if (has(n, "c3")) return "via_c3";
    if (has(n, "eden x4")) return "via_eden_x4";
    if (has(n, "eden x2")) return "via_eden_x2";
    if (has(n, "eden")) return "via_eden";
    if (has(n, "nano x2")) return "via_nano_x2";
    if (has(n, "nano")) return "via_nano";
    if (has(n, "quadcore")) return "via_quadcore";
    if (has(n, "antaur")) return "via_antaur";
    return "via";
}

static const char* cpuBaseName(const char* name) {
    if (!name || !*name) return NULL;
    Name n = normalize(name);

    if (has(&n, "apple")) {
        for (const char* gen : { "m1", "m2", "m3", "m4", "m5" }) {
            if (has(&n, gen)) {
                static char buf[16];
                snprintf(buf, sizeof(buf), "apple_%s", gen);
                return buf;
            }
        }
    }
    if (has(&n, "intel")) return intelCPU(&n);
    if (has(&n, "amd") || has(&n, "ryzen") || has(&n, "athlon")) return amdCPU(&n);
    if (has(&n, "via ") || has(&n, "centaur")) return has(&n, "centaur") && !has(&n, "via") ? "centaur" : viaCPU(&n);
    if (has(&n, "zhaoxin") || has(&n, "kaixian")) return "zhaoxin";
    if (has(&n, "hygon")) return "hygon";
    if (has(&n, "cyrix")) {
        if (has(&n, "6x86mx") || has(&n, "686mx")) return "cyrix_686mx";
        if (has(&n, "6x86")) return "cyrix_6x86";
        if (has(&n, "5x86")) return "cyrix_5x86";
        if (has(&n, "mediagx") || has(&n, "gx")) return "cyrix_mediagx";
        if (has(&n, "mii")) return "cyrix_mii";
        return "cyrix";
    }
    if (has(&n, "snapdragon") || has(&n, "qualcomm") || has(&n, "oryon")) return "qualcomm_snapdragon";
    if (has(&n, "transmeta") || has(&n, "efficeon")) return "transmeta_efficeon";
    if (has(&n, "winchip") || has(&n, "idt")) return "idt_winchip";
    if (has(&n, "vortex86")) return "dmp_vortex86dx";
    if (has(&n, "nexgen")) return "nexgen_nx586";
    if (has(&n, "rise")) return "rise";
    if (has(&n, "umc")) return "umc";
    if (has(&n, "ibm") || has(&n, "powerpc")) return "ibm";
    return NULL;
}

static const char* gpuBaseName(const char* name) {
    Name n = normalize(name ? name : "");
    Name v = {};

    if (has(&v, "nvidia") || has(&n, "nvidia") || has(&n, "geforce")) {
        if (has(&n, "rtx")) return "nvidia_geforce_rtx";
        if (has(&n, "gtx")) return "nvidia_geforce_gtx";
        return "nvidia";
    }
    if (has(&v, "intel") || has(&n, "intel")) {
        if (has(&n, "iris pro")) return "intel_iris_pro_graphics";
        if (has(&n, "iris xe") || has(&n, "iris(r) xe") || has(&n, "iris xe")) return "intel_iris_xe_graphics";
        if (has(&n, "arc")) return has(&n, "arc graphics") ? "intel_arc_graphics_ultra" : "intel_arc_graphics";
        return "intel_graphics";
    }
    if (has(&v, "amd") || has(&v, "ati") || has(&n, "radeon")) {
        if (has(&n, "ati ") || has(&n, "ati-")) return "ati_radeon";
        return "amd_radeon_graphics";
    }
    if (has(&v, "qualcomm") || has(&n, "adreno")) return "qualcomm_snapdragon";
    if (has(&v, "glenfly") || has(&n, "glenfly")) return "glenfly";
    if (has(&v, "zhaoxin")) return "zhaoxin";
    if (has(&n, "3dfx") || has(&n, "voodoo")) return "3dfx";
    return NULL;
}


static std::string toAsset(const char* dir, const char* baseName) {
    std::string r = std::string(dir) + "/" + baseName + ".png";
    std::replace(r.begin(), r.end(), '_', '-');
    return r;
}

static std::string lowercase(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char) std::tolower(c); });
    return s;
}

static bool containsAny(const std::string& haystack, std::initializer_list<const char*> needles) {
    const auto lower = lowercase(haystack);
    return std::any_of(needles.begin(), needles.end(), [&](const char* n) { return lower.find(n) != std::string::npos; });
}

hw_logo::Logo hw_logo::os(const std::string& name) {
    // clang-format off
    static const struct { std::initializer_list<const char*> keys; const char* asset; const char* caption; } table[] = {
        { { "fedora" }, "fedora", "Fedora" },
        { { "cachyos" }, "cachyos", "CachyOS" },
        { { "manjaro" }, "manjaro", "Manjaro" },
        { { "arch" }, "arch", "Arch Linux" },
        { { "mint" }, "linuxmint", "Linux Mint" },
        { { "pop!_os", "pop_os", "pop os" }, "popos", "Pop!_OS" },
        { { "ubuntu" }, "ubuntu", "Ubuntu" },
        { { "debian" }, "debian", "Debian" },
        { { "opensuse", "suse" }, "opensuse", "openSUSE" },
        { { "nixos" }, "nixos", "NixOS" },
        { { "kali" }, "kali", "Kali Linux" },
        { { "gentoo" }, "gentoo", "Gentoo" },
        { { "windows 11" }, "windows11", "Windows 11" },
        { { "windows 10" }, "windows10", "Windows 10" },
        { { "windows 7" }, "windows7", "Windows 7" },
        { { "windows" }, "windows10", "Windows" },
        { { "macos", "mac os", "darwin" }, "macos", "macOS" },
        { { "linux" }, "linux", "Linux" },
    };
    // clang-format on
    for (const auto& e : table) {
        if (containsAny(name, e.keys)) {
            return { std::string("logo/") + e.asset + ".png", e.caption };
        }
    }
    return {};
}

hw_logo::Logo hw_logo::cpu(const std::string& name) {
    if (const char* base = cpuBaseName(name.c_str())) {
        return { toAsset("cpu", base), {} };
    }
    return { "cpu/generic.png", {} };
}

hw_logo::Logo hw_logo::gpu(const std::string& name) {
    const char* base = gpuBaseName(name.c_str());
    if (!base) {
        return {};
    }
    // "NVIDIA GeForce RTX 4090" -> "RTX 4090"
    std::string caption = name;
    const auto lower = lowercase(name);
    for (const char* marker : { "geforce", "radeon", "intel" }) {
        if (const auto at = lower.find(marker); at != std::string::npos) {
            caption = name.substr(at + strlen(marker));
            break;
        }
    }
    const auto first = caption.find_first_not_of(' ');
    caption = first == std::string::npos ? std::string() : caption.substr(first, caption.find_last_not_of(' ') - first + 1);
    return { toAsset("gpu", base), caption };
}
