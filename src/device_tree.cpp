#include "device_tree.h"

#include "cpu_frequency.h"
#include "cpu_power.h"
#include "cpu_temperature.h"
#include "cpu_usage.h"

using namespace ui;

Value& DeviceTreeWriter::touch(const AString& key) {
    auto it = mTree.find(key);
    if (it == mTree.end()) {
        it = mTree.emplace(key, _new<Value>()).first;
        mChanged = true;
    }
    it->second->touched = true;
    return *it->second;
}

void DeviceTreeWriter::set(const AString& key, AString text) {
    Value& node = touch(key);
    if (const auto* current = std::get_if<AString>(&node.value.raw); current != nullptr && *current == text) {
        return;
    }
    if (std::holds_alternative<Tree>(node.value.raw)) {
        mChanged = true;   // a subtree has become a leaf: its rows are gone
    }
    node.value.raw = std::move(text);
    node.value.notify();
}

bool DeviceTreeWriter::finish() {
    for (auto it = mTree.begin(); it != mTree.end();) {
        if (it->second->touched) {
            it->second->touched = false;
            ++it;
        } else {
            it = mTree.erase(it);
            mChanged = true;
        }
    }
    return std::exchange(mChanged, false);
}

void ui::writeKnownIndicators(DeviceTreeWriter& root) {
    root.group("CPU", [](DeviceTreeWriter& cpu) {
        if (const auto v = cpuTemperatureCelsius()) cpu.set("Temperature", "{:.1f} \u00b0C"_format(*v));
        if (const auto v = coolantTemperatureCelsius()) cpu.set("Coolant temperature", "{:.1f} \u00b0C"_format(*v));
        if (const auto v = vrmTemperatureCelsius()) cpu.set("VRM temperature", "{:.1f} \u00b0C"_format(*v));
        if (const auto v = pumpRpm()) cpu.set("Pump", "{:.0f} RPM"_format(*v));
        if (const auto v = cpuPowerWatts()) cpu.set("Power", "{:.1f} W"_format(*v));
        if (const auto v = cpuFrequencyGHz()) cpu.set("Frequency", "{:.2f} GHz"_format(*v));
        if (const auto v = cpuUsageFraction()) cpu.set("Usage", "{:.0f} %"_format(*v * 100.f));
    });
}
