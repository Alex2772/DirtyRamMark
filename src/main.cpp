#include <AUI/Platform/Entry.h>
#include <AUI/Platform/AWindow.h>
#include <AUI/Util/kAUI.h>
#include <AUI/ASS/AStylesheet.h>
#include <AUI/ASS/Property/BackgroundSolid.h>
#include <AUI/View/ATitlebar.h>
#if AUI_PLATFORM_LINUX
#include <AUI/Platform/linux/APlatformAbstractionOptions.h>
#endif

#if __has_include(<AUI/Remote/AHotCodeReload.h>)
#include <AUI/Remote/AHotCodeReload.h>
#endif

#include "background_blur.h"
#include "cli.h"
#include "showcase.h"
#include "AUI/Util/Declarative/Containers.h"

using namespace ass;

AUI_ENTRY {
    if (auto exitCode = runCli(args)) {
        return *exitCode;
    }

#if AUI_PLATFORM_LINUX
    APlatformAbstractionOptions::set({
        .initializationOrder = {
            APlatformAbstractionOptions::Adwaita1{},
            APlatformAbstractionOptions::Gtk4{},
            APlatformAbstractionOptions::X11{},
        },
    });
#endif

    auto window = _new<AWindow>(AWindow::Init {
        .title = "DirtyRamMark {}"_format(AUI_PP_STRINGIZE(AUI_CMAKE_PROJECT_VERSION)),
        .width = 450_dp,
        .height = 430_dp,
        .decoration = AWindow::Init::DecorationDefault {
            .buttons = {.minimize = true, .maximize = false, .close = true, },
        },
        .resizable = false,
    });



    window->setContents(declarative::Centered { ui::showcase(*window) } AUI_OVERRIDE_STYLE { Padding(0) });

    window->show();

#if AUI_PLATFORM_LINUX
    // the window is translucent only if the compositor blurs it, otherwise the text would be hard to read
    if (background_blur::enable(*window)) {
        window AUI_OVERRIDE_STYLE { BackgroundSolid { AColor(0xf0f0f0_rgb).transparentize(0.5f) } };
        // with the extended decoration the titlebar is drawn by AUI (the GTK one is hidden)
        AStylesheet::global().addRule({
            t<AView>(),
            FontRendering::ANTIALIASING,
        });
    }
#endif

    return 0;
};
