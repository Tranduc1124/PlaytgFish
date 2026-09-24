#import "Touch.hpp"

#import <UIKit/UIKit.h>
#import <objc/runtime.h>

#include "../Core/log.hpp"
#include "Gui.hpp"
#include "imgui.h"

namespace PF::Touch {
namespace {

// --- gốc (implementation gốc của UIView) ---
void (*g_origBegan)(id, SEL, NSSet*, UIEvent*) = nullptr;
void (*g_origMoved)(id, SEL, NSSet*, UIEvent*) = nullptr;
void (*g_origEnded)(id, SEL, NSSet*, UIEvent*) = nullptr;
void (*g_origCancelled)(id, SEL, NSSet*, UIEvent*) = nullptr;

UIView* gameView() {
    UIWindow* window = nil;
    for (UIWindow* w in UIApplication.sharedApplication.windows) {
        if (w.isKeyWindow) { window = w; break; }
    }
    if (!window) window = UIApplication.sharedApplication.windows.firstObject;
    return window.rootViewController.view;
}

// Chuyển UIEvent -> chuột ImGui (port từ tphat/ImGuiDrawView.mm updateIOWithTouchEvent)
void feedImGui(UIView* view, UIEvent* event) {
    if (!event || !Gui::ready()) return;
    ImGuiIO& io = ImGui::GetIO();

    // vị trí chạm (đơn vị point, khớp DisplaySize của ImGui)
    UITouch* any = event.allTouches.anyObject;
    if (any) {
        const CGPoint p = [any locationInView:view ?: any.view];
        io.AddMousePosEvent(static_cast<float>(p.x), static_cast<float>(p.y));
    }

    // trạng thái nhấn: có touch nào chưa kết thúc/hủy
    BOOL down = NO;
    for (UITouch* t in event.allTouches) {
        if (t.phase != UITouchPhaseEnded && t.phase != UITouchPhaseCancelled) {
            down = YES;
            break;
        }
    }
    io.AddMouseButtonEvent(0, down ? true : false);
}

void hookBegan(id self, SEL _cmd, NSSet* touches, UIEvent* event) {
    feedImGui((UIView*)self, event);
    if (g_origBegan) g_origBegan(self, _cmd, touches, event);
}
void hookMoved(id self, SEL _cmd, NSSet* touches, UIEvent* event) {
    feedImGui((UIView*)self, event);
    if (g_origMoved) g_origMoved(self, _cmd, touches, event);
}
void hookEnded(id self, SEL _cmd, NSSet* touches, UIEvent* event) {
    feedImGui((UIView*)self, event);
    if (g_origEnded) g_origEnded(self, _cmd, touches, event);
}
void hookCancelled(id self, SEL _cmd, NSSet* touches, UIEvent* event) {
    feedImGui((UIView*)self, event);
    if (g_origCancelled) g_origCancelled(self, _cmd, touches, event);
}

int g_retry = 0;

} // namespace

void install() {
    UIView* view = gameView();
    if (!view) {
        if (g_retry++ < 20) {
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(3 * NSEC_PER_SEC)),
                           dispatch_get_main_queue(), ^{ install(); });
        } else {
            PF_LOG("[touch] khong tim thay view cua game, menu se khong click duoc");
        }
        return;
    }

    Class cls = [view class];
    PF_LOG("[touch] game view class = %s", class_getName(cls));

    struct Item {
        SEL sel;
        const char* label;
        void** slot;
    };
    const Item items[] = {
        {sel_registerName("touchesBegan:withEvent:"), "touchesBegan", (void**)&g_origBegan},
        {sel_registerName("touchesMoved:withEvent:"), "touchesMoved", (void**)&g_origMoved},
        {sel_registerName("touchesEnded:withEvent:"), "touchesEnded", (void**)&g_origEnded},
        {sel_registerName("touchesCancelled:withEvent:"), "touchesCancelled", (void**)&g_origCancelled},
    };

    IMP replacements[] = {(IMP)hookBegan, (IMP)hookMoved, (IMP)hookEnded,
                          (IMP)hookCancelled};

    for (size_t i = 0; i < sizeof(items) / sizeof(items[0]); ++i) {
        Method m = class_getInstanceMethod(cls, items[i].sel);
        if (!m) {
            PF_LOG("[touch] khong thay -%s", items[i].label);
            continue;
        }
        *items[i].slot = reinterpret_cast<void*>(method_getImplementation(m));
        method_setImplementation(m, replacements[i]);
        PF_LOG("[touch] hooked -[%s %s]", class_getName(cls), items[i].label);
    }
}

} // namespace PF::Touch
