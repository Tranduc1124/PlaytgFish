#pragma once

// Cấu trúc dữ liệu IL2CPP runtime — port từ tphat/Helper/Monostring.h
// (bản gọn: chỉ giữ phần cần cho đọc chuỗi/tên và duyệt collection).
//
//   Il2CppString : { klass, monitor, int32 length, char16_t chars[length] }
//   Il2CppArray  : { klass, monitor, bounds, int32 max_length, T data[] }
//   List<T>      : { items(Il2CppArray<T>*), int32 size, int32 version }

#include <cstdint>
#include <string>
#include <vector>

namespace PF {

// ---------------------------------------------------------------------
//  Il2CppString: layout 64-bit
// ---------------------------------------------------------------------
struct MonoString {
    void* klass;
    void* monitor;
    int32_t length;
    char16_t chars[1]; // thực tế length phần tử

    int32_t getLength() const { return length; }
    const char16_t* getChars() const { return chars; }
};

// UTF-16LE -> UTF-8 (tự chuyển để không cần Foundation)
inline std::string monoToUtf8(const MonoString* s) {
    if (!s || s->length <= 0) return std::string();
    std::string out;
    out.reserve(static_cast<size_t>(s->length));
    for (int32_t i = 0; i < s->length; ++i) {
        const uint32_t c = static_cast<uint16_t>(s->chars[i]);
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
        } else if (c < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (c >> 6)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (c >> 12)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return out;
}

inline std::string monoToUtf8(const void* s) {
    return monoToUtf8(reinterpret_cast<const MonoString*>(s));
}

// ---------------------------------------------------------------------
//  Il2CppArray<T>
// ---------------------------------------------------------------------
template <typename T>
struct MonoArray {
    void* klass;
    void* monitor;
    void* bounds;
    int32_t max_length;
    T data[1];

    int32_t length() const { return max_length; }
    T* pointer() { return data; }
    const T* pointer() const { return data; }

    std::vector<T> toVector() const {
        std::vector<T> out;
        if (max_length <= 0) return out;
        out.reserve(static_cast<size_t>(max_length));
        for (int32_t i = 0; i < max_length; ++i) out.push_back(data[i]);
        return out;
    }
};

// ---------------------------------------------------------------------
//  List<T>
// ---------------------------------------------------------------------
template <typename T>
struct MonoList {
    void* unk0;
    void* unk1;
    MonoArray<T>* items;
    int32_t size;
    int32_t version;

    int32_t length() const { return size; }
    T* pointer() { return items ? items->pointer() : nullptr; }
    const T* pointer() const { return items ? items->pointer() : nullptr; }

    std::vector<T> toVector() const {
        std::vector<T> out;
        const T* p = pointer();
        if (!p || size <= 0) return out;
        out.reserve(static_cast<size_t>(size));
        for (int32_t i = 0; i < size; ++i) out.push_back(p[i]);
        return out;
    }
};

// ---------------------------------------------------------------------
//  Dictionary<K,V> — đọc trực tiếp mảng entries (nhanh hơn GetEnumerator)
//  Layout chuẩn .NET/IL2CPP 64-bit.
// ---------------------------------------------------------------------
template <typename TKey, typename TValue>
struct MonoDictionary {
    struct Entry {
        int32_t hashCode;
        int32_t next;
        TKey key;
        TValue value;
    };

    void* klass;
    void* monitor;
    MonoArray<int32_t>* buckets;
    MonoArray<Entry>* entries;
    int32_t count;
    int32_t version;
    int32_t freeList;
    int32_t freeCount;
    void* comparer;
    MonoArray<TKey>* keys;
    MonoArray<TValue>* values;
    void* syncRoot;

    int32_t size() const { return count; }

    std::vector<TValue> getValues() const {
        std::vector<TValue> out;
        if (!entries) return out;
        const Entry* e = entries->pointer();
        const int32_t n = entries->length();
        out.reserve(static_cast<size_t>(count > 0 ? count : n));
        for (int32_t i = 0; i < n; ++i) {
            // hashCode < 0 => slot đã bị xoá
            if (e[i].hashCode < 0) continue;
            out.push_back(e[i].value);
        }
        return out;
    }

    std::vector<TKey> getKeys() const {
        std::vector<TKey> out;
        if (!entries) return out;
        const Entry* e = entries->pointer();
        const int32_t n = entries->length();
        out.reserve(static_cast<size_t>(count > 0 ? count : n));
        for (int32_t i = 0; i < n; ++i) {
            if (e[i].hashCode < 0) continue;
            out.push_back(e[i].key);
        }
        return out;
    }
};

} // namespace PF
