#include <stratosphere.hpp>
#include <switch.h>
#include <atomic>
#include "protocol.hpp"
#include "filter.hpp"

namespace nh {
using namespace ams;
alignas(16) uint64_t hidden[hgm::MaxHidden];
size_t hiddenCount;
hgm::Status status{.protocol = hgm::Protocol};
bool sdMounted;
bool logOpen;
s64 logOffset;
unsigned loggedCommands;
bool interceptHome;

// FS IPC reuses TLS. Preserve it even when called between forwarding and reply;
// otherwise the logger itself would corrupt the response sent to HOME.
void log(const char *event, u32 command = 0, u32 result = 0) {
    alignas(16) u8 saved[sf::hipc::TlsMessageBufferSize];
    void *tls = sf::hipc::GetMessageBufferOnTls();
    std::memcpy(saved, tls, sizeof(saved));
    ON_SCOPE_EXIT { std::memcpy(tls, saved, sizeof(saved)); };
    char line[160];
    int bytes = std::snprintf(line, sizeof(line), "%s stage=%u mode=%u flags=%X hos=%X cmd=%u result=%08X\n",
        event, status.stage, status.boot_mode, status.flags, status.hos_version, command, result);
    bytes = std::clamp(bytes, 0, static_cast<int>(sizeof(line) - 1));
    svcOutputDebugString(line, bytes);
    if (!logOpen || logOffset + bytes > 128 * 1024) return;
    // Close after each entry so other clients can read the log.
    fs::FileHandle file;
    auto rc = fs::OpenFile(&file, hgm::LogPath, fs::OpenMode_Write | fs::OpenMode_AllowAppend);
    if (R_SUCCEEDED(rc)) {
        rc = fs::WriteFile(file, logOffset, line, bytes, fs::WriteOption::Flush);
        fs::CloseFile(file);
    }
    if (R_SUCCEEDED(rc)) logOffset += bytes;
    else { status.log_result = rc.GetValue(); logOpen = false; }
}
void initLog() {
    if (!sdMounted) return;
    auto rc = fs::EnsureDirectory(hgm::Directory);
    if (R_FAILED(rc)) { status.log_result = rc.GetValue(); return; }
    bool exists = false;
    if (R_SUCCEEDED(fs::HasFile(&exists, hgm::LogPath)) && exists) {
        rc = fs::DeleteFile(hgm::PreviousLogPath);
        if (R_FAILED(rc) && !fs::ResultPathNotFound::Includes(rc)) { status.log_result = rc.GetValue(); return; }
        rc = fs::RenameFile(hgm::LogPath, hgm::PreviousLogPath);
        if (R_FAILED(rc)) { status.log_result = rc.GetValue(); return; }
    }
    rc = fs::CreateFile(hgm::LogPath, 0);
    logOpen = R_SUCCEEDED(rc);
    status.log_result = rc.GetValue();
    log(hgm::Version);
}
void loadMode() {
    // Release installs hide by default; explicit recovery modes remain valid.
    status.boot_mode = hgm::Filter;
    if (!sdMounted) return;
    fs::FileHandle file;
    auto rc = fs::OpenFile(&file, hgm::ModePath, fs::OpenMode_Read);
    if (fs::ResultPathNotFound::Includes(rc)) return;
    if (R_FAILED(rc)) { status.flags |= hgm::ModeInvalid; status.last_result = rc.GetValue(); return; }
    ON_SCOPE_EXIT { fs::CloseFile(file); };
    s64 size = 0;
    char text[4];
    rc = fs::GetFileSize(&size, file);
    if (R_SUCCEEDED(rc) && size > 0 && size <= static_cast<s64>(sizeof(text))) {
        rc = fs::ReadFile(file, 0, text, size);
        if (R_SUCCEEDED(rc) && hgm::parseBootMode(text, size, status.boot_mode)) return;
    }
    status.boot_mode = hgm::DiagnosticsOnly;
    status.flags |= hgm::ModeInvalid;
    status.last_result = R_FAILED(rc) ? rc.GetValue() : sf::cmif::ResultInvalidOutRawSize().GetValue();
}

bool isHidden(uint64_t id) {
    return std::binary_search(hidden, hidden + hiddenCount, id);
}

void loadPolicy() {
    status.hos_version = static_cast<u32>(hos::GetVersion());
    if (hos::GetVersion() >= hos::Version_20_0_0) status.flags |= hgm::ModernPathUnknown;
    if (!sdMounted) return;
    bool disabled = false;
    auto disableRc = fs::HasFile(&disabled, hgm::DisablePath);
    if (R_FAILED(disableRc) || disabled) {
        status.flags |= hgm::EmergencyDisabled;
        status.last_result = disableRc.GetValue();
        return;
    }
    fs::FileHandle file;
    auto rc = fs::OpenFile(&file, hgm::PolicyPath, fs::OpenMode_Read);
    if (fs::ResultPathNotFound::Includes(rc)) return;
    if (R_FAILED(rc)) { status.flags |= hgm::PolicyInvalid; status.last_result = rc.GetValue(); return; }
    ON_SCOPE_EXIT { fs::CloseFile(file); };
    s64 bytes = 0;
    static char text[hgm::MaxPolicyBytes];
    rc = fs::GetFileSize(&bytes, file);
    if (R_SUCCEEDED(rc) && bytes >= 0 && static_cast<size_t>(bytes) <= sizeof(text)) {
        rc = fs::ReadFile(file, 0, text, bytes);
        if (R_SUCCEEDED(rc) && hgm::parsePolicy(text, bytes, hidden, hiddenCount)) {
            std::sort(hidden, hidden + hiddenCount);
            status.hidden_count = hiddenCount;
            return;
        }
    }
    hiddenCount = 0;
    status.flags |= hgm::PolicyInvalid;
    status.last_result = R_FAILED(rc) ? rc.GetValue() : sf::cmif::ResultInvalidOutRawSize().GetValue();
}

// The server loop serializes policy reloads and list queries. The relay thread
// only touches immutable event handles and an atomic error value, never policy.
Handle recordSource = INVALID_HANDLE, recordWriter = INVALID_HANDLE, recordReader = INVALID_HANDLE;
::Thread recordRelayThread;
alignas(0x1000) u8 recordRelayStack[0x4000];
std::atomic<u32> recordRelayError{0};
bool recordEventReady;
bool recordEventFailed;
void relayRecordEvents(void *) {
    while (true) {
        auto rc = svcWaitSynchronizationSingle(recordSource, UINT64_MAX);
        if (svc::ResultCancelled::Includes(rc)) continue;
        if (R_SUCCEEDED(rc)) rc = svcResetSignal(recordSource);
        // Another NS client may clear the shared source event after our wait.
        // Retry that race rather than permanently stopping the relay.
        if (svc::ResultInvalidState::Includes(rc)) continue;
        if (R_SUCCEEDED(rc)) rc = svcSignalEvent(recordWriter);
        if (R_FAILED(rc)) {
            recordRelayError.store(rc);
            // Wake existing subscribers once so they can observe current lists.
            // They still need a reboot to regain future event delivery.
            R_DISCARD(svcSignalEvent(recordWriter));
            return;
        }
    }
}
ams::Result prepareRecordEvent(Service *forward) {
    if (recordEventReady) {
        // New subscriptions fall back to NS if the relay has failed.
        const u32 error = recordRelayError.load();
        if (error) R_RETURN(ams::Result(error));
        R_SUCCEED();
    }
    R_UNLESS(!recordEventFailed, sf::cmif::ResultInvalidOutRawSize());
    log("HOME.event.begin", 2);
    R_TRY(serviceDispatch(forward, 2,
        .out_handle_attrs = {SfOutHandleAttr_HipcCopy}, .out_handles = &recordSource));
    auto rc = svcCreateEvent(&recordWriter, &recordReader);
    log("HOME.event.created", 2, rc);
    // libnx takes the actual kernel priority. Atmosphere os::CreateThread adds
    // 28 to its user priority; do not pass a kernel priority to that API.
    constexpr int KernelPriority = 40;
    static_assert(KernelPriority >= 24 && KernelPriority <= 63);
    bool threadCreated = false;
    if (R_SUCCEEDED(rc)) {
        rc = ::threadCreate(&recordRelayThread, relayRecordEvents, nullptr,
            recordRelayStack, sizeof(recordRelayStack), KernelPriority, 3);
        threadCreated = R_SUCCEEDED(rc);
        log("HOME.relay.created", 2, rc);
    }
    if (R_SUCCEEDED(rc)) {
        rc = ::threadStart(&recordRelayThread);
        log("HOME.relay.started", 2, rc);
    }
    if (R_FAILED(rc)) {
        recordEventFailed = true;
        status.refresh_result = rc;
        if (threadCreated) {
            const auto closeRc = ::threadClose(&recordRelayThread);
            if (R_FAILED(closeRc)) log("HOME.relay.cleanup.failed", 2, closeRc);
        }
        log("HOME.event.fallback", 2, rc);
        svcCloseHandle(recordSource); recordSource = INVALID_HANDLE;
        if (recordWriter != INVALID_HANDLE) svcCloseHandle(recordWriter);
        if (recordReader != INVALID_HANDLE) svcCloseHandle(recordReader);
        recordWriter = recordReader = INVALID_HANDLE;
        R_RETURN(rc);
    }
    recordEventReady = true;
    status.event_ready = 1;
    // Catch up even when HOME subscribes after a reload.
    if (status.reloads) R_DISCARD(svcSignalEvent(recordWriter));
    log("HOME.event.attached", 2);
    R_SUCCEED();
}
ams::Result reloadPolicy(u32 *notified) {
    *notified = 0;
    R_UNLESS(sdMounted && interceptHome, sf::cmif::ResultInvalidOutRawSize());
    bool disabled = false;
    R_TRY(fs::HasFile(&disabled, hgm::DisablePath));
    R_UNLESS(!disabled, sf::cmif::ResultInvalidOutRawSize());
    // Parse in separate storage: an invalid or partial file cannot replace the
    // active list. Empty/missing files deliberately mean restore everything.
    static char text[hgm::MaxPolicyBytes];
    static u64 pending[hgm::MaxHidden];
    size_t count = 0;
    fs::FileHandle file;
    auto rc = fs::OpenFile(&file, hgm::PolicyPath, fs::OpenMode_Read);
    if (!fs::ResultPathNotFound::Includes(rc)) {
        R_TRY(rc);
        ON_SCOPE_EXIT { fs::CloseFile(file); };
        s64 size = 0;
        R_TRY(fs::GetFileSize(&size, file));
        R_UNLESS(size >= 0 && static_cast<u64>(size) <= sizeof(text), sf::cmif::ResultInvalidOutRawSize());
        R_TRY(fs::ReadFile(file, 0, text, size));
        R_UNLESS(hgm::parsePolicy(text, size, pending, count), sf::cmif::ResultInvalidOutRawSize());
    }
    std::sort(pending, pending + count);
    std::memcpy(hidden, pending, count * sizeof(u64));
    hiddenCount = count;
    status.hidden_count = count;
    status.flags &= ~hgm::PolicyInvalid;
    ++status.reloads;
    status.refresh_result = recordRelayError.load();
    if (recordEventReady && !status.refresh_result) {
        status.refresh_result = svcSignalEvent(recordWriter);
        *notified = R_SUCCEEDED(status.refresh_result);
    }
    log(*notified ? "policy.reload: HOME notified" : "policy.reload: no HOME event", 2, status.refresh_result);
    R_SUCCEED();
}

// Enumerate RAW offsets, then apply the caller's offset to the VISIBLE stream.
// A fully hidden raw page must never become an early end-of-list response.
static_assert(sizeof(NsApplicationRecord) == 0x18);
ams::Result scan(Service *srv, s32 offset, NsApplicationRecord *out, size_t capacity, u64 *total, s32 *written, u32 listCommand = 0) {
    ams::Result fetchResult = ams::ResultSuccess();
    u64 visible = 0, removed = 0;
    s32 used = 0;
    const auto result = hgm::scanVisible(offset, out, capacity, visible, used, removed,
        [&](s32 rawOffset, NsApplicationRecord *page, size_t count, s32 &got) {
            fetchResult = serviceDispatchInOut(srv, listCommand, rawOffset, got,
                .buffer_attrs = {SfBufferAttr_HipcMapAlias | SfBufferAttr_Out},
                .buffers = {{page, count * sizeof(*page)}});
            return R_SUCCEEDED(fetchResult);
        }, isHidden);
    R_TRY(fetchResult);
    R_UNLESS(result == hgm::ScanResult::Complete, sf::cmif::ResultInvalidOutRawSize());
    if (total) *total = visible;
    if (written) *written = used;
    status.filtered_records += removed;
    R_SUCCEED();
}

#define MANAGER_INFO(C,H) \
    AMS_SF_METHOD_INFO(C,H,0,ams::Result,List,(sf::Out<s32> count, s32 offset, const sf::OutMapAliasArray<NsApplicationRecord> &records),(count,offset,records)) \
    AMS_SF_METHOD_INFO(C,H,1,ams::Result,Count,(sf::Out<u64> count),(count)) \
    AMS_SF_METHOD_INFO(C,H,2,ams::Result,GetUpdateEvent,(sf::OutCopyHandle event),(event))
}
AMS_SF_DEFINE_MITM_INTERFACE(nh, IManager, MANAGER_INFO, 0x48474D01)
namespace nh {
class Manager : public sf::MitmServiceImplBase {
public:
    using MitmServiceImplBase::MitmServiceImplBase;
    ams::Result fallback(ams::Result rc) {
        status.last_result = rc.GetValue();
        ++status.forward_fallbacks;
        return sm::mitm::ResultShouldForwardToSession();
    }
    ams::Result List(sf::Out<s32> count, s32 offset, const sf::OutMapAliasArray<NsApplicationRecord> &records) {
        ++status.list_calls;
        status.last_command = 0;
        const bool trace = loggedCommands++ < 128;
        if (trace) log("AM.list.begin");
        if (!hiddenCount || offset < 0 || !records.GetSize()) return sm::mitm::ResultShouldForwardToSession();
        auto rc = scan(m_forward_service.get(), offset, records.GetPointer(), records.GetSize(), nullptr, count.GetPointer());
        if (trace) log("AM.list.end", 0, rc.GetValue());
        if (R_FAILED(rc)) return fallback(rc);
        R_SUCCEED();
    }
    ams::Result GetUpdateEvent(sf::OutCopyHandle event) {
        ++status.update_event_requests;
        auto rc = prepareRecordEvent(m_forward_service.get());
        if (R_FAILED(rc)) return fallback(rc);
        event.SetValue(recordReader, false);
        R_SUCCEED();
    }
    ams::Result Count(sf::Out<u64> count) {
        ++status.count_calls;
        status.last_command = 1;
        const bool trace = loggedCommands++ < 128;
        if (trace) log("AM.count.begin", 1);
        if (!hiddenCount) return sm::mitm::ResultShouldForwardToSession();
        auto rc = scan(m_forward_service.get(), 0, nullptr, 0, count.GetPointer(), nullptr);
        if (trace) log("AM.count.end", 1, rc.GetValue());
        if (R_FAILED(rc)) return fallback(rc);
        R_SUCCEED();
    }
};
static_assert(IsIManager<Manager>);

#define READONLY_INFO(C,H) \
    AMS_SF_METHOD_INFO(C,H,3,ams::Result,List,(sf::Out<s32> count, s32 offset, const sf::OutMapAliasArray<NsApplicationRecord> &records),(count,offset,records),ams::hos::Version_20_0_0)
}
AMS_SF_DEFINE_MITM_INTERFACE(nh, IReadOnlyRecord, READONLY_INFO, 0x48474D04)
namespace nh {
class ReadOnlyRecord : public sf::MitmServiceImplBase {
public:
    using MitmServiceImplBase::MitmServiceImplBase;
    ams::Result List(sf::Out<s32> count, s32 offset, const sf::OutMapAliasArray<NsApplicationRecord> &records) {
        ++status.readonly_list_calls;
        status.last_command = 3;
        const bool trace = loggedCommands++ < 128;
        if (trace) log("RO.list.begin", 3);
        if (!hiddenCount || offset < 0 || !records.GetSize()) return sm::mitm::ResultShouldForwardToSession();
        auto rc = scan(m_forward_service.get(), offset, records.GetPointer(), records.GetSize(), nullptr, count.GetPointer(), 3);
        if (trace) log("RO.list.end", 3, rc.GetValue());
        if (R_FAILED(rc)) {
            status.last_result = rc.GetValue();
            ++status.forward_fallbacks;
            return sm::mitm::ResultShouldForwardToSession();
        }
        R_SUCCEED();
    }
};
static_assert(IsIReadOnlyRecord<ReadOnlyRecord>);

#define GETTER_INFO(C,H) \
    AMS_SF_METHOD_INFO(C,H,7991,ams::Result,GetReadOnlyRecord,(sf::Out<sf::SharedPointer<nh::IReadOnlyRecord>> out),(out),ams::hos::Version_20_0_0) \
    AMS_SF_METHOD_INFO(C,H,7996,ams::Result,GetManager,(sf::Out<sf::SharedPointer<nh::IManager>> out),(out))
}
AMS_SF_DEFINE_MITM_INTERFACE(nh, IGetter, GETTER_INFO, 0x48474D02)
namespace nh {

// A returned non-domain object needs its OWN MITM session to forward unknown
// commands. Ordinary SF Out<interface> creates a non-MITM session. Domain objects
// use the generated serializer and retain the original domain object ID.
struct GetterDispatch : sf::cmif::impl::ServiceDispatchTableBase {
    ams::Result ProcessMessageForMitm(sf::cmif::ServiceDispatchContext &, const sf::cmif::PointerAndSize &) const;
    ams::Result ProcessMessage(sf::cmif::ServiceDispatchContext &ctx, const sf::cmif::PointerAndSize &data) const {
        return ProcessMessageForMitm(ctx, data);
    }
};
class Getter : public IGetter {
    std::atomic<int> refs{1};
public:
    std::shared_ptr<Service> forward;
    sm::MitmProcessInfo client;
    Getter(std::shared_ptr<Service> f, const sm::MitmProcessInfo &c) : forward(std::move(f)), client(c) {}
    void AddReference() override { ++refs; }
    void Release() override { if (--refs == 0) delete this; }
    template<typename> static constexpr GetterDispatch s_CmifServiceDispatchTable{};
    static bool ShouldMitm(const sm::MitmProcessInfo &c) {
        if (c.program_id != ncm::SystemAppletId::Qlaunch) return false;
        if (status.boot_mode == hgm::ForwardOnly) return true;
        return interceptHome;
    }
    ams::Result _ams_sf_sync_GetManager(sf::Out<sf::SharedPointer<nh::IManager>> out) override {
        auto child = sf::hipc::ServerSession::CreateForwardService();
        auto rc = serviceDispatch(forward.get(), 7996, .out_num_objects = 1, .out_objects = child.get());
        R_TRY(rc);
        out.SetValue(sf::CreateSharedObjectEmplaced<IManager, Manager>(std::shared_ptr<Service>(child), client),
                     sf::cmif::DomainObjectId{serviceGetObjectId(child.get())});
        ++status.manager_getters;
        R_SUCCEED();
    }
    ams::Result _ams_sf_sync_GetReadOnlyRecord(sf::Out<sf::SharedPointer<nh::IReadOnlyRecord>> out) override {
        auto child = sf::hipc::ServerSession::CreateForwardService();
        R_TRY(serviceDispatch(forward.get(), 7991, .out_num_objects = 1, .out_objects = child.get()));
        out.SetValue(sf::CreateSharedObjectEmplaced<IReadOnlyRecord, ReadOnlyRecord>(std::shared_ptr<Service>(child), client),
                     sf::cmif::DomainObjectId{serviceGetObjectId(child.get())});
        ++status.readonly_wrapped;
        R_SUCCEED();
    }
};

ams::Result GetterDispatch::ProcessMessageForMitm(sf::cmif::ServiceDispatchContext &ctx, const sf::cmif::PointerAndSize &data) const {
    auto *self = static_cast<Getter *>(ctx.srv_obj);
    if (data.GetSize() < sizeof(CmifInHeader)) return ctx.session->ForwardRequest(ctx);
    const auto *header = static_cast<const CmifInHeader *>(data.GetPointer());
    if (header->magic != CMIF_IN_HEADER_MAGIC) return ctx.session->ForwardRequest(ctx);
    const u32 command = header->command_id;
    status.last_command = command;
    const bool trace = loggedCommands++ < 128;
    if (trace) log("getter.begin", command);
    ON_SCOPE_EXIT { if (trace) log("getter.end", command); };
    if (command == 7991) ++status.readonly_getters;
    const bool wrapReadOnly = command == 7991 && hos::GetVersion() >= hos::Version_20_0_0;
    if (status.boot_mode != hgm::Filter || !interceptHome || (command != 7996 && !wrapReadOnly)) return ctx.session->ForwardRequest(ctx);
    // SF may clone a session while retaining the same service object. Do not
    // serialize against the object's original forward session if the clone's
    // domain state differs: leave the original response untouched instead.
    if ((ctx.processor != nullptr) != static_cast<bool>(serviceIsDomain(self->forward.get()))) {
        ++status.forward_fallbacks;
        if (trace) log("getter.domain mismatch: forward", command);
        return ctx.session->ForwardRequest(ctx);
    }
    if (serviceIsDomain(self->forward.get()))
        return IGetter::s_CmifServiceDispatchTable<IGetter>.ProcessMessageForMitm(ctx, data);

    R_TRY(ctx.session->ForwardRequest(ctx));
    void *buffer = ctx.out_message_buffer.GetPointer();
    const auto reply = hipcParseResponse(buffer);
    CmifResponse cmif{};
    if (R_FAILED(cmifParseResponse(&cmif, buffer, false, 0)) || reply.num_move_handles != 1) R_SUCCEED();
    auto child = sf::hipc::ServerSession::CreateForwardService();
    // Subservice creation inherits the pointer buffer and does not send IPC.
    serviceCreateNonDomainSubservice(child.get(), self->forward.get(), reply.move_handles[0]);
    Handle server, client;
    auto rc = sf::hipc::CreateSession(&server, &client);
    if (R_FAILED(rc)) { child->own_handle = false; ++status.forward_fallbacks; R_SUCCEED(); }
    if (wrapReadOnly) {
        auto object = sf::CreateSharedObjectEmplaced<IReadOnlyRecord, ReadOnlyRecord>(std::shared_ptr<Service>(child), self->client);
        rc = ctx.manager->RegisterMitmSession(server, sf::cmif::ServiceObjectHolder(std::move(object)), std::shared_ptr<Service>(child));
    } else {
        auto object = sf::CreateSharedObjectEmplaced<IManager, Manager>(std::shared_ptr<Service>(child), self->client);
        rc = ctx.manager->RegisterMitmSession(server, sf::cmif::ServiceObjectHolder(std::move(object)), std::shared_ptr<Service>(child));
    }
    if (R_FAILED(rc)) {
        svcCloseHandle(server); svcCloseHandle(client);
        child->own_handle = false;
        ++status.forward_fallbacks;
        R_SUCCEED();
    }
    const_cast<Handle *>(reply.move_handles)[0] = client;
    if (wrapReadOnly) ++status.readonly_wrapped;
    else ++status.manager_getters;
    R_SUCCEED();
}

#define CONTROL_INFO(C,H) \
    AMS_SF_METHOD_INFO(C,H,0,ams::Result,GetStatus,(sf::Out<hgm::Status> out),(out)) \
    AMS_SF_METHOD_INFO(C,H,1,ams::Result,GetActive,(const sf::OutMapAliasArray<u64> &out),(out)) \
    AMS_SF_METHOD_INFO(C,H,2,ams::Result,Reload,(sf::Out<u32> notified),(notified))
}
AMS_SF_DEFINE_INTERFACE(nh, IControl, CONTROL_INFO, 0x48474D03)
namespace nh {
class Control {
public:
    ams::Result Reload(sf::Out<u32> notified) {
        u32 sent = 0;
        auto rc = reloadPolicy(&sent);
        notified.SetValue(sent);
        status.last_result = rc.GetValue();
        if (R_FAILED(rc)) log("policy.reload.failed", 2, rc.GetValue());
        R_RETURN(rc);
    }
    ams::Result GetStatus(sf::Out<hgm::Status> out) {
        auto snapshot = status;
        const u32 error = recordRelayError.load();
        if (error) { snapshot.refresh_result = error; snapshot.event_ready = 0; }
        out.SetValue(snapshot);
        R_SUCCEED();
    }
    ams::Result GetActive(const sf::OutMapAliasArray<u64> &out) {
        R_UNLESS(out.GetSize() >= hiddenCount, sf::cmif::ResultInvalidOutRawSize());
        if (out.GetSize()) std::memset(out.GetPointer(), 0, out.GetSize() * sizeof(u64));
        if (hiddenCount) std::memcpy(out.GetPointer(), hidden, hiddenCount * sizeof(u64));
        R_SUCCEED();
    }
};
struct Options : sf::hipc::DefaultServerManagerOptions {
    static constexpr bool CanManageMitmServers = true;
    static constexpr size_t PointerBufferSize = 0x1000;
    static constexpr size_t MaxDomains = 8;
    static constexpr size_t MaxDomainObjects = 128;
};
class ManagerServer : public sf::hipc::ServerManager<2, Options, 32> {
    ams::Result OnNeedsToAccept(int port, Server *server) override {
        AMS_UNUSED(port);
        std::shared_ptr<Service> f;
        sm::MitmProcessInfo c;
        if (status.sessions < 8) log("MITM.accept.begin");
        server->AcknowledgeMitmSession(&f, &c);
        ++status.sessions;
        if (status.sessions <= 8) log("MITM.accepted");
        sf::SharedPointer<Getter> object(new Getter(f, c), false);
        return this->AcceptMitmImpl(server, std::move(object), f);
    }
public:
    void Run() {
        status.stage = 4;
        log("control.register.begin");
        auto rc = this->RegisterObjectForServer(sf::CreateSharedObjectEmplaced<IControl, Control>(), sm::ServiceName::Encode(hgm::ServiceName), 4);
        log("control.register.end", 0, rc.GetValue());
        R_ABORT_UNLESS(rc);
        if (status.boot_mode != hgm::DiagnosticsOnly) {
            bool available = false;
            bool alreadyMitm = false;
            rc = sm::HasService(&available, sm::ServiceName::Encode("ns:am2"));
            if (R_SUCCEEDED(rc)) rc = sm::mitm::HasMitm(&alreadyMitm, sm::ServiceName::Encode("ns:am2"));
            // Do not wait indefinitely for a missing service, or displace another MITM.
            if (R_FAILED(rc) || !available || alreadyMitm) {
                status.flags |= hgm::MitmUnavailable;
                status.last_result = rc.GetValue();
                log("MITM.skipped: missing/conflicting service", 0, rc.GetValue());
            } else {
                status.stage = 5;
                log("MITM.register.begin");
                rc = this->RegisterMitmServer<Getter>(0, sm::ServiceName::Encode("ns:am2"));
                log("MITM.register.end", 0, rc.GetValue());
                if (R_FAILED(rc)) {
                    status.flags |= hgm::MitmUnavailable;
                    status.last_result = rc.GetValue();
                    // Clear the declaration so clients do not wait after failure.
                    R_DISCARD(sm::mitm::ClearFutureMitm(sm::ServiceName::Encode("ns:am2")));
                }
            }
        }
        status.stage = 6;
        log("server.loop.ready");
        this->LoopProcess();
    }
};
}

// Initialize the C allocator too: defining operator new alone leaves
// libstratosphere malloc returning nullptr and its libnx hooks aborting.
namespace {
alignas(0x1000) constinit u8 heap[0x100000];
void *allocate(size_t size, size_t align = 16) {
    return ams::init::GetAllocator()->Allocate(size ? size : 1, align);
}
void deallocate(void *p, size_t = 0) { if (p) ams::init::GetAllocator()->Free(p); }
}
namespace ams {
namespace init {
void InitializeSystemModuleBeforeConstructors() {
    InitializeAllocator(heap, sizeof(heap));
}
void InitializeSystemModule() {
    R_ABORT_UNLESS(sm::Initialize());
    fs::SetAllocator([](size_t n) { return allocate(n); }, deallocate);
    fs::SetEnabledAutoAbort(false);
    fs::InitializeForSystem();
    // SD/config errors fail open, and stay visible through diagnostic flags.
    auto rc = fs::MountSdCard("sdmc");
    nh::sdMounted = R_SUCCEEDED(rc);
    if (R_FAILED(rc)) { nh::status.flags |= hgm::PolicyInvalid; nh::status.last_result = rc.GetValue(); }
    nh::status.stage = 1;
    nh::initLog();
}
void FinalizeSystemModule() {}
void Startup() {}
}
ncm::ProgramId CurrentProgramId = {0x00FF48474D000001ul};
namespace result { bool CallFatalOnResultAssertion = true; }
void Main() {
    nh::status.stage = 2;
    nh::log("config.load.begin");
    nh::loadMode(); nh::loadPolicy();
    nh::interceptHome = nh::sdMounted && nh::status.boot_mode == hgm::Filter &&
        !(nh::status.flags & (hgm::EmergencyDisabled | hgm::ModeInvalid));
    nh::log("config.load.end", nh::status.boot_mode, nh::status.last_result);
    nh::status.stage = 3;
    nh::log("server.construct.begin");
    static nh::ManagerServer server;
    nh::log("server.construct.end");
    server.Run();
}
}
extern "C" void *__libnx_alloc(size_t size) { return allocate(size); }
extern "C" void *__libnx_aligned_alloc(size_t align, size_t size) { return allocate(size, align); }
extern "C" void __libnx_free(void *p) { deallocate(p); }
void *operator new(size_t n) { auto *p = allocate(n); AMS_ABORT_UNLESS(p); return p; }
void *operator new[](size_t n) { return ::operator new(n); }
void *operator new(size_t n, const std::nothrow_t &) { return allocate(n); }
void *operator new[](size_t n, const std::nothrow_t &) { return allocate(n); }
void *operator new(size_t n, std::align_val_t a) { auto *p = allocate(n, static_cast<size_t>(a)); AMS_ABORT_UNLESS(p); return p; }
void operator delete(void *p) { deallocate(p); }
void operator delete[](void *p) { deallocate(p); }
void operator delete(void *p, size_t n) { deallocate(p, n); }
void operator delete[](void *p, size_t n) { deallocate(p, n); }
void operator delete(void *p, std::align_val_t) { deallocate(p); }

void *operator new[](size_t n, std::align_val_t a) { return ::operator new(n, a); }
void operator delete[](void *p, std::align_val_t) { deallocate(p); }
void operator delete(void *p, size_t n, std::align_val_t) { deallocate(p, n); }
void operator delete[](void *p, size_t n, std::align_val_t) { deallocate(p, n); }
