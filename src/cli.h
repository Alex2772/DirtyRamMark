#pragma once

#include <optional>
#include <AUI/Common/AStringVector.h>
#include <AUI/Json/AJson.h>

/**
 * @brief Everything the app can tell about the memory, as one JSON object: {"version", "controller", "memory", "timings"}.
 * @details
 * A failing section is reported as {"error": "..."} instead of failing the whole report.
 */
AJson::Object collectReport();

/**
 * @brief Handles command line mode.
 * @return exit code if a CLI flag was handled (the GUI must not start); std::nullopt otherwise.
 * @details
 * `--json` prints the report to stdout and nothing else (logs are redirected to stderr). `--help` prints usage.
 */
std::optional<int> runCli(const AStringVector& args);
