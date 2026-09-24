#pragma once

#include <string>

namespace PF::paths {

// Thư mục Documents của app (nơi lưu file config, ví dụ offsets)
std::string documents();

// Đường dẫn file trong Documents
std::string dataFile(const std::string& name);

} // namespace PF::paths
