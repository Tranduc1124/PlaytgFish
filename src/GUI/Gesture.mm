#import "Gesture.hpp"

#import <UIKit/UIKit.h>

#include "../Config.hpp"
#include "../Core/log.hpp"

// ---------------------------------------------------------------------
//  Target cho gesture: UIKit yêu cầu target là ObjC object.
//  Khai báo ở global scope (không được đặt trong namespace C++).
// ---------------------------------------------------------------------
@interface PFGestureTarget : NSObject
+ (instancetype)shared;
- (void)handle:(id)sender;
@end

@implementation PFGestureTarget

+ (instancetype)shared {
    static PFGestureTarget* instance = nil;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        instance = [[PFGestureTarget alloc] init];
    });
    return instance;
}

- (void)handle:(id)sender {
    // UIKit gọi handler trên main thread
    PF::g_cfg.showMenu = !PF::g_cfg.showMenu;
    PF_LOG("[gesture] menu %s", PF::g_cfg.showMenu ? "mo" : "dong");
}

@end

namespace PF::Gesture {
namespace {

// Giữ strong reference cho gesture recognizer
UITapGestureRecognizer* g_threeFinger = nil;
UITapGestureRecognizer* g_twoFingerDouble = nil;
int g_retry = 0;

UIWindow* keyWindow() {
    UIApplication* app = UIApplication.sharedApplication;
    for (UIWindow* w in app.windows) {
        if (w.isKeyWindow) return w;
    }
    return app.windows.firstObject;
}

} // namespace

void install() {
    UIWindow* window = keyWindow();
    if (!window) {
        if (g_retry++ < 20) { // thử lại tối đa 20 lần (mỗi lần 3s)
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(3 * NSEC_PER_SEC)),
                           dispatch_get_main_queue(), ^{
                               install();
                           });
        } else {
            PF_LOG("[gesture] khong tim thay key window, bo qua gesture");
        }
        return;
    }

    if (!g_threeFinger) {
        g_threeFinger = [[UITapGestureRecognizer alloc] initWithTarget:[PFGestureTarget shared]
                                                               action:@selector(handle:)];
        g_threeFinger.numberOfTouchesRequired = 3;
        g_threeFinger.cancelsTouchesInView = NO; // không nuốt input của game
        g_threeFinger.delaysTouchesEnded = NO;
        [window addGestureRecognizer:g_threeFinger];
        PF_LOG("[gesture] da them: chạm 3 ngon để mo/dong menu");
    }

    if (!g_twoFingerDouble) {
        g_twoFingerDouble = [[UITapGestureRecognizer alloc] initWithTarget:[PFGestureTarget shared]
                                                                   action:@selector(handle:)];
        g_twoFingerDouble.numberOfTouchesRequired = 2;
        g_twoFingerDouble.numberOfTapsRequired = 2;
        g_twoFingerDouble.cancelsTouchesInView = NO;
        g_twoFingerDouble.delaysTouchesEnded = NO;
        [window addGestureRecognizer:g_twoFingerDouble];
        PF_LOG("[gesture] da them: chạm 2 ngon 2 lan (du phong)");
    }
}

} // namespace PF::Gesture
