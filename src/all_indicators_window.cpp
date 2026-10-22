#include <AUI/ASS/Property/BackgroundSolid.h>
#include <AUI/ASS/Property/Expanding.h>
#include <AUI/ASS/Property/FixedSize.h>
#include <AUI/ASS/Property/Padding.h>
#include <AUI/Common/ATimer.h>
#include <AUI/Platform/AWindow.h>
#include <AUI/Util/UIBuildingHelpers.h>
#include <AUI/Util/kAUI.h>
#include <AUI/View/AForEachUI.h>
#include <AUI/View/AScrollArea.h>
#include <range/v3/view/enumerate.hpp>

#include "all_indicators_window.h"
#include "device_tree.h"

using namespace ass;
using namespace declarative;
using namespace std::chrono_literals;

namespace {

constexpr float NAME_COLUMN_DP = 320.f;
constexpr float VALUE_COLUMN_DP = 220.f;
constexpr float INDENT_DP = 16.f;
constexpr AColor COLOR_ODD_ROW = AColor(0.f, 0.f, 0.f, 0.f);
constexpr AColor COLOR_EVEN_ROW = 0xe6e6e6_rgb;

/// A line of the table: the tree is shown flat, so that the zebra goes through the whole table and the list is a single
/// lazy AForEachUI.
struct Row {
    AString name;
    unsigned depth;
    AArc<ui::Value> node;

    bool operator==(const Row&) const = default;
};

void flatten(const ui::Tree& tree, unsigned depth, AVector<Row>& out) {
    for (const auto& [name, node] : tree) {
        out.push_back({ name, depth, node });
        if (const auto* subtree = std::get_if<ui::Tree>(&node->value.raw)) {
            flatten(*subtree, depth + 1, out);
        }
    }
}

struct State : AObject {
    AProperty<ui::Tree> lastReportedTree;
    AProperty<AVector<Row>> rows;
    AArc<ATimer> updateTimer = _new<ATimer>(1s);

    State() {
        connect(lastReportedTree.changed, me::rebuildRows);
        connect(updateTimer->fired, me::update);
        updateTimer->start();
        update();
    }

    void update() {
        // the tree is updated in place: the rows follow the values by themselves, only a new or a gone node needs a rebuild
        if (ui::queryDeviceTree(lastReportedTree.raw)) {
            lastReportedTree.notify();
        }
    }

private:
    void rebuildRows() {
        AVector<Row> result;
        flatten(lastReportedTree.raw, 0, result);
        rows = std::move(result);
    }
};

AString textOf(const std::variant<AString, ui::Tree>& value) {
    const auto* text = std::get_if<AString>(&value);
    return text ? *text : AString {};
}

_<AView> rowView(size_t index, const Row& row) {
    const float indent = INDENT_DP * float(row.depth);
    const auto node = row.node;
    return Horizontal {
      _new<AView>() AUI_OVERRIDE_STYLE { FixedSize { AMetric(indent, AMetric::T_DP), 1_px } },
      Label { row.name } AUI_OVERRIDE_STYLE { FixedSize { AMetric(NAME_COLUMN_DP - indent, AMetric::T_DP), {} } },
      Label { AUI_REACT(textOf(*node->value)) } AUI_OVERRIDE_STYLE { FixedSize { AMetric(VALUE_COLUMN_DP, AMetric::T_DP), {} } },
    } AUI_OVERRIDE_STYLE {
        BackgroundSolid { index % 2 == 0 ? COLOR_EVEN_ROW : COLOR_ODD_ROW },
        Padding { 2_dp, 8_dp },
    };
}

}   // namespace

void ui::openAllIndicatorsWindow(AWindow& parent) {
    auto window = _new<AWindow>(AWindow::Init {
      .title = "All Indicators",
      .width = 600_dp,
      .height = 700_dp,
      .parent = AWindow::Init::Parent { .window = parent },
    });
    auto state = _new<State>();
    window->setContents(Vertical {
      Horizontal {
        Label { "Indicator" } AUI_OVERRIDE_STYLE { FixedSize { AMetric(NAME_COLUMN_DP, AMetric::T_DP), {} } },
        Label { "Value" } AUI_OVERRIDE_STYLE { FixedSize { AMetric(VALUE_COLUMN_DP, AMetric::T_DP), {} } },
      } AUI_OVERRIDE_STYLE { Padding { 4_dp, 8_dp } },
      AScrollArea::Builder()
          .withContents(AUI_DECLARATIVE_FOR(i, ranges::views::enumerate(*state->rows), AVerticalLayout) {
              const auto& [index, row] = i;
              return rowView(index, row);
          })
          .build() AUI_OVERRIDE_STYLE { Expanding { 1, 1 } },
    });
    window->show();
}
