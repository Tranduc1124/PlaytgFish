#include "Discovery.hpp"

#include <algorithm>
#include <cstring>

#include "../Core/il2cpp.hpp"
#include "../Core/log.hpp"

namespace PF::Discovery {
namespace {

// Namespace hệ thống/unreal — bỏ qua để danh sách gọn
bool isNoise(const char* nsp) {
    static const char* noise[] = {"System", "UnityEngine", "Mono.", "Microsoft.", "TMPro",
                                  "Google.", "Firebase", "DG.", "TM.", "Newtonsoft", "AOT"};
    if (!nsp || !*nsp) return false;
    for (const char* n : noise)
        if (strcasestr(nsp, n)) return true;
    return false;
}

int scoreOf(const char* text, const char* keywords, const char** matchedRole) {
    if (!text) return 0;
    int score = 0;
    if (strcasestr(text, "bite") || strcasestr(text, "nibble")) { score += 5; if (matchedRole) *matchedRole = "bite"; }
    if (strcasestr(text, "reel") || strcasestr(text, "hookup")) { score += 4; if (matchedRole) *matchedRole = "reel"; }
    if (strcasestr(text, "catch") || strcasestr(text, "result") || strcasestr(text, "success")) { score += 4; if (matchedRole) *matchedRole = "result"; }
    if (strcasestr(text, "cast") || strcasestr(text, "throw") || strcasestr(text, "fishing")) { score += 3; if (matchedRole && !*matchedRole) *matchedRole = "cast"; }
    if (strcasestr(text, "fish")) { score += 3; if (matchedRole && !*matchedRole) *matchedRole = "fish"; }
    if (strcasestr(text, "rod") || strcasestr(text, "bait")) { score += 1; if (matchedRole && !*matchedRole) *matchedRole = "other"; }
    if (keywords && *keywords && strcasestr(text, keywords)) score += 2;
    return score;
}

} // namespace

std::vector<Candidate> scan(const char* keywords, size_t limit) {
    std::vector<Candidate> out;
    if (!Il2Cpp::ready()) {
        PF_LOG("[discovery] il2cpp chưa sẵn sàng");
        return out;
    }

    Il2Cpp::forEachClass([&](void* k, const char* kn, const char* ksp, const char* img) {
        if (out.size() >= limit) return;
        if (isNoise(ksp)) return;

        const char* role = nullptr;
        const int classScore = scoreOf(kn, keywords, &role);
        if (classScore <= 0) return;

        for (const auto& m : Il2Cpp::classMethods(k, nullptr, 80)) {
            const char* methodRole = nullptr;
            const int methodScore = scoreOf(m.name, keywords, &methodRole);
            if (methodScore <= 0) continue;
            if (!m.fnptr) continue; // chỉ lấy method có code native

            Candidate c;
            c.nsp = ksp ? ksp : "";
            c.klass = kn ? kn : "";
            c.method = m.name ? m.name : "";
            c.image = img ? img : "";
            c.fnptr = m.fnptr;
            c.score = classScore + methodScore;
            c.role = methodRole ? methodRole : (role ? role : "other");
            out.push_back(c);
        }
    });

    std::sort(out.begin(), out.end(), [](const Candidate& a, const Candidate& b) {
        return a.score > b.score;
    });
    PF_LOG("[discovery] tìm thấy %zu method ứng viên", out.size());
    return out;
}

std::vector<Candidate> methodsOf(void* klass, const char* className, const char* nsp, size_t limit) {
    std::vector<Candidate> out;
    for (const auto& m : Il2Cpp::classMethods(klass, nullptr, limit)) {
        if (!m.fnptr) continue;
        Candidate c;
        c.nsp = nsp ? nsp : "";
        c.klass = className ? className : "";
        c.method = m.name ? m.name : "";
        c.fnptr = m.fnptr;
        const char* role = nullptr;
        c.score = scoreOf(m.name, nullptr, &role);
        c.role = role ? role : "other";
        out.push_back(c);
    }
    std::sort(out.begin(), out.end(), [](const Candidate& a, const Candidate& b) {
        return a.score > b.score;
    });
    return out;
}

} // namespace PF::Discovery
