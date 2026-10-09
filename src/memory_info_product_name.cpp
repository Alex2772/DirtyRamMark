#include <memory_info.h>

#include <utility>

namespace {

bool isDigit(AChar c) { return c >= '0' && c <= '9'; }

/// Skips a run of digits starting at i.
size_t skipDigits(const AString& s, size_t i) {
    while (i < s.length() && isDigit(s[i])) {
        ++i;
    }
    return i;
}

/// First match of `table` (prefix -> name); the table must be ordered with longer prefixes first.
AString lookup(const AString& s, std::initializer_list<std::pair<AStringView, AStringView>> table) {
    for (const auto& [prefix, name] : table) {
        if (s.startsWith(prefix)) {
            return name;
        }
    }
    return {};
}

/// Kingston FURY (KF) and HyperX (HX):
///   <KF|HX><DDR gen><speed>(C|S)<CL><series><colour>[E][revision][A=RGB][K<n>]/<GB>
/// e.g. KF432C16BB1AK2/32, KF560C36BWEAK2, KF548S38IB, HX432C16PB3K2/16.
/// Series: KF - B(east), I(mpact), R(enegade); HX - F(ury), S(avage), P(redator), I(mpact).
AString kingston(const AString& pn) {
    bool fury = pn.startsWith("KF");
    bool hyperx = pn.startsWith("HX");
    if (!fury && !hyperx) {
        return {};
    }
    size_t i = 2;
    if (i >= pn.length() || !isDigit(pn[i])) {
        return {};
    }
    char gen = pn[i];
    size_t speedEnd = skipDigits(pn, i);
    // legacy form KF3600C18D4/32GX has the speed spelled out (4 digits) and no generation digit
    bool legacy = speedEnd - i >= 4;
    if (!legacy && gen != '3' && gen != '4' && gen != '5') {
        return {};
    }
    i = speedEnd;   // generation + speed
    if (i >= pn.length() || (pn[i] != 'C' && pn[i] != 'S')) {
        return {};
    }
    i = skipDigits(pn, i + 1);   // CAS latency
    if (i >= pn.length()) {
        return {};
    }

    AString series;
    if (fury) {
        switch (pn[i]) {
            case 'I': series = "Impact"; break;
            case 'R': series = "Renegade"; break;
            case 'B': series = "Beast"; break;
            default:
                if (!legacy) {
                    return {};
                }
                series = "Beast";
        }
    } else {
        switch (pn[i]) {
            case 'F': series = "Fury"; break;
            case 'S': series = "Savage"; break;
            case 'P': series = "Predator"; break;
            case 'I': series = "Impact"; break;
            default: return {};
        }
    }

    // the rest: [colour][E][revision][A][K<n>]; only RGB matters for the name
    size_t j = i + 1;
    if (j < pn.length() && AString("BRWS").contains(pn[j])) {
        ++j;
    }
    if (j < pn.length() && pn[j] == 'E') {
        ++j;
    }
    j = skipDigits(pn, j);
    bool rgb = j < pn.length() && pn[j] == 'A';

    AString result = fury ? "Kingston FURY " : "HyperX ";
    result += series;
    if (rgb) {
        result += " RGB";
    }
    return result;
}

/// Corsair: CM<line>[...]<capacity>G X<DDR gen> M<modules>..., e.g. CMK32GX4M2Z3200C16, CMH32GX5M2B6000C30.
AString corsair(const AString& pn) {
    if (!pn.startsWith("CM")) {
        return {};
    }
    // SO-DIMM lines have a 4-letter code
    if (auto name = lookup(pn, {{"CMSX", "Corsair Vengeance SODIMM"},
                                {"CMSO", "Corsair Value Select SODIMM"},
                                {"CMSA", "Corsair SODIMM for Apple"}});
        !name.empty()) {
        return name;
    }
    if (pn.length() < 3) {
        return {};
    }
    // DDR generation: the digit after "X" in "<capacity>GX<gen>"
    char gen = 0;
    if (auto gx = pn.find("GX"); gx != AString::npos && gx + 2 < pn.length() && isDigit(pn[gx + 2])) {
        gen = pn[gx + 2];
    }
    switch (pn[2]) {
        case 'K': return gen == '4' ? "Corsair Vengeance LPX" : "Corsair Vengeance";
        case 'H': return gen == '5' ? "Corsair Vengeance RGB" : "Corsair Vengeance RGB Pro SL";
        case 'W': return "Corsair Vengeance RGB Pro";
        case 'N': return "Corsair Vengeance RGB RT";
        case 'G': return "Corsair Vengeance RGB RS";
        case 'U': return "Corsair Vengeance LED";
        case 'D': return "Corsair Dominator Platinum";
        case 'T': return "Corsair Dominator Platinum RGB";
        case 'P': return gen == '5' ? "Corsair Dominator Titanium RGB" : "Corsair Dominator";
        case 'V': return "Corsair Value Select";
        case 'X': return "Corsair XMS";
        case 'Y': return "Corsair Vengeance Pro";
        case 'Z': return "Corsair Vengeance";
        default: return {};
    }
}

/// G.Skill: F<gen>-<speed>C<CL><kit>-<GB>G<series> (DDR4), F<gen>-<speed>J<timings><GB>G<kit>-<series> (DDR5).
/// The series code is what follows the last dash (DDR4: after the capacity), a colour letter may trail it,
/// e.g. F4-3600C16D-32GTZN, F5-6000J3038F16GX2-TZ5RK.
AString gskill(const AString& pn) {
    if (pn.length() < 4 || pn[0] != 'F' || !isDigit(pn[1]) || pn[2] != '-') {
        return {};
    }
    auto dash = pn.rfind('-');
    AStringView code = pn.substr(dash + 1);
    size_t i = skipDigits(code, 0);
    if (i > 0 && i < code.length() && code[i] == 'G') {
        ++i;   // DDR4: <capacity>G<series>
    }
    code = code.substr(i);
    auto name = lookup(code, {{"TR5N", "Trident Z5 Royal Neo"},
                              {"TR5", "Trident Z5 Royal"},
                              {"TZ5NR", "Trident Z5 Neo RGB"},
                              {"TZ5N", "Trident Z5 Neo"},
                              {"TZ5CR", "Trident Z5 CK RGB"},
                              {"TZ5C", "Trident Z5 CK"},
                              {"TZ5R", "Trident Z5 RGB"},
                              {"TZ5", "Trident Z5"},
                              {"RM5NR", "Ripjaws M5 Neo RGB"},
                              {"RM5R", "Ripjaws M5 RGB"},
                              {"RS5", "Ripjaws S5"},
                              {"FX5", "Flare X5"},
                              {"IS", "Aegis 5"},
                              {"TZNR", "Trident Z Neo RGB"},
                              {"TZN", "Trident Z Neo"},
                              {"TZR", "Trident Z RGB"},
                              {"TZ", "Trident Z"},
                              {"SXF", "Sniper X"},
                              {"SXK", "Sniper X"},
                              {"SXW", "Sniper X"},
                              {"FT", "Fortis"},
                              {"FX", "Flare X"},
                              {"VR", "Ripjaws V"},
                              {"VB", "Ripjaws V"},
                              {"VK", "Ripjaws V"},
                              {"VS", "Ripjaws V"},
                              {"VG", "Ripjaws V"},
                              {"RR", "Ripjaws 4"},
                              {"RB", "Ripjaws 4"},
                              {"RK", "Ripjaws 4"},
                              {"TX", "TridentX"}});
    return name.empty() ? name : "G.Skill " + name;
}

/// Crucial / Ballistix (needs the manufacturer: the prefixes are too short to be unambiguous):
/// BL[S|E|M|T]<GB>G<speed>C<CL>U<gen>.., CP<GB>G<speed>C<CL>U5<colour>, CT<GB>G<gen>(D|S)...
AString crucial(const AString& pn) {
    if (pn.startsWith("CP") && pn.length() > 2 && isDigit(pn[2])) {
        return "Crucial Pro";
    }
    if (pn.startsWith("BL")) {
        if (auto name = lookup(pn, {{"BLS", "Crucial Ballistix Sport"},
                                    {"BLE", "Crucial Ballistix Elite"},
                                    {"BLM", "Crucial Ballistix MAX"},
                                    {"BLT", "Crucial Ballistix Tactical"}});
            !name.empty()) {
            return name;
        }
        if (pn.length() > 2 && isDigit(pn[2])) {
            return "Crucial Ballistix";
        }
        return {};
    }
    if (pn.startsWith("CT")) {
        size_t i = skipDigits(pn, 2);
        if (i > 2 && i + 2 < pn.length() && pn[i] == 'G' && isDigit(pn[i + 1])) {
            AString ddr = "DDR";
            ddr += pn[i + 1];
            switch (pn[i + 2]) {
                case 'D': return "Crucial " + ddr + " DIMM";
                case 'S': return "Crucial " + ddr + " SODIMM";
                default: return {};
            }
        }
    }
    return {};
}

/// Team Group T-Force / Elite. DDR4 prefixes (TL.., TF..) and DDR5 prefixes (FL.., FF..).
AString teamgroup(const AString& pn) {
    return lookup(pn, {{"TLZGD", "Team T-Force Vulcan Z"},
                       {"TLZRD", "Team T-Force Vulcan Z"},
                       {"TF3D", "Team T-Force Delta RGB"},
                       {"TF4D", "Team T-Force Delta RGB"},
                       {"TF9D", "Team T-Force Delta TUF"},
                       {"FLRD", "Team T-Force Vulcan"},
                       {"FLGD", "Team T-Force Vulcan"},
                       {"FLBD", "Team T-Force Vulcan"},
                       {"FF3D", "Team T-Force Delta RGB"},
                       {"FF4D", "Team T-Force Delta RGB"},
                       {"TED", "Team Elite"}});
}

/// ADATA / XPG: AX<3|4|5>U<speed><capacity>G<CL>A-<series>, the series being D<colour/RGB letter><model>, e.g. AX4U320038G16A-DT50, AX5U6000C3016G-DCLARBK.
AString adata(const AString& pn) {
    if (!pn.startsWith("AX") || pn.length() < 4 || pn[3] != 'U') {
        return {};
    }
    auto dash = pn.find('-');
    if (dash == AString::npos) {
        return {};
    }
    AStringView suffix = pn.substr(dash + 1);
    if (suffix.startsWith("DCLA")) {
        return suffix.find("RB") != AStringView::npos ? "XPG Lancer RGB" : "XPG Lancer";
    }
    for (auto [needle, name] : std::initializer_list<std::pair<AStringView, AStringView>> {
             {"500G", "XPG Spectrix D500G"},
             {"50", "XPG Spectrix D50"},
             {"45G", "XPG Spectrix D45G"},
             {"41", "XPG Spectrix D41"},
             {"35", "XPG Gammix D35"},
             {"30", "XPG Gammix D30"},
             {"20", "XPG Gammix D20"},
             {"10", "XPG Gammix D10"},
         }) {
        if (suffix.find(needle) != AStringView::npos) {
            return name;
        }
    }
    return {};
}

/// Patriot Viper: PVS Steel, PVE Elite, PVV Venom, PV4 Viper 4; a trailing R after the line is RGB (PVSR416G320C8K).
AString patriot(const AString& pn) {
    return lookup(pn, {{"PVSR", "Patriot Viper Steel RGB"},
                       {"PVS", "Patriot Viper Steel"},
                       {"PVER", "Patriot Viper Elite RGB"},
                       {"PVE2", "Patriot Viper Elite II"},
                       {"PVE", "Patriot Viper Elite"},
                       {"PVVR", "Patriot Viper Venom RGB"},
                       {"PVV", "Patriot Viper Venom"},
                       {"PV4", "Patriot Viper 4"},
                       {"PSD", "Patriot Signature Line"},
                       {"PSP", "Patriot Signature Premium"}});
}

/// DRAM vendors' own (OEM) modules; only the family is reported (needs the manufacturer).
AString oem(const AString& pn, const AString& manufacturer) {
    if (manufacturer.contains("samsung")) {
        return lookup(pn, {{"M378A", "Samsung DDR4 UDIMM"},
                           {"M471A", "Samsung DDR4 SODIMM"},
                           {"M323R", "Samsung DDR5 UDIMM"},
                           {"M425R", "Samsung DDR5 SODIMM"},
                           {"M378B", "Samsung DDR3 UDIMM"},
                           {"M471B", "Samsung DDR3 SODIMM"}});
    }
    if (manufacturer.contains("hynix")) {
        return lookup(pn, {{"HMCG", "SK hynix DDR5"}, {"HMA", "SK hynix DDR4"}, {"HMT", "SK hynix DDR3"}});
    }
    if (manufacturer.contains("micron")) {
        return lookup(pn, {{"MTC", "Micron DDR5"}, {"MTA", "Micron DDR4"}});
    }
    return {};
}

}   // namespace

AString memory_info::productName(const AString& manufacturer, const AString& partNumber) {
    // SPD strings are often space-padded
    AString pn = partNumber.trim().uppercase();
    AString mf = manufacturer.lowercase();
    if (pn.empty()) {
        return {};
    }

    // part number prefixes below are specific enough to be matched without a (frequently "Unknown") manufacturer
    for (auto* rule : {kingston, corsair, gskill, teamgroup, adata, patriot}) {
        if (auto name = rule(pn); !name.empty()) {
            return name;
        }
    }
    if (mf.contains("crucial") || mf.contains("micron")) {
        if (auto name = crucial(pn); !name.empty()) {
            return name;
        }
    }
    return oem(pn, mf);
}
