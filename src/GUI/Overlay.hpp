#pragma once

#import <Metal/Metal.h>

namespace PF::Overlay {

// Cài hook lên frame present của game để vẽ ImGui mỗi frame.
void install();

bool ready();

void shutdown();

} // namespace PF::Overlay
