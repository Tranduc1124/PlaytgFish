#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <dlfcn.h>

#include "log.hpp"
#include "target.hpp"

// Resolve các hàm export của libil2cpp bằng tên (KHÔNG phụ thuộc offset).
// Kết quả: đổi version game mà tên class/method giữ nguyên thì code vẫn chạy.
//
//   auto m = PF::Il2Cpp::resolve("FishingManager", "CheckBite", "", 0);
//   PF::hook("FishingManager::CheckBite", m.fnptr, (void*)detour, &orig);
namespace PF::Il2Cpp {

// ---- Con trỏ hàm export (resolve một lần lúc init) ----
using fn_domain_get = void* (*)();
using fn_domain_get_assemblies = const void* (*)(void*, size_t*);
using fn_assembly_get_image = const void* (*)(const void*);
using fn_image_get_name = const char* (*)(const void*);
using fn_image_get_class_count = size_t (*)(const void*);
using fn_image_get_class = void* (*)(const void*, size_t);
using fn_class_get_name = const char* (*)(const void*);
using fn_class_get_namespace = const char* (*)(const void*);
using fn_class_get_method_count = size_t (*)(void*);
using fn_class_get_method = void* (*)(void*, size_t);
using fn_class_get_method_from_name = void* (*)(void*, const char*, int);
using fn_method_get_name = const char* (*)(const void*);
using fn_method_get_pointer = void* (*)(const void*);

inline fn_domain_get _domain_get = nullptr;
inline fn_domain_get_assemblies _domain_get_assemblies = nullptr;
inline fn_assembly_get_image _assembly_get_image = nullptr;
inline fn_image_get_name _image_get_name = nullptr;
inline fn_image_get_class_count _image_get_class_count = nullptr;
inline fn_image_get_class _image_get_class = nullptr;
inline fn_class_get_name _class_get_name = nullptr;
inline fn_class_get_namespace _class_get_namespace = nullptr;
inline fn_class_get_method_count _class_get_method_count = nullptr;
inline fn_class_get_method _class_get_method = nullptr;
inline fn_class_get_method_from_name _class_get_method_from_name = nullptr;
inline fn_method_get_name _method_get_name = nullptr;
inline fn_method_get_pointer _method_get_pointer = nullptr;

inline bool ready() {
    return _domain_get != nullptr && _domain_get_assemblies != nullptr;
}

inline bool init() {
    _domain_get = (fn_domain_get)dlsym(RTLD_DEFAULT, "il2cpp_domain_get");
    _domain_get_assemblies = (fn_domain_get_assemblies)dlsym(RTLD_DEFAULT, "il2cpp_domain_get_assemblies");
    _assembly_get_image = (fn_assembly_get_image)dlsym(RTLD_DEFAULT, "il2cpp_assembly_get_image");
    _image_get_name = (fn_image_get_name)dlsym(RTLD_DEFAULT, "il2cpp_image_get_name");
    _image_get_class_count = (fn_image_get_class_count)dlsym(RTLD_DEFAULT, "il2cpp_image_get_class_count");
    _image_get_class = (fn_image_get_class)dlsym(RTLD_DEFAULT, "il2cpp_image_get_class");
    _class_get_name = (fn_class_get_name)dlsym(RTLD_DEFAULT, "il2cpp_class_get_name");
    _class_get_namespace = (fn_class_get_namespace)dlsym(RTLD_DEFAULT, "il2cpp_class_get_namespace");
    _class_get_method_count = (fn_class_get_method_count)dlsym(RTLD_DEFAULT, "il2cpp_class_get_method_count");
    _class_get_method = (fn_class_get_method)dlsym(RTLD_DEFAULT, "il2cpp_class_get_method");
    _class_get_method_from_name =
        (fn_class_get_method_from_name)dlsym(RTLD_DEFAULT, "il2cpp_class_get_method_from_name");
    _method_get_name = (fn_method_get_name)dlsym(RTLD_DEFAULT, "il2cpp_method_get_name");
    _method_get_pointer = (fn_method_get_pointer)dlsym(RTLD_DEFAULT, "il2cpp_method_get_pointer");

    const bool ok = _domain_get && _domain_get_assemblies && _assembly_get_image &&
                    _image_get_class_count && _image_get_class && _class_get_name;
    PF_LOG("[il2cpp] init %s", ok ? "OK" : "PARTIAL (thiếu symbol export)");
    return ok;
}

// ------------------------------------------------------------------ //
//  Kiểu dữ liệu cho GUI
// ------------------------------------------------------------------ //
struct MethodRef {
    void* klass = nullptr;   // Il2CppClass*
    void* method = nullptr;  // MethodInfo*
    void* fnptr = nullptr;   // native code pointer (địa chỉ để hook)
    const char* name = nullptr;
};

struct ClassRef {
    void* klass = nullptr;
    const char* name = nullptr;
    const char* nsp = nullptr;
    const char* image = nullptr;
};

// Duyệt toàn bộ class trong domain, gọi callback với từng class
template <typename Fn>
inline void forEachClass(Fn&& callback) {
    if (!ready() || !_assembly_get_image || !_image_get_class_count || !_image_get_class) return;
    void* domain = _domain_get();
    if (!domain) return;
    size_t count = 0;
    const void* assemblies = _domain_get_assemblies(domain, &count);
    if (!assemblies) return;
    auto** arr = const_cast<void**>(reinterpret_cast<void* const*>(assemblies));
    for (size_t i = 0; i < count; ++i) {
        const void* image = _assembly_get_image(arr[i]);
        if (!image) continue;
        const size_t n = _image_get_class_count(image);
        for (size_t c = 0; c < n; ++c) {
            void* k = _image_get_class(image, c);
            if (!k) continue;
            const char* kn = _class_get_name ? _class_get_name(k) : nullptr;
            if (!kn) continue;
            const char* ksp = _class_get_namespace ? _class_get_namespace(k) : nullptr;
            const char* img = _image_get_name ? _image_get_name(image) : nullptr;
            callback(k, kn, ksp ? ksp : "", img ? img : "");
        }
    }
}

inline bool classMatches(const char* name, const char* nsp, const char* keyword) {
    if (!keyword || !*keyword) return true;
    if (name && strcasestr(name, keyword)) return true;
    if (nsp && strcasestr(nsp, keyword)) return true;
    return false;
}

// Tìm class theo tên (namespace có thể để rỗng)
inline void* findClass(const char* name, const char* nsp = nullptr) {
    void* found = nullptr;
    forEachClass([&](void* k, const char* kn, const char* ksp, const char*) {
        if (found) return;
        if (strcmp(kn, name) != 0) return;
        if (nsp && *nsp && strcmp(ksp, nsp) != 0) return;
        found = k;
    });
    return found;
}

inline void* findMethod(void* klass, const char* methodName, int argc = -1) {
    if (!klass || !methodName) return nullptr;
    if (_class_get_method_from_name) {
        if (void* m = _class_get_method_from_name(klass, methodName, argc)) return m;
    }
    if (!_class_get_method_count || !_class_get_method) return nullptr;
    const size_t n = _class_get_method_count(klass);
    for (size_t i = 0; i < n; ++i) {
        void* m = _class_get_method(klass, i);
        if (!m) continue;
        const char* mn = _method_get_name ? _method_get_name(m) : nullptr;
        if (mn && strcmp(mn, methodName) == 0) return m;
    }
    return nullptr;
}

inline void* methodPointer(void* method) {
    if (!method || !_method_get_pointer) return nullptr;
    return _method_get_pointer(method);
}

inline MethodRef resolve(const char* className, const char* methodName, const char* nsp = "", int argc = -1) {
    MethodRef ref;
    ref.klass = findClass(className, nsp);
    ref.method = findMethod(ref.klass, methodName, argc);
    ref.fnptr = methodPointer(ref.method);
    if (_method_get_name && ref.method) ref.name = _method_get_name(ref.method);
    PF_LOG("[il2cpp] %s::%s -> %p (fn=%p)", className, methodName, ref.method, ref.fnptr);
    return ref;
}

// Tìm tất cả class chứa keyword (dùng cho tab Explorer)
inline std::vector<ClassRef> searchClasses(const char* keyword, size_t limit = 300) {
    std::vector<ClassRef> out;
    forEachClass([&](void* k, const char* kn, const char* ksp, const char* img) {
        if (out.size() >= limit) return;
        if (!classMatches(kn, ksp, keyword)) return;
        out.push_back({k, kn, ksp, img});
    });
    return out;
}

// Liệt kê method của 1 class
inline std::vector<MethodRef> classMethods(void* klass, const char* keyword = nullptr, size_t limit = 500) {
    std::vector<MethodRef> out;
    if (!klass || !_class_get_method_count || !_class_get_method) return out;
    const size_t n = _class_get_method_count(klass);
    for (size_t i = 0; i < n && out.size() < limit; ++i) {
        void* m = _class_get_method(klass, i);
        if (!m) continue;
        const char* mn = _method_get_name ? _method_get_name(m) : nullptr;
        if (!mn) continue;
        if (keyword && *keyword && !strcasestr(mn, keyword)) continue;
        MethodRef ref;
        ref.klass = klass;
        ref.method = m;
        ref.name = mn;
        ref.fnptr = methodPointer(m);
        out.push_back(ref);
    }
    return out;
}

} // namespace PF::Il2Cpp
