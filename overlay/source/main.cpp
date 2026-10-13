#define TESLA_INIT_IMPL
#include <tesla.hpp>
#include "protocol.hpp"
#include "i18n.hpp"
#include "file_store.hpp"
#include <cinttypes>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <set>
#include <sys/stat.h>
#include <unistd.h>

namespace {
using hgm::i18n::Text;
hgm::i18n::Language language = hgm::i18n::Language::English;
const char *tr(Text id) { return hgm::i18n::text(id, language); }
std::set<u64> policy, active;
std::vector<u64> catalog;
hgm::Status backend{};
bool policyValid = true, backendOnline = false;
Result nsResult = 0, backendResult = 0;
Result smResult = 0, sdResult = 0;
bool backendVersionMismatch = false;
uint32_t savedMode = hgm::Filter;
bool modeValid = true;
std::string notice;

std::string hexId(u64 id) {
    char text[17];
    std::snprintf(text, sizeof(text), "%016" PRIX64, id);
    return text;
}
using hgm::loadPolicyFile;
using hgm::writeSynced;
void refreshPolicy() {
    policyValid = R_SUCCEEDED(sdResult) && loadPolicyFile(hgm::PolicyPath, policy);
}
void refreshMode() {
    savedMode = hgm::Filter;
    modeValid = R_SUCCEEDED(sdResult);
    if (!modeValid) return;
    FILE *file = std::fopen(hgm::ModePath, "rb");
    if (!file) { modeValid = errno == ENOENT; return; }
    char data[4];
    size_t size = std::fread(data, 1, sizeof(data), file);
    const bool readOK = !std::ferror(file);
    std::fclose(file);
    modeValid = readOK && hgm::parseBootMode(data, size, savedMode);
}
bool saveMode(uint32_t mode);
void applyPolicy();
bool savePolicy(const std::set<u64> &next) {
    if (R_FAILED(sdResult)) { notice = tr(Text::SdUnavailable); return false; }
    if (next.size() > hgm::MaxHidden) { notice = tr(Text::TooMany); return false; }
    // Refuse to overwrite an externally changed or corrupt policy silently.
    std::set<u64> disk;
    if (!loadPolicyFile(hgm::PolicyPath, disk) || disk != policy) {
        policyValid = false;
        notice = tr(Text::PolicyChanged);
        return false;
    }
    if (mkdir("sdmc:/config", 0777) && errno != EEXIST) { notice = tr(Text::CreateConfig); return false; }
    if (mkdir(hgm::Directory, 0777) && errno != EEXIST) { notice = tr(Text::CreatePolicy); return false; }
    // Saving a list enables filtering; mode changes take effect after reboot.
    refreshMode();
    std::string text, backup;
    for (u64 id : next) text += hexId(id) + "\n";
    for (u64 id : policy) backup += hexId(id) + "\n";
    const std::string temp = std::string(hgm::PolicyPath) + ".tmp";
    const std::string bak = std::string(hgm::PolicyPath) + ".bak";
    if (!writeSynced(bak, backup.data(), backup.size()) || !writeSynced(temp, text.data(), text.size())) {
        notice = tr(Text::WriteFailed);
        return false;
    }
    const auto replaced = hgm::replaceFile(hgm::PolicyPath);
    if (replaced != hgm::ReplaceResult::Saved) {
        notice = replaced == hgm::ReplaceResult::RecoveryRequired ?
            tr(Text::RecoverBackup) : tr(Text::OriginalKept);
        return false;
    }
    policy = next;
    if ((!modeValid || savedMode != hgm::Filter) && !saveMode(hgm::Filter)) {
        notice = tr(Text::EnableFailed);
        return true;
    }
    applyPolicy();
    return true;
}
bool saveMode(uint32_t mode) {
    if (R_FAILED(sdResult)) { notice = tr(Text::SdUnavailable); return false; }
    if (mkdir("sdmc:/config", 0777) && errno != EEXIST) { notice = tr(Text::CreateConfig); return false; }
    if (mkdir(hgm::Directory, 0777) && errno != EEXIST) { notice = tr(Text::CreateConfig); return false; }
    const std::string temp = std::string(hgm::ModePath) + ".tmp";
    const char data[] = {static_cast<char>('0' + mode), '\n'};
    if (!writeSynced(temp, data, sizeof(data))) { notice = tr(Text::ModeWriteFailed); return false; }
    const auto replaced = hgm::replaceFile(hgm::ModePath);
    if (replaced != hgm::ReplaceResult::Saved) {
        notice = tr(Text::ModeRecover);
        return false;
    }
    savedMode = mode;
    modeValid = true;
    notice = tr(Text::ModeSaved);
    return true;
}
void refreshBackend() {
    backendOnline = false;
    backendVersionMismatch = false;
    active.clear(); backend = {};
    if (R_FAILED(smResult)) { backendResult = smResult; return; }
    // Atmosphere's read-only HasService extension avoids waiting forever when
    // the sysmodule is absent. Serialization follows Atmosphere sm_ams source.
    const SmServiceName name = smEncodeName(hgm::ServiceName);
    u8 exists = 0;
    backendResult = tipcDispatchInOut(smGetServiceSessionTipc(), 65100, name, exists);
    if (R_FAILED(backendResult) || !exists) return;
    Service srv{};
    backendResult = smGetService(&srv, hgm::ServiceName);
    if (R_FAILED(backendResult)) return;
    backendResult = serviceDispatchOut(&srv, 0, backend);
    backendVersionMismatch = R_SUCCEEDED(backendResult) && backend.protocol != hgm::Protocol;
    if (R_SUCCEEDED(backendResult) && backend.protocol == hgm::Protocol && backend.hidden_count <= hgm::MaxHidden) {
        std::vector<u64> ids(hgm::MaxHidden);
        backendResult = serviceDispatch(&srv, 1,
            .buffer_attrs = {SfBufferAttr_HipcMapAlias | SfBufferAttr_Out},
            .buffers = {{ids.data(), ids.size() * sizeof(u64)}});
        if (R_SUCCEEDED(backendResult)) {
            active.insert(ids.begin(), ids.begin() + backend.hidden_count);
            backendOnline = true;
        }
    }
    serviceClose(&srv);
}
void applyPolicy() {
    // Persisted policy remains usable at next boot even if the backend is absent.
    refreshBackend();
    if (!backendOnline) { notice = tr(Text::Saved); return; }
    Service srv{};
    auto rc = smGetService(&srv, hgm::ServiceName);
    u32 notified = 0;
    if (R_SUCCEEDED(rc)) {
        rc = serviceDispatchOut(&srv, 2, notified);
        serviceClose(&srv);
    }
    if (R_FAILED(rc)) { notice = tr(Text::ApplyFailed); return; }
    refreshBackend();
    if (!backendOnline || active != policy || backend.boot_mode != hgm::Filter ||
        (backend.flags & (hgm::EmergencyDisabled | hgm::ModeInvalid | hgm::MitmUnavailable))) {
        notice = tr(Text::BackendDisabled);
        return;
    }
    notice = notified ? tr(Text::Applied) : tr(Text::Saved);
}
void refreshCatalog() {
    catalog.clear();
    if (R_FAILED(nsResult)) { notice = tr(Text::CatalogFailed); return; }
    NsApplicationRecord page[128];
    for (s32 offset = 0; offset < 32768;) {
        s32 got = 0;
        Result rc = nsListApplicationRecord(page, 128, offset, &got);
        if (R_FAILED(rc) || got < 0 || got > 128) { notice = tr(Text::CatalogIncomplete); break; }
        if (!got) return;
        for (s32 i = 0; i < got; ++i) {
            u64 id = page[i].application_id;
            if (hgm::isApplicationId(id)) catalog.push_back(id);
        }
        offset += got;
    }
}
std::string gameName(u64 id) {
    if (R_FAILED(nsResult)) return hexId(id);
    static NsApplicationControlData data;
    u64 size = 0;
    Result rc = nsGetApplicationControlData(NsApplicationControlSource_Storage, id, &data, sizeof(data), &size);
    if (R_FAILED(rc) || size < sizeof(NacpStruct)) return hexId(id);
    NacpLanguageEntry *entry = nullptr;
    rc = nacpGetLanguageEntry(&data.nacp, &entry);
    if (R_FAILED(rc) || !entry || !entry->name[0]) return hexId(id);
    return std::string(entry->name, strnlen(entry->name, sizeof(entry->name)));
}

using List = tsl::elm::List;
using Item = tsl::elm::ListItem;
using Header = tsl::elm::CategoryHeader;
// Keep libtesla's frame layout and input handling, translating its fixed footer.
class LocalizedFrame : public tsl::elm::OverlayFrame {
public:
    using OverlayFrame::OverlayFrame;
    void draw(tsl::gfx::Renderer *renderer) override {
        renderer->fillScreen(a(tsl::style::color::ColorFrameBackground));
        renderer->drawRect(tsl::cfg::FramebufferWidth - 1, 0, 1, tsl::cfg::FramebufferHeight, a(0xF222));
        renderer->drawString(m_title.c_str(), false, 20, 50, 30, a(tsl::style::color::ColorText));
        renderer->drawString(m_subtitle.c_str(), false, 20, 70, 15, a(tsl::style::color::ColorDescription));
        renderer->drawRect(15, tsl::cfg::FramebufferHeight - 73, tsl::cfg::FramebufferWidth - 30, 1,
                           a(tsl::style::color::ColorText));
        renderer->drawString(tr(Text::Footer), false, 30, 693, 23, a(tsl::style::color::ColorText));
        if (m_contentElement) m_contentElement->frame(renderer);
    }
};
tsl::elm::Element *frame(const char *title, List *list) {
    auto *f = new LocalizedFrame(title, std::string("v") + hgm::Version);
    f->setContent(list); return f;
}
void action(List *list, const std::string &text, std::function<void()> fn) {
    auto *item = new Item(text);
    item->setClickListener([fn](u64 keys) { if (!(keys & HidNpadButton_A)) return false; fn(); return true; });
    list->addItem(item);
}

// Confirm only the batch operation. Descriptive text never takes focus, and
// pop the screen from update rather than destroying it inside its click callback.
class RestoreAll : public tsl::Gui {
    Header *message{};
    bool leave = false;
public:
    tsl::elm::Element *createUI() override {
        auto *list = new List;
        char prompt[128];
        std::snprintf(prompt, sizeof(prompt), tr(Text::RestorePrompt), policy.size());
        list->addItem(new Header(prompt));
        message = new Header(tr(Text::NoDeletion)); list->addItem(message);
        action(list, tr(Text::RestoreAll), [this] {
            if (savePolicy({})) { notice = notice == tr(Text::Applied) ? tr(Text::Restored) : notice; leave = true; }
            else message->setText(notice);
        });
        action(list, tr(Text::Cancel), [this] { leave = true; });
        return frame(tr(Text::RestoreAll), list);
    }
    void update() override {
        if (leave) { tsl::goBack(); return; }
    }
};
class Games : public tsl::Gui {
    size_t page = 0;
    List *list{};
    Header *summary{};
    tsl::elm::OverlayFrame *screen{};
    bool rebuild = false;
    std::vector<u64> ids;
    std::vector<std::pair<u64, tsl::elm::ToggleListItem *>> rows;
    void fill() {
        rows.clear();
        constexpr size_t PageSize = 30;
        const size_t pages = std::max(size_t(1), (ids.size() + PageSize - 1) / PageSize);
        if (page >= pages) page = pages - 1;
        summary = new Header(""); list->addItem(summary);
        if (!policyValid) list->addItem(new Header(tr(Text::PolicyFailed)));
        else if (ids.empty()) list->addItem(new Header(R_FAILED(nsResult) ? tr(Text::CatalogFailed) : tr(Text::NoGames)));
        else for (size_t i = page * PageSize; i < std::min(ids.size(), (page + 1) * PageSize); ++i) {
            const u64 id = ids[i];
            auto *item = new tsl::elm::ToggleListItem(gameName(id), policy.count(id), tr(Text::Hidden), tr(Text::Shown));
            item->setStateChangedListener([id, item](bool hide) {
                auto next = policy;
                if (hide) next.insert(id); else next.erase(id);
                if (savePolicy(next)) {
                    if (notice == tr(Text::Applied))
                        notice = hide ? tr(Text::HideApplied) : tr(Text::ShowApplied);
                }
                // Match the switch to the last successfully persisted policy.
                item->setState(policy.count(id));
            });
            item->setClickListener([](u64 keys) { return (keys & HidNpadButton_A) != 0; });
            list->addItem(item);
            rows.emplace_back(id, item);
        }
        if (page + 1 < pages) action(list, tr(Text::NextPage), [this] { ++page; rebuild = true; });
        if (page) action(list, tr(Text::PreviousPage), [this] { --page; rebuild = true; });
        if (policyValid) {
            list->addItem(new Header(tr(Text::Batch), true));
            action(list, tr(Text::RestoreAll), [] {
                if (policy.empty()) { notice = tr(Text::NoneHidden); return; }
                tsl::changeTo<RestoreAll>();
            });
        }
        list->addItem(new Header(tr(Text::Shared)));
    }
public:
    Games() {
        // Keep a stable order and include hidden IDs even after uninstalling.
        // Changing a switch never removes its row or moves the cursor.
        std::set<u64> seen;
        for (u64 id : catalog)
            if (seen.insert(id).second) ids.push_back(id);
        for (u64 id : policy)
            if (seen.insert(id).second) ids.push_back(id);
    }
    tsl::elm::Element *createUI() override {
        list = new List; fill();
        screen = new LocalizedFrame(tr(Text::AppName), "");
        screen->setContent(list);
        return screen;
    }
    void update() override {
        if (rebuild) {
            removeFocus(); list->clear(); fill(); rebuild = false;
            restoreFocus();
        }
        for (const auto &[id, item] : rows) {
            const bool hidden = policy.count(id);
            if (item->getState() != hidden) item->setState(hidden);
        }
        char counts[128];
        std::snprintf(counts, sizeof(counts), tr(Text::Summary), policy.size(), ids.size());
        std::string state = counts;
        const size_t pages = std::max(size_t(1), (ids.size() + 29) / 30);
        if (pages > 1) state += " · " + std::to_string(page + 1) + "/" + std::to_string(pages);
        summary->setText(state);
        // Errors remain visible until the next action succeeds. No transient
        // success page, forced navigation, or rebuilding after a single toggle.
        if (!policyValid) notice = tr(Text::PolicyFailed);
        else if (backendVersionMismatch) notice = tr(Text::UpdateBackend);
        else if (!backendOnline && notice.empty()) notice = tr(Text::BackendAbsent);
        else if (backendOnline && (backend.boot_mode != hgm::Filter || policy != active ||
            (backend.flags & (hgm::EmergencyDisabled | hgm::ModeInvalid | hgm::MitmUnavailable))) && notice.empty()) notice = tr(Text::Pending);
        // Use the fixed frame subtitle so errors and results remain visible
        // even while the game list is scrolled.
        screen->setSubtitle(notice.empty() ? std::string("v") + hgm::Version + tr(Text::ToggleHint) : notice);
    }
};
class Overlay : public tsl::Overlay {
    Result setResult = 0;
public:
    void initServices() override {
        // libtesla's surrounding doWithSmSession ends before loadInitialGui.
        // Retain our own SM reference until exit; refreshBackend runs later.
        smResult = smInitialize();
        sdResult = fsdevMountSdmc();
        nsResult = nsInitialize();
        setResult = setInitialize();
        u64 languageCode = 0;
        if (R_SUCCEEDED(setResult) && R_SUCCEEDED(setGetSystemLanguage(&languageCode))) {
            char code[sizeof(languageCode) + 1]{};
            std::memcpy(code, &languageCode, sizeof(languageCode));
            language = hgm::i18n::languageFor(code);
        }
    }
    void exitServices() override {
        if (R_SUCCEEDED(sdResult)) fsdevUnmountDevice("sdmc");
        if (R_SUCCEEDED(setResult)) setExit();
        if (R_SUCCEEDED(nsResult)) nsExit();
        if (R_SUCCEEDED(smResult)) smExit();
    }
    std::unique_ptr<tsl::Gui> loadInitialGui() override {
        refreshPolicy();
        if (!policyValid) notice = tr(Text::PolicyFailed);
        refreshBackend(); refreshCatalog();
        return initially<Games>();
    }
};
}
int main(int argc, char **argv) { return tsl::loop<Overlay>(argc, argv); }
