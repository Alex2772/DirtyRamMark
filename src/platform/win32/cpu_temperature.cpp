#include <algorithm>
#include <memory>
#include <windows.h>
#include <comdef.h>
#include <wbemidl.h>
#include <cpu_temperature.h>
#include "pdh_counter.h"

namespace {

struct ComRelease {
    void operator()(IUnknown* p) const { p->Release(); }
};
template <class T>
using ComPtr = std::unique_ptr<T, ComRelease>;

// ACPI thermal zones through WMI (root\wmi). Needs administrator rights, hence only a fallback.
AOptional<float> wmiThermalZoneCelsius() {
    static bool initialized = false;
    static ComPtr<IWbemServices> services;
    if (!initialized) {
        initialized = true;
        // RPC_E_CHANGED_MODE means COM is already up in another apartment model, which is fine for us
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE,
                             nullptr, EOAC_NONE, nullptr);   // may fail if already set; harmless
        IWbemLocator* locator = nullptr;
        if (FAILED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator,
                                    reinterpret_cast<void**>(&locator)))) {
            return std::nullopt;
        }
        ComPtr<IWbemLocator> locatorGuard(locator);
        IWbemServices* raw = nullptr;
        if (FAILED(locator->ConnectServer(_bstr_t(L"ROOT\\WMI"), nullptr, nullptr, nullptr, 0, nullptr, nullptr,
                                          &raw))) {
            return std::nullopt;
        }
        services.reset(raw);
        CoSetProxyBlanket(raw, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL,
                          RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
    }
    if (!services) {
        return std::nullopt;
    }

    IEnumWbemClassObject* rawEnumerator = nullptr;
    if (FAILED(services->ExecQuery(_bstr_t(L"WQL"),
                                   _bstr_t(L"SELECT CurrentTemperature FROM MSAcpi_ThermalZoneTemperature"),
                                   WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr,
                                   &rawEnumerator))) {
        return std::nullopt;
    }
    ComPtr<IEnumWbemClassObject> enumerator(rawEnumerator);

    AOptional<float> hottest;
    for (;;) {
        IWbemClassObject* rawObject = nullptr;
        ULONG returned = 0;
        if (FAILED(enumerator->Next(WBEM_INFINITE, 1, &rawObject, &returned)) || returned == 0) {
            break;
        }
        ComPtr<IWbemClassObject> object(rawObject);
        VARIANT value;
        VariantInit(&value);
        if (SUCCEEDED(object->Get(L"CurrentTemperature", 0, &value, nullptr, nullptr)) && value.vt == VT_I4) {
            // tenths of Kelvin
            const float celsius = float(value.lVal) / 10.f - 273.15f;
            if (celsius > 0.f && celsius < 150.f) {
                hottest = std::max(hottest.valueOr(celsius), celsius);
            }
        }
        VariantClear(&value);
    }
    return hottest;
}

}   // namespace

AOptional<float> cpuTemperatureCelsius() {
    // No driver, so no real per-core / package sensors: the hottest ACPI thermal zone is the closest thing.
    static PdhCounterArray zones(L"\\Thermal Zone Information(*)\\High Precision Temperature");   // tenths of Kelvin
    AOptional<float> hottest;
    for (const auto& v : zones.values()) {
        const float celsius = float(v.value) / 10.f - 273.15f;
        if (celsius > 0.f && celsius < 150.f) {
            hottest = std::max(hottest.valueOr(celsius), celsius);
        }
    }
    if (hottest) {
        return hottest;
    }
    return wmiThermalZoneCelsius();
}
