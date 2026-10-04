#include <cstring>

#include <memory_info.h>

namespace {

constexpr uint8_t TYPE_MEMORY_ARRAY = 16;
constexpr uint8_t TYPE_MEMORY_DEVICE = 17;
constexpr uint8_t TYPE_END = 127;

/// One SMBIOS structure: formatted area plus its trailing string set.
struct Structure {
    uint8_t type;
    const uint8_t* data;   ///< formatted area, starts with the 4-byte header
    size_t length;
    AVector<AString> strings;

    template<typename T>
    std::optional<T> field(size_t offset) const {
        if (offset + sizeof(T) > length) {
            return std::nullopt;   // older SMBIOS versions have shorter structures
        }
        T value;
        std::memcpy(&value, data + offset, sizeof(T));
        return value;
    }

    AString string(size_t offset) const {
        auto index = field<uint8_t>(offset).value_or(0);
        return index >= 1 && index <= strings.size() ? strings[index - 1] : AString {};
    }
};

AString sizeText(uint64_t megabytes) {
    return megabytes % 1024 == 0 ? "{} GB"_format(megabytes / 1024) : "{} MB"_format(megabytes);
}

AString memoryTypeName(uint8_t type) {
    switch (type) {
        case 0x12: return "DDR";
        case 0x13: return "DDR2";
        case 0x18: return "DDR3";
        case 0x1a: return "DDR4";
        case 0x22: return "DDR5";
        default: return "";
    }
}

void parseMemoryArray(const Structure& s, AMap<AString, AString>& out) {
    // Maximum Capacity is in KB; 0x80000000 means the Extended Maximum Capacity (bytes) is used instead.
    uint64_t kilobytes = s.field<uint32_t>(0x07).value_or(0);
    if (kilobytes == 0x80000000ull) {
        kilobytes = s.field<uint64_t>(0x0f).value_or(0) / 1024;
    }
    if (kilobytes != 0) {
        out["Maximum Capacity"] = sizeText(kilobytes / 1024);
    }
    if (auto devices = s.field<uint16_t>(0x0d); devices && *devices != 0) {
        out["Number Of Devices"] = AString::number(int(*devices));
    }
}

AMap<AString, AString> parseMemoryDevice(const Structure& s) {
    AMap<AString, AString> device;
    auto set = [&](const char* key, const AString& value) {
        if (!memory_info::isPlaceholder(value)) {
            device[key] = value;
        }
    };

    // Size: 0 = empty socket, 0xffff = unknown, 0x7fff = see Extended Size (MB); bit 15 = KB granularity.
    if (auto size = s.field<uint16_t>(0x0c)) {
        if (*size == 0 || *size == 0xffff) {
            return {};
        }
        if (*size == 0x7fff) {
            set("Size", sizeText(s.field<uint32_t>(0x1c).value_or(0) & 0x7fffffff));
        } else if (*size & 0x8000) {
            set("Size", "{} kB"_format(*size & 0x7fff));
        } else {
            set("Size", sizeText(*size));
        }
    }
    set("Locator", s.string(0x10));
    set("Bank Locator", s.string(0x11));
    set("Type", memoryTypeName(s.field<uint8_t>(0x12).value_or(0)));

    // Speeds are in MT/s; 0xffff means the 32-bit extended field follows.
    auto speed = [&](size_t offset, size_t extendedOffset) -> uint32_t {
        auto value = s.field<uint16_t>(offset).value_or(0);
        return value == 0xffff ? s.field<uint32_t>(extendedOffset).value_or(0) & 0x7fffffff : value;
    };
    if (auto v = speed(0x15, 0x54)) {
        set("Speed", "{} MT/s"_format(v));
    }
    if (auto v = speed(0x20, 0x58)) {
        set("Configured Memory Speed", "{} MT/s"_format(v));
    }
    set("Manufacturer", s.string(0x17));
    set("Serial Number", s.string(0x18));
    set("Part Number", s.string(0x1a));
    if (auto attributes = s.field<uint8_t>(0x1b); attributes && (*attributes & 0xf) != 0) {
        set("Rank", AString::number(int(*attributes & 0xf)));
    }
    return device;
}

}   // namespace

AJson::Object parseSmbiosTable(const AByteBuffer& table) {
    const auto* data = reinterpret_cast<const uint8_t*>(table.data());
    const size_t size = table.size();

    AMap<AString, AString> memoryArray;
    AVector<AMap<AString, AString>> devices;

    size_t offset = 0;
    while (offset + 4 <= size) {
        Structure s { data[offset], data + offset, data[offset + 1], {} };
        if (s.length < 4 || offset + s.length > size) {
            break;   // malformed
        }
        // the string set follows the formatted area and is terminated by an extra NUL
        size_t cursor = offset + s.length;
        while (cursor < size && data[cursor] != 0) {
            size_t end = cursor;
            while (end < size && data[end] != 0) {
                ++end;
            }
            s.strings << AString::fromUtf8(std::string_view(reinterpret_cast<const char*>(data + cursor), end - cursor)).trim();
            cursor = end + 1;
        }
        // no strings at all: the set is two NULs
        offset = cursor + (s.strings.empty() ? 2 : 1);

        if (s.type == TYPE_MEMORY_ARRAY) {
            parseMemoryArray(s, memoryArray);
        } else if (s.type == TYPE_MEMORY_DEVICE) {
            auto device = parseMemoryDevice(s);
            if (device.contains("Type") && memory_info::isDdr(device["Type"])) {
                devices << std::move(device);
            }
        } else if (s.type == TYPE_END) {
            break;
        }
    }
    return memory_info::assemble(memoryArray, devices);
}
