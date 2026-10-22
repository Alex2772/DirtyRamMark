#include <device_tree.h>

using namespace ui;

bool ui::queryDeviceTree(Tree& out) {
    DeviceTreeWriter root(out);
    writeKnownIndicators(root);
    return root.finish();
}
