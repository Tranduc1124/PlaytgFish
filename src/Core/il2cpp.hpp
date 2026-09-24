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
// ---- Field API (quan trọng: offset field lấy theo TÊN => tự đổi theo version) ----
using fn_class_get_field_from_name = void* (*)(void*, const char*);
using fn_field_get_offset = size_t (*)(const void*);
using fn_field_static_get_value = void (*)(const void*, void*);
using fn_class_get_field_count = size_t (*)(void*);
using fn_class_get_field = void* (*)(void*, size_t);
using fn_field_get_name = const char* (*)(const void*);
using fn_field_get_type = const void* (*)(const void*);
using fn_type_get_name = char* (*)(const void*);
// ---- Object API ----
using fn_object_get_class = void* (*)(void*);

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
inline fn_class_get_field_from_name _class_get_field_from_name = nullptr;
inline fn_field_get_offset _field_get_offset = nullptr;
inline fn_field_static_get_value _field_static_get_value = nullptr;
inline fn_class_get_field_count _class_get_field_count = nullptr;
inline fn_class_get_field _class_get_field = nullptr;
inline fn_field_get_name _field_get_name = nullptr;
inline fn_field_get_type _field_get_type = nullptr;
inline fn_type_get_name _type_get_name = nullptr;
inline fn_object_get_class _object_get_class = nullptr;

inline bool ready() {
    return _domain_get != nullptr && _domain_get_assemblies != nullptr;
}

// Lọc con trỏ rác. User-space của tiến trình arm64 iOS: >= 4 GB, < 2^48,
// luôn align 8. Rác thường là số nhỏ (0x135, 0x8…) hoặc lệch align.
inline bool plausiblePtr(const void* p) {
    if (!p) return false;
    const uintptr_t v = reinterpret_cast<uintptr_t>(p);
    return v >= 0x100000000ull && v < 0x0001000000000000ull && (v & 0x7) == 0;
}

// IL2CPP domain đã dựng xong chưa.
//
// QUAN TRỌNG: ready() chỉ nói "đã dlsym được symbol" — điều đó xảy ra ngay
// khi UnityFramework vừa được load, TỚI RỒI TRƯỚC il2cpp_init(). Gọi
// domain_get_assemblies lúc đó trả về mảng rác, và bản thân code il2cpp
// deref con trỏ NULL -> SIGSEGV. Đã thấy thực tế trên iOS 27: app chết sau
// 0.4s, far=0x135, 3 frame trong UnityFramework, thread bootstrap của ta.
inline bool domainReady() {
    if (!ready() || !_domain_get_assemblies) return false;
    void* domain = _domain_get();
    if (!plausiblePtr(domain)) return false;
    size_t count = 0;
    const void* assemblies = _domain_get_assemblies(domain, &count);
    if (!assemblies || count == 0 || count > 4096) return false;
    auto** arr = const_cast<void**>(reinterpret_cast<void* const*>(assemblies));
    return plausiblePtr(arr[0]);
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

    _class_get_field_from_name =
        (fn_class_get_field_from_name)dlsym(RTLD_DEFAULT, "il2cpp_class_get_field_from_name");
    _field_get_offset = (fn_field_get_offset)dlsym(RTLD_DEFAULT, "il2cpp_field_get_offset");
    _field_static_get_value =
        (fn_field_static_get_value)dlsym(RTLD_DEFAULT, "il2cpp_field_static_get_value");
    _class_get_field_count = (fn_class_get_field_count)dlsym(RTLD_DEFAULT, "il2cpp_class_get_field_count");
    _class_get_field = (fn_class_get_field)dlsym(RTLD_DEFAULT, "il2cpp_class_get_field");
    _field_get_name = (fn_field_get_name)dlsym(RTLD_DEFAULT, "il2cpp_field_get_name");
    _field_get_type = (fn_field_get_type)dlsym(RTLD_DEFAULT, "il2cpp_field_get_type");
    _type_get_name = (fn_type_get_name)dlsym(RTLD_DEFAULT, "il2cpp_type_get_name");
    _object_get_class = (fn_object_get_class)dlsym(RTLD_DEFAULT, "il2cpp_object_get_class");

    const bool ok = _domain_get && _domain_get_assemblies && _assembly_get_image &&
                    _image_get_class_count && _image_get_class && _class_get_name;
    PF_LOG("[il2cpp] init %s (field_api=%s object_api=%s)", ok ? "OK" : "PARTIAL (thiếu symbol export)",
           _class_get_field_from_name ? "OK" : "NO", _object_get_class ? "OK" : "NO");
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
    if (!domainReady() || !_assembly_get_image || !_image_get_class_count || !_image_get_class) return;
    void* domain = _domain_get();
    size_t count = 0;
    const void* assemblies = _domain_get_assemblies(domain, &count);
    if (!assemblies || count == 0 || count > 4096) return;
    auto** arr = const_cast<void**>(reinterpret_cast<void* const*>(assemblies));
    for (size_t i = 0; i < count; ++i) {
        if (!plausiblePtr(arr[i])) continue;
        const void* image = _assembly_get_image(arr[i]);
        if (!plausiblePtr(image)) continue;
        const size_t n = _image_get_class_count(image);
        if (n > 200000) continue;  // số class vô lý = image chưa sẵn sàng
        for (size_t c = 0; c < n; ++c) {
            void* k = _image_get_class(image, c);
            if (!plausiblePtr(k)) continue;
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

// Resolve method từ Il2CppClass* đã có sẵn (nhanh, không cần tìm class)
inline MethodRef resolveByClass(void* klass, const char* methodName, int argc = -1) {
    MethodRef ref;
    ref.klass = klass;
    ref.method = findMethod(klass, methodName, argc);
    ref.fnptr = methodPointer(ref.method);
    if (_method_get_name && ref.method) ref.name = _method_get_name(ref.method);
    if (!ref.fnptr) PF_LOG("[il2cpp] method '%s' chưa có native pointer", methodName);
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

// ===================================================================== //
//  FIELD API — điểm mấu chốt để "tự update offset":
//  offset field được lấy theo TÊN lúc runtime, nên đổi version game
//  (layout đổi) vẫn đúng miễn là tên field không đổi.
// ===================================================================== //

struct FieldRef {
    void* field = nullptr;   // FieldInfo*
    void* klass = nullptr;   // Il2CppClass chứa nó
    std::string name;
    std::string type;
    size_t offset = 0;
    bool isStatic = false;
};

inline void* findField(void* klass, const char* name) {
    if (!klass || !name || !_class_get_field_from_name) return nullptr;
    return _class_get_field_from_name(klass, name);
}

inline size_t fieldOffset(void* klass, const char* name) {
    void* f = findField(klass, name);
    if (!f || !_field_get_offset) return 0;
    return _field_get_offset(f);
}

// Liệt kê field của class (dùng cho tab "Fields" trong GUI)
inline std::vector<FieldRef> classFields(void* klass, const char* keyword = nullptr, size_t limit = 300) {
    std::vector<FieldRef> out;
    if (!klass || !_class_get_field_count || !_class_get_field) return out;
    const size_t n = _class_get_field_count(klass);
    for (size_t i = 0; i < n && out.size() < limit; ++i) {
        void* f = _class_get_field(klass, i);
        if (!f) continue;
        FieldRef r;
        r.field = f;
        r.klass = klass;
        r.name = _field_get_name ? _field_get_name(f) : "";
        if (_field_get_type && _type_get_name) {
            const void* t = _field_get_type(f);
            if (t) r.type = _type_get_name(t);
        }
        r.offset = _field_get_offset ? _field_get_offset(f) : 0;
        // offset == 0 thường là static field trong dump
        r.isStatic = (r.offset == 0);
        if (keyword && *keyword && r.name.find(keyword) == std::string::npos) continue;
        out.push_back(r);
    }
    return out;
}

// Đọc field instance (offset tự lấy theo tên)
template <typename T>
inline T readField(void* object, const char* fieldName) {
    T v{};
    if (!object) return v;
    void* klass = _object_get_class ? _object_get_class(object) : nullptr;
    const size_t off = fieldOffset(klass, fieldName);
    if (!off) return v;
    memcpy(&v, reinterpret_cast<uint8_t*>(object) + off, sizeof(T));
    return v;
}

// Ghi field instance
template <typename T>
inline bool writeField(void* object, const char* fieldName, T value) {
    if (!object) return false;
    void* klass = _object_get_class ? _object_get_class(object) : nullptr;
    const size_t off = fieldOffset(klass, fieldName);
    if (!off) return false;
    memcpy(reinterpret_cast<uint8_t*>(object) + off, &value, sizeof(T));
    return true;
}

// Ghi field bằng offset đã biết (nhanh hơn resolve tên mỗi lần)
template <typename T>
inline bool writeFieldAt(void* object, size_t offset, T value) {
    if (!object || !offset) return false;
    memcpy(reinterpret_cast<uint8_t*>(object) + offset, &value, sizeof(T));
    return true;
}

template <typename T>
inline T readFieldAt(void* object, size_t offset) {
    T v{};
    if (!object || !offset) return v;
    memcpy(&v, reinterpret_cast<uint8_t*>(object) + offset, sizeof(T));
    return v;
}

// Đọc static field (vd ActorSystem.Self)
template <typename T>
inline bool readStaticField(void* klass, const char* fieldName, T& out) {
    if (!klass || !fieldName || !_class_get_field_from_name || !_field_static_get_value) return false;
    void* f = _class_get_field_from_name(klass, fieldName);
    if (!f) return false;
    T tmp{};
    _field_static_get_value(f, &tmp);
    out = tmp;
    return true;
}

// Gọi method không tham số trả về con trỏ (vd get_Self(), get_OtherActorCharacter())
inline void* callNoArgObject(void* object, const char* methodName) {
    void* klass = _object_get_class ? _object_get_class(object) : nullptr;
    if (!klass) return nullptr;
    auto m = resolveByClass(klass, methodName, 0);
    if (!m.fnptr) return nullptr;
    return reinterpret_cast<void* (*)(void*)>(m.fnptr)(object);
}

} // namespace PF::Il2Cpp
