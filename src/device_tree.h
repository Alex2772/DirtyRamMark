#pragma once

#include <variant>
#include <AUI/Common/AMap.h>
#include <AUI/Common/AProperty.h>
#include <AUI/Common/AString.h>
#include <AUI/Common/SharedPtrTypes.h>

namespace ui {

struct Value;

/// Name -> node. Sorted by name (AMap is an std::map).
using Tree = AMap<AString, AArc<Value>>;

/// A node of the device tree: either a ready-to-show leaf ("54.5 °C") or a subtree.
struct Value {
    AProperty<std::variant<AString /* value */, Tree /* subtree */>> value;

    /// Bookkeeping of DeviceTreeWriter (what has been written during the current update); views don't need it.
    bool touched = false;
};

/**
 * @brief Fills Tree in place, keeping the nodes that are still there.
 * @details
 * A tree is queried a few times a second, but it almost never changes its shape, and the views hold the nodes
 * (AArc<Value>) and observe their properties. So the writer
 *  - doesn't create a node if there is one with that name already: it only assigns the leaf, and only if the text differs;
 *  - notifies the property of a subtree only if a node was added to or removed from it (or changed its kind);
 *  - removes the nodes nobody has written to (finish()).
 *
 * Typical use:
 * @code
 * DeviceTreeWriter root(out);
 * root.group("hwmon0", [&](DeviceTreeWriter& w) { w.set("temp1", "42 °C"); });
 * return root.finish();
 * @endcode
 */
class DeviceTreeWriter {
public:
    explicit DeviceTreeWriter(Tree& tree) : mTree(tree) {}
    DeviceTreeWriter(const DeviceTreeWriter&) = delete;

    /// Writes (or updates) a leaf.
    void set(const AString& key, AString text);

    /// Writes (or updates) a subtree: `fill` gets a writer of it. Nodes `fill` hasn't written to are removed.
    template <typename Fill>
    void group(const AString& key, Fill&& fill) {
        Value& node = touch(key);
        auto* subtree = std::get_if<Tree>(&node.value.raw);
        bool kindChanged = false;
        if (subtree == nullptr) {
            node.value.raw = Tree {};
            subtree = &std::get<Tree>(node.value.raw);
            kindChanged = true;
        }
        DeviceTreeWriter child(*subtree);
        fill(child);
        const bool shapeChanged = child.finish() || kindChanged;
        if (shapeChanged) {
            mChanged = true;
            node.value.notify();
        }
    }

    /// Removes the nodes that haven't been written to since the previous update.
    /// @return true if a node was added or removed in this tree or below it: whoever owns the tree must notify its observers
    [[nodiscard]] bool finish();

private:
    Tree& mTree;
    bool mChanged = false;

    Value& touch(const AString& key);
};

/**
 * @brief Reads every indicator the platform can show (temperatures, fans, voltages, ...) into `out`.
 * @return true if the shape of `out` has changed (a node was added or removed at any depth); the caller then notifies
 * the observers of `out`. The subtrees whose shape has changed are notified by themselves too.
 * @details
 * Updates `out` in place (see DeviceTreeWriter), so it can be called often: that's how the "All Indicators" window
 * refreshes. Doesn't need elevated privileges. Implemented per platform (src/platform/<os>/device_tree.cpp).
 */
bool queryDeviceTree(Tree& out);

/// The indicators the gauges of the CPU stress test show (cpuTemperatureCelsius() and the like), as a group "CPU". For the
/// platforms that can't enumerate the sensors by themselves.
void writeKnownIndicators(DeviceTreeWriter& root);

}   // namespace ui
