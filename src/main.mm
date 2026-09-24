#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <unistd.h>

#include "Config.hpp"
#include "Core/il2cpp.hpp"
#include "Core/log.hpp"
#include "Core/target.hpp"
#include "Core/version.hpp"
#include "GUI/Gui.hpp"

namespace PF {
Config g_cfg; // định nghĩa duy nhất
}

// Dylib được chèn vào IPA nên constructor chạy khi app khởi động.
__attribute__((constructor)) static void playfish_entry() {
    @autoreleasepool {
        PF_LOG("==================================================");
        PF_LOG("PlayFish %s (%s) loaded, pid=%d", PF::kVersion, PF::kBuild, getpid());
    }

    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        PF_LOG("waiting for %s ...", PF::kGameImage);
        if (!PF::waitForImage(PF::kGameImage, 60000)) {
            PF_LOG("timeout: %s not loaded, abort", PF::kGameImage);
            return;
        }
        PF_LOG("%s base = 0x%lx", PF::kGameImage, PF::imageBase(PF::kGameImage));
        PF_LOG("%s base = 0x%lx", PF::kAppImage, PF::imageBase(PF::kAppImage));

        PF::Il2Cpp::init();

        // Khởi động GUI (gesture + overlay ImGui/Metal)
        PF::Gui::startup();
    });
}
