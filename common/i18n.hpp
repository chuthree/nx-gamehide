#pragma once
#include <cstddef>
#include <string_view>

namespace hgm::i18n {
enum class Language { English, SimplifiedChinese, TraditionalChinese };
constexpr Language languageFor(std::string_view code) {
    if (code == "zh-CN" || code == "zh-Hans") return Language::SimplifiedChinese;
    if (code == "zh-TW" || code == "zh-Hant") return Language::TraditionalChinese;
    return Language::English;
}
enum class Text {
    AppName,
    SdUnavailable,
    TooMany,
    PolicyChanged,
    CreateConfig,
    CreatePolicy,
    WriteFailed,
    RecoverBackup,
    OriginalKept,
    EnableFailed,
    ModeWriteFailed,
    ModeRecover,
    ModeSaved,
    Saved,
    ApplyFailed,
    BackendDisabled,
    Applied,
    CatalogFailed,
    CatalogIncomplete,
    NoDeletion,
    RestoreAll,
    Restored,
    Cancel,
    PolicyFailed,
    NoGames,
    Hidden,
    Shown,
    HideApplied,
    ShowApplied,
    NextPage,
    PreviousPage,
    Batch,
    NoneHidden,
    Shared,
    UpdateBackend,
    BackendAbsent,
    Pending,
    ToggleHint,
    RestorePrompt,
    Summary,
    Footer,
    Count
};

// English is the fallback for unsupported system languages.
inline constexpr const char *Translations[][3] = {
    {"GameHide", "游戏隐藏", "遊戲隱藏"}, // AppName
    {"Cannot save: SD unavailable", "SD 挂载失败，无法保存", "SD 掛載失敗，無法儲存"}, // SdUnavailable
    {"Cannot save: limit is 4096", "保存失败：最多支持 4096 项", "儲存失敗：最多支援 4096 項"}, // TooMany
    {"List changed or invalid. Reopen", "名单已变化或无效，请重新打开", "名單已變更或無效，請重新開啟"}, // PolicyChanged
    {"Cannot create config folder", "无法创建配置目录", "無法建立設定目錄"}, // CreateConfig
    {"Cannot create list folder", "无法创建名单目录", "無法建立名單目錄"}, // CreatePolicy
    {"Save failed: SD write error", "保存失败：SD 写入或刷新失败", "儲存失敗：SD 寫入錯誤"}, // WriteFailed
    {"Save failed. Restore .bak", "保存失败，请从 .bak 恢复名单", "儲存失敗，請從 .bak 還原名單"}, // RecoverBackup
    {"Save failed. Original list kept", "保存失败，原名单保留", "儲存失敗，原名單保留"}, // OriginalKept
    {"Saved. Check boot_mode.txt", "名单已保存，启用失败，请检查模式文件", "名單已儲存，請檢查模式檔案"}, // EnableFailed
    {"Mode save failed: SD error", "模式保存失败：SD 写入错误", "模式儲存失敗：SD 寫入錯誤"}, // ModeWriteFailed
    {"Check boot_mode.txt.old", "模式保存失败，请检查 boot_mode.txt.old", "請檢查 boot_mode.txt.old"}, // ModeRecover
    {"Mode saved. Reboot to apply", "模式已保存，完整重启后生效", "模式已儲存，完整重啟後生效"}, // ModeSaved
    {"Saved. Reboot to apply", "已保存 · 重启后生效", "已儲存 · 重啟後生效"}, // Saved
    {"Saved. Apply failed; reboot", "应用失败 · 已保存，请重启", "已儲存 · 套用失敗，請重啟"}, // ApplyFailed
    {"Saved. Check sysmodule; reboot", "已保存 · 后台未启用，请检查后重启", "已儲存 · 請檢查背景模組並重啟"}, // BackendDisabled
    {"Applied. Check HOME", "已应用 · 返回 HOME 查看", "已套用 · 返回 HOME 查看"}, // Applied
    {"Cannot load games. Reopen", "游戏列表读取失败，请重新打开", "遊戲清單讀取失敗，請重新開啟"}, // CatalogFailed
    {"Game list incomplete. Reopen", "游戏列表未读完，请重新打开", "遊戲清單不完整，請重新開啟"}, // CatalogIncomplete
    {"Games and saves are kept", "游戏和存档不会被删除", "遊戲和存檔不會被刪除"}, // NoDeletion
    {"Show all games", "恢复全部游戏", "顯示全部遊戲"}, // RestoreAll
    {"All shown. Check HOME", "已全部恢复 · 返回 HOME 查看", "已全部顯示 · 返回 HOME 查看"}, // Restored
    {"Cancel", "取消", "取消"}, // Cancel
    {"Cannot load list. Reopen", "名单读取失败，请重新打开", "名單讀取失敗，請重新開啟"}, // PolicyFailed
    {"No games found", "暂无游戏", "暫無遊戲"}, // NoGames
    {"Hidden", "隐藏", "隱藏"}, // Hidden
    {"Shown", "显示", "顯示"}, // Shown
    {"Hidden. Check HOME", "已隐藏 · 返回 HOME 查看", "已隱藏 · 返回 HOME 查看"}, // HideApplied
    {"Shown. Check HOME", "已显示 · 返回 HOME 查看", "已顯示 · 返回 HOME 查看"}, // ShowApplied
    {"Next page", "下一页", "下一頁"}, // NextPage
    {"Previous page", "上一页", "上一頁"}, // PreviousPage
    {"Actions", "批量操作", "批次操作"}, // Batch
    {"No hidden games", "当前没有隐藏游戏", "目前沒有隱藏遊戲"}, // NoneHidden
    {"Applies to all users", "设置对所有账号生效", "設定對所有帳號生效"}, // Shared
    {"Update sysmodule and reboot", "请更新后台并重启", "請更新背景模組並重啟"}, // UpdateBackend
    {"Sysmodule offline. Reboot", "后台未加载，保存后需重启", "背景模組未載入，請重啟"}, // BackendAbsent
    {"List pending. Reboot to apply", "名单待应用，请重启", "名單待套用，請重啟"}, // Pending
    {" · A: Show / hide", " · A 切换显示 / 隐藏", " · A 切換顯示 / 隱藏"}, // ToggleHint
    {"Show all %zu hidden games?", "显示全部 %zu 款隐藏游戏？", "顯示全部 %zu 款隱藏遊戲？"}, // RestorePrompt
    {"Hidden: %zu / %zu", "已隐藏 %zu / 共 %zu 款", "已隱藏 %zu / 共 %zu 款"}, // Summary
    {"\uE0E1  Back     \uE0E0  OK", "\uE0E1  返回     \uE0E0  确定", "\uE0E1  返回     \uE0E0  確定"}, // Footer
};
static_assert(sizeof(Translations) / sizeof(Translations[0]) == static_cast<size_t>(Text::Count));
constexpr const char *text(Text id, Language language) {
    return Translations[static_cast<size_t>(id)][static_cast<size_t>(language)];
}
}
