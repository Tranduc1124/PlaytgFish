#import "paths.hpp"

#import <Foundation/Foundation.h>

#include <string>

namespace PF::paths {

std::string documents() {
    static std::string cached;
    static bool resolved = false;
    if (resolved) return cached;
    resolved = true;
    @autoreleasepool {
        NSArray* urls = [[NSFileManager defaultManager] URLsForDirectory:NSDocumentDirectory
                                                                  inDomains:NSUserDomainMask];
        if (urls.count > 0)
            cached = [urls.firstObject path].UTF8String;
    }
    return cached;
}

std::string dataFile(const std::string& name) {
    const std::string dir = documents();
    if (dir.empty()) return name;
    return dir + "/" + name;
}

} // namespace PF::paths
