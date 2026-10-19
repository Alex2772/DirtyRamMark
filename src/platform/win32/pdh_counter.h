#pragma once

#include <vector>
#include <string>
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>

/**
 * @brief Wildcard PDH counter (e.g. `\Processor Information(*)\% Processor Performance`) holding its own query.
 * @details
 * Doesn't need elevated privileges. Rate counters need two samples, so the first values() call may return nothing.
 */
class PdhCounterArray {
public:
    explicit PdhCounterArray(const wchar_t* path) {
        if (PdhOpenQueryW(nullptr, 0, &mQuery) != ERROR_SUCCESS) {
            mQuery = nullptr;
            return;
        }
        if (PdhAddEnglishCounterW(mQuery, path, 0, &mCounter) != ERROR_SUCCESS) {
            PdhCloseQuery(mQuery);
            mQuery = nullptr;
            return;
        }
        PdhCollectQueryData(mQuery);   // baseline sample
    }
    ~PdhCounterArray() {
        if (mQuery) {
            PdhCloseQuery(mQuery);
        }
    }
    PdhCounterArray(const PdhCounterArray&) = delete;
    PdhCounterArray& operator=(const PdhCounterArray&) = delete;

    struct Value {
        std::wstring instance;
        double value;
    };

    /// Values of every instance that has a valid reading right now.
    std::vector<Value> values() {
        std::vector<Value> result;
        if (!mQuery || PdhCollectQueryData(mQuery) != ERROR_SUCCESS) {
            return result;
        }
        DWORD bytes = 0;
        DWORD count = 0;
        if (PdhGetFormattedCounterArrayW(mCounter, PDH_FMT_DOUBLE, &bytes, &count, nullptr) != PDH_MORE_DATA) {
            return result;
        }
        // instance names live in the same buffer right after the items
        std::vector<PDH_FMT_COUNTERVALUE_ITEM_W> items(bytes / sizeof(PDH_FMT_COUNTERVALUE_ITEM_W) + 1);
        if (PdhGetFormattedCounterArrayW(mCounter, PDH_FMT_DOUBLE, &bytes, &count, items.data()) != ERROR_SUCCESS) {
            return result;
        }
        for (DWORD i = 0; i < count; ++i) {
            if (items[i].FmtValue.CStatus == PDH_CSTATUS_VALID_DATA ||
                items[i].FmtValue.CStatus == PDH_CSTATUS_NEW_DATA) {
                result.push_back({items[i].szName, items[i].FmtValue.doubleValue});
            }
        }
        return result;
    }

private:
    PDH_HQUERY mQuery = nullptr;
    PDH_HCOUNTER mCounter = nullptr;
};
