#import "Overlay.hpp"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <objc/runtime.h>

#import <mach/mach_time.h>
#import <vector>

#import "imgui.h"
#import "imgui_impl_metal.h"

#import "../Config.hpp"
#import "../Core/log.hpp"
#import "Gui.hpp"
#import "Menu.hpp"

namespace PF::Overlay {
namespace {

// ---------------------------------------------------------------- state
id<MTLDevice> g_device = nil;
id<CAMetalDrawable> g_currentDrawable = nil;
bool g_imguiReady = false;
bool g_hooked = false;
bool g_useCommitPath = false; // true nếu không hook được presentDrawable:
mach_timebase_info_data_t g_timebase{};

id<CAMetalDrawable> (*g_origNextDrawable)(id, SEL) = nullptr;
id (*g_origPresent)(id, SEL, id) = nullptr;
void (*g_origCommit)(id, SEL) = nullptr;

CFAbsoluteTime nowSeconds() {
    if (g_timebase.denom == 0) mach_timebase_info(&g_timebase);
    return static_cast<CFAbsoluteTime>(mach_absolute_time()) * g_timebase.numer / g_timebase.denom / 1e9;
}

// ---------------------------------------------------------------- imgui
void ensureImgui(id<MTLDevice> device) {
    if (!device) return;
    if (g_imguiReady && g_device == device) return;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // không ghi config ra sandbox
    io.LogFilename = nullptr;

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 8.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 6.0f;
    style.GrabRounding = 6.0f;
    style.ScrollbarRounding = 6.0f;
    style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
    style.WindowPadding = ImVec2(10.0f, 10.0f);
    style.ItemSpacing = ImVec2(8.0f, 7.0f);

    if (!ImGui_ImplMetal_Init(device)) {
        PF_LOG("[overlay] ImGui_ImplMetal_Init failed");
        return;
    }
    g_device = device;
    g_imguiReady = true;
    PF_LOG("[overlay] ImGui ready (Metal backend)");
}

// Vẽ ImGui vào command buffer của game (sau tất cả pass của game => nằm trên cùng)
void renderInto(id<MTLCommandBuffer> commandBuffer, id<CAMetalDrawable> drawable) {
    if (!commandBuffer || !drawable) return;
    // Dự phòng: nếu hook nextDrawable chưa kịp init, lấy device từ command buffer
    if (!g_imguiReady) ensureImgui(commandBuffer.device);
    if (!g_imguiReady) return;

    @autoreleasepool {
        MTLRenderPassDescriptor* rpd = [MTLRenderPassDescriptor renderPassDescriptor];
        rpd.colorAttachments[0].texture = drawable.texture;
        rpd.colorAttachments[0].loadAction = MTLLoadActionLoad; // giữ nguyên hình game
        rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
        rpd.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);

        id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:rpd];

        ImGuiIO& io = ImGui::GetIO();

        static CFAbsoluteTime last = 0.0;
        CFAbsoluteTime t = nowSeconds();
        io.DeltaTime = (last > 0.0) ? static_cast<float>(t - last) : (1.0f / 60.0f);
        if (io.DeltaTime <= 0.0f || io.DeltaTime > 0.5f) io.DeltaTime = 1.0f / 60.0f;
        last = t;

        const CGFloat scale = drawable.layer.contentsScale > 0 ? drawable.layer.contentsScale : 2.0;
        io.DisplaySize = ImVec2(static_cast<float>(drawable.texture.width / scale),
                               static_cast<float>(drawable.texture.height / scale));
        io.DisplayFramebufferScale = ImVec2(static_cast<float>(scale), static_cast<float>(scale));

        ImGui_ImplMetal_NewFrame(rpd);
        Menu::draw();

        ImDrawData* data = ImGui::GetDrawData();
        if (data && data->CmdLists.Size > 0)
            ImGui_ImplMetal_RenderDrawData(data, commandBuffer, encoder);

        Gui::noteFrame();
        [encoder endEncoding];
    }
}

// ---------------------------------------------------------------- hooks
id<CAMetalDrawable> pf_nextDrawable(id self, SEL cmd) {
    id<CAMetalDrawable> drawable = g_origNextDrawable ? g_origNextDrawable(self, cmd) : nil;
    if (drawable) {
        g_currentDrawable = drawable;
        // CAMetalDrawable không có .device; device nằm ở layer
        ensureImgui(drawable.layer.device);
    }
    return drawable;
}

// Đường chính: game gọi presentDrawable: ngay trước commit,
// ta chèn pass ImGui vào cùng command buffer trước khi present.
id pf_presentDrawable(id self, SEL cmd, id drawable) {
    if (g_cfg.drawOverlay && drawable && g_imguiReady)
        renderInto((id<MTLCommandBuffer>)self, (id<CAMetalDrawable>)drawable);
    if (g_origPresent) return g_origPresent(self, cmd, drawable);
    return nil;
}

// Đường dự phòng: nếu không hook được presentDrawable: thì chèn trước commit.
void pf_commit(id self, SEL cmd) {
    if (!g_useCommitPath) {
        if (g_origCommit) g_origCommit(self, cmd);
        return;
    }
    if (g_cfg.drawOverlay && g_currentDrawable && g_imguiReady)
        renderInto((id<MTLCommandBuffer>)self, g_currentDrawable);
    g_currentDrawable = nil;
    if (g_origCommit) g_origCommit(self, cmd);
}

std::vector<Class> classesConforming(Protocol* proto, SEL sel) {
    std::vector<Class> out;
    int count = objc_getClassList(nullptr, 0);
    if (count <= 0) return out;
    // Dùng malloc buffer thô: objc_getClassList yêu cầu __unsafe_unretained
    // (mà std::vector<Class> dưới ARC lại là __strong -> không tương thích).
    Class* all = (Class*)malloc(sizeof(Class) * (size_t)count);
    if (!all) return out;
    count = objc_getClassList(all, count);
    for (int i = 0; i < count; ++i) {
        Class c = all[i];
        if (!class_conformsToProtocol(c, proto)) continue;
        if (sel && !class_getInstanceMethod(c, sel)) continue;
        out.push_back(c);
    }
    free(all);
    return out;
}

void hookNextDrawable() {
    Class layerCls = objc_getClass("CAMetalLayer");
    if (!layerCls) {
        PF_LOG("[overlay] CAMetalLayer not found yet");
        return;
    }
    SEL sel = sel_registerName("nextDrawable");
    Method m = class_getInstanceMethod(layerCls, sel);
    if (!m) return;
    g_origNextDrawable = reinterpret_cast<id<CAMetalDrawable> (*)(id, SEL)>(method_getImplementation(m));
    method_setImplementation(m, reinterpret_cast<IMP>(pf_nextDrawable));
    PF_LOG("[overlay] hooked -[CAMetalLayer nextDrawable]");
}

size_t hookPresentDrawable() {
    SEL sel = sel_registerName("presentDrawable:");
    Protocol* proto = @protocol(MTLCommandBuffer);
    size_t n = 0;
    for (Class c : classesConforming(proto, sel)) {
        Method m = class_getInstanceMethod(c, sel);
        if (!m) continue;
        g_origPresent = reinterpret_cast<id (*)(id, SEL, id)>(method_getImplementation(m));
        method_setImplementation(m, reinterpret_cast<IMP>(pf_presentDrawable));
        ++n;
    }
    return n;
}

size_t hookCommit() {
    SEL sel = sel_registerName("commit");
    Protocol* proto = @protocol(MTLCommandBuffer);
    size_t n = 0;
    for (Class c : classesConforming(proto, sel)) {
        Method m = class_getInstanceMethod(c, sel);
        if (!m) continue;
        g_origCommit = reinterpret_cast<void (*)(id, SEL)>(method_getImplementation(m));
        method_setImplementation(m, reinterpret_cast<IMP>(pf_commit));
        ++n;
    }
    return n;
}

} // namespace

void install() {
    if (g_hooked) return;

    hookNextDrawable();

    const size_t presentCount = hookPresentDrawable();
    if (presentCount > 0) {
        g_useCommitPath = false;
        PF_LOG("[overlay] hooked presentDrawable: on %zu class(es)", presentCount);
    } else {
        const size_t commitCount = hookCommit();
        g_useCommitPath = commitCount > 0;
        PF_LOG("[overlay] fallback: hooked commit on %zu class(es)", commitCount);
    }

    g_hooked = true;
    if (!presentCount && !hookCommit()) PF_LOG("[overlay] WARNING: không hook được frame present");
}

bool ready() {
    return g_imguiReady;
}

void shutdown() {
    if (!g_imguiReady) return;
    ImGui_ImplMetal_Shutdown();
    ImGui::DestroyContext();
    g_imguiReady = false;
}

} // namespace PF::Overlay
