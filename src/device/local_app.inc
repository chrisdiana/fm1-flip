// Included inside app.cpp's device-local namespace: platform I/O stays here;
// layout, buffering, repaint policy and text editing have native test seams.
namespace ui = device::presentation;
namespace local = device::local;

enum class LocalPage { Home, Wifi, Backups, BackupActions, History, Setup, WebUi, Info, Storage, StoragePackages };
enum class LocalConfirmation { None, Format, DeleteBackup, DeletePackage, DeleteLogs, EraseAll };
struct WifiChoice { String ssid; int rssi; bool open; };
struct BackupChoice { String path; uint32_t size; uint32_t number; };
struct StorageGroup { uint32_t files = 0, bins = 0; uint64_t bytes = 0; };
struct PackageChoice { String path; uint32_t size; String label; };

// M5Cardputer.Display is a reference initialized by another global constructor.
// The underlying M5.Display object has a stable address before any constructors.
M5Canvas screenBuffer(&M5.Display);
#ifdef FM1_CARDPUTER
ui::BufferedCanvas<M5Canvas> screenCanvas(screenBuffer, true);
#else
ui::BufferedCanvas<M5Canvas> screenCanvas(screenBuffer, false);
#endif
ui::RepaintGate repaintGate;
local::TextEditor textEditor;
LocalPage localPage = LocalPage::Home;
LocalPage editorReturnPage = LocalPage::Home;
LocalPage infoReturnPage = LocalPage::Home;
bool homeMenu = true, editorReturnMenu = false;
bool infoReturnMenu = false, infoReturnDetail = false, infoReturnHome = true;
size_t homeIndex = 0, hotspotSelected = 0;
LocalConfirmation localConfirmation = LocalConfirmation::None;
String localConfirmationPath, localInfoTitle, localInfoBody;
std::vector<WifiChoice> wifiChoices;
std::vector<BackupChoice> backupChoices;
std::vector<ui::Row> historyChoices;
std::vector<PackageChoice> packageChoices;
StorageGroup storagePackages, storageBackups, storageLogs, storageOther;
size_t storageSelected = 0, packageSelected = 0;
bool backupsFromStorage = false;
constexpr size_t STORAGE_ROWS = 6;
size_t localInfoOffset = 0;
size_t wifiSelected = 0, backupSelected = 0, backupAction = 0, historySelected = 0;
uint8_t mainTab = 0;
bool wifiScanning = false, wifiConnecting = false, screenReady = false;
uint32_t wifiConnectStarted = 0;
bool localReview = false;
String editingSsid;
uint32_t localHeldSince = 0;
bool localHeldFired = false, localReleased = false;
String lastNotice;
uint32_t noticeUntil = 0;

const char* LOCAL_ACTIONS[] = {
    "Discover", "Downloaded", "Search", "Favorite / unfavorite",
    "Download package", "Install on FM-1", "Connect FM-1", "Create backup", "Backups",
    "Wi-Fi setup", "Refresh catalogue", "Category", "Release channel", "Chip support",
    "Sort", "Favorites only", "Storage & export", "Web UI / hotspot", "Last operation", "Loaded on FM-1", "Home"
};
constexpr size_t LOCAL_ACTION_COUNT = sizeof(LOCAL_ACTIONS) / sizeof(LOCAL_ACTIONS[0]);

std::string standard(const String& value) { return std::string(value.c_str()); }
String readableBytes(uint64_t bytes) {
    if (bytes >= 1024 * 1024) return String(double(bytes) / (1024 * 1024), 1) + " MB";
    if (bytes >= 1024) return String((unsigned long)(bytes / 1024)) + " KB";
    return String((unsigned long)bytes) + " B";
}

void showLocalInfo(const String& title, const String& body) {
    if (localPage != LocalPage::Info) {
        infoReturnPage = localPage;
        infoReturnMenu = actionMenu;
        infoReturnDetail = detail;
        infoReturnHome = homeMenu;
    }
    localInfoOffset = 0;
    localInfoTitle = title;
    localInfoBody = body;
    localPage = LocalPage::Info;
    actionMenu = false;
    dirty = true;
}

void setMainTab(uint8_t tab) {
    mainTab = tab % 3;
    savedOnly = mainTab == 1;
    localPage = LocalPage::Home;
    detail = false;
    actionMenu = false;
    selected = 0;
    detailOffset = 0;
    dirty = true;
}

void openEditor(local::EditTarget target, const String& initial, size_t limit) {
    editorReturnPage = localPage;
    editorReturnMenu = actionMenu;
    textEditor.begin(target, standard(initial), limit);
    searchPicker = true;
    actionMenu = false;
    dirty = true;
}

void completeEditor() {
    String value(textEditor.value().c_str());
    searchPicker = false;
    localPage = editorReturnPage;
    if (textEditor.target() == local::EditTarget::Search) {
        query = value;
        selected = detailOffset = 0;
        detail = false;
        localPage = LocalPage::Home;
        if (mainTab == 2) setMainTab(0);
    } else if (textEditor.target() == local::EditTarget::Ssid) {
        if (value.isEmpty()) { notice = "Enter a network name"; dirty = true; return; }
        editingSsid = value;
        openEditor(local::EditTarget::Password, "", 63);
    } else {
        startWifiJoin(editingSsid,value);
    }
    dirty = true;
}

void startWifiJoin(const String& ssid,const String& password) {
    if(!wifiEnabled&&!setWifiPower(true,false)){showLocalInfo("Wi-Fi error","Could not enable the Wi-Fi radio.");return;}
    preferences.putString("ssid",ssid);preferences.putString("password",password);
    editingSsid=ssid;
    WiFi.scanDelete();wifiScanning=false;
    WiFi.disconnect(false);wifiDisconnectReason.store(0);
    wifiResult="Joining Wi-Fi";wifiConnecting=true;wifiConnectStarted=millis();
    searchPicker=false;actionMenu=false;localPage=LocalPage::Wifi;
    WiFi.begin(ssid.c_str(),password.c_str());dirty=true;
}

String webAccessDetails() {
    if(!wifiEnabled)return "Wi-Fi radio is OFF to save power. Turn Wi-Fi on here for browser access, or choose Download / Refresh to reconnect when needed.";
    String result=apEnabled ? "Hotspot ON. Join FM1-Flip, password "+apPassword+". Open http://192.168.4.1. " : "Hotspot OFF; no FM1-Flip network is broadcast. ";
    if(WiFi.status()==WL_CONNECTED)result+="Home-network browser address: http://"+WiFi.localIP().toString()+". ";
    else result+="Join home Wi-Fi or enable the hotspot for browser access. ";
    return result+"This setting is saved across restarts.";
}

void showHome();
bool requestNetworkJob(const Job& next) {
    if(locked())return false;
    if(!wifiEnabled){
        pendingNetworkJob=next;networkPrompt=true;networkPromptIndex=0;
        actionMenu=false;searchPicker=false;detail=false;localPage=LocalPage::Home;dirty=true;return true;
    }
    return startJob(next);
}

void chooseNetworkPrompt() {
    if(networkPromptIndex){networkPrompt=false;showHome();return;}
    networkPrompt=false;
    String ssid=preferences.getString("ssid","");
    if(ssid.isEmpty()){showLocalInfo("Wi-Fi setup needed","No network is saved. Open Home > Wi-Fi setup, then retry the download or refresh.");return;}
    if(!setWifiPower(true,false)){showLocalInfo("Wi-Fi error","Could not turn on the radio.");return;}
    networkWaiting=true;networkReadyStarted=millis();
    startWifiJoin(ssid,preferences.getString("password",""));
}

void beginWifiScan() {
    if (locked()) return;
    if(!wifiEnabled&&!setWifiPower(true,false)){showLocalInfo("Wi-Fi error","Could not enable the radio.");return;}
    actionMenu = false;
    searchPicker = false;
    localPage = LocalPage::Wifi;
    wifiSelected = 0;
    wifiChoices.clear();
    WiFi.scanDelete();
    int result = WiFi.scanNetworks(true);
    wifiScanning = result == WIFI_SCAN_RUNNING;
    if (result == WIFI_SCAN_FAILED) notice = "Scan failed; enter SSID manually";
    dirty = true;
}

void pollWifiScan() {
    if (wifiConnecting) {
        int status = WiFi.status();
        if (status == WL_CONNECTED && uint32_t(WiFi.localIP())!=0) {
            wifiConnecting=false;localPage=LocalPage::Home;actionMenu=true;homeMenu=true;
            wifiResult="Connected: "+WiFi.localIP().toString();
            if(networkWaiting)networkReadyStarted=millis();
            dirty=true;
        } else if (status == WL_CONNECT_FAILED || status == WL_NO_SSID_AVAIL || uint32_t(millis() - wifiConnectStarted) > 30000) {
            wifiConnecting = false;networkWaiting=false;
            wifiResult=wifiJoinFailure(status,wifiDisconnectReason.load());
            WiFi.disconnect(false);
            showLocalInfo("Wi-Fi not joined",wifiResult);
        }
    }
    if(networkWaiting&&!wifiConnecting){
        if(WiFi.status()==WL_CONNECTED && networkPreflight().isEmpty()){
            networkWaiting=false;Job next=pendingNetworkJob;pendingNetworkJob=Job{};
            if(!startJob(next))showLocalInfo("Download not started","Please select the firmware or refresh again.");
        }else if(uint32_t(millis()-networkReadyStarted)>20000){
            networkWaiting=false;showLocalInfo("Network not ready",networkPreflight());
        }
    }
    if (!wifiScanning) return;
    int count = WiFi.scanComplete();
    if (count == WIFI_SCAN_RUNNING) return;
    wifiScanning = false;
    if (count >= 0) {
        for (int i = 0; i < count && wifiChoices.size() < 12; ++i) {
            String ssid = WiFi.SSID(i);
            if (ssid.isEmpty()) continue;
            bool duplicate = false;
            for (const auto& choice : wifiChoices) if (choice.ssid == ssid) duplicate = true;
            if (!duplicate) wifiChoices.push_back({ssid, WiFi.RSSI(i), WiFi.encryptionType(i) == WIFI_AUTH_OPEN});
        }
        WiFi.scanDelete();
    } else notice = "Scan failed; enter SSID manually";
    dirty = true;
}

void chooseWifi() {
    if (wifiScanning) return;
    if (wifiSelected >= wifiChoices.size()) {
        editingSsid = "";
        openEditor(local::EditTarget::Ssid, "", 32);
        return;
    }
    const auto& choice = wifiChoices[wifiSelected];
    editingSsid = choice.ssid;
    if (choice.open) {
        startWifiJoin(editingSsid,"");
    } else openEditor(local::EditTarget::Password, "", 63);
}

void openBackups() {
    if (!storageReady || locked()) { notice = "Storage unavailable or busy"; dirty = true; return; }
    backupChoices.clear();
    backupChoices.reserve(48);
    File directory = disk->open("/fm1/backups");
    for (File file = directory.openNextFile(); file; file = directory.openNextFile()) {
        String name = file.name();
        if (file.isDirectory() || !name.endsWith(".bin")) continue;
        name = name.substring(name.lastIndexOf('/') + 1);
        uint32_t number = name.startsWith("backup-") ? name.substring(7).toInt() : 0;
        BackupChoice choice{String("/fm1/backups/") + name, uint32_t(file.size()), number};
        if (backupChoices.size() < 48) backupChoices.push_back(choice);
        else {
            auto oldest = std::min_element(backupChoices.begin(), backupChoices.end(),
                [](const BackupChoice& a, const BackupChoice& b) { return a.number < b.number; });
            if (oldest != backupChoices.end() && number > oldest->number) *oldest = choice;
        }
    }
    std::sort(backupChoices.begin(), backupChoices.end(),
        [](const BackupChoice& a, const BackupChoice& b) { return a.number > b.number; });
    backupSelected = 0;
    localPage = LocalPage::Backups;
    actionMenu = false;
    dirty = true;
}

String backupLabel(const BackupChoice& backup) {
    if (backup.number) return "Backup " + String((unsigned long)backup.number);
    return backup.path.substring(backup.path.lastIndexOf('/') + 1);
}

void armLocalConfirmation(LocalConfirmation action, const String& path);

// Files directly in dirName (not subdirectories). Collects saved .fwsc packages
// when asked; their .json metadata counts towards the bytes.
StorageGroup scanFiles(const char* dirName, std::vector<PackageChoice>* packages = nullptr) {
    StorageGroup group;
    File dir = disk->open(dirName);
    if (!dir || !dir.isDirectory()) return group;
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        if (f.isDirectory()) continue;
        String name = f.name();
        name = name.substring(name.lastIndexOf('/') + 1);
        group.files++;
        if (name.endsWith(".bin")) group.bins++;
        group.bytes += f.size();
        if (packages && name.endsWith(".fwsc") && packages->size() < 64)
            packages->push_back({String(dirName) + "/" + name, uint32_t(f.size()), name});
    }
    return group;
}

void scanStorage() {
    packageChoices.clear();
    storagePackages = scanFiles("/fm1/packages", &packageChoices);
    storageBackups = scanFiles("/fm1/backups");
    storageLogs = scanFiles("/fm1/logs");
    storageOther = scanFiles("/fm1");  // catalogue, FM-1 record, CA bundle
    for (auto& package : packageChoices)
        for (size_t i = 0; i < entryCount; i++)
            if (entryIndex[i].localPath == package.path) {
                Entry entry = readEntry(i);
                package.label = entry.name + " " + entry.version;
                break;
            }
}

String groupSummary(const StorageGroup& group, uint32_t count, const char* noun) {
    return String((unsigned long)count) + " " + noun + " / " + readableBytes(group.bytes);
}

void openStorage(bool reset = false) {
    if (!storageReady) { localPage = LocalPage::Setup; actionMenu = false; dirty = true; return; }
    if (locked()) { notice = "Storage busy; finish the current operation first"; dirty = true; return; }
    scanStorage();
    if (reset) storageSelected = 0;
    backupsFromStorage = false;
    localPage = LocalPage::Storage;
    actionMenu = false;
    dirty = true;
}

void openPackages() {
    if (locked()) { notice = "Storage busy"; dirty = true; return; }
    scanStorage();
    packageSelected = 0;
    localPage = LocalPage::StoragePackages;
    actionMenu = false;
    dirty = true;
}

String storageDetails() {
    const uint64_t total = totalSpace(), free = freeSpace();
    return readableBytes(free) + " free of " + readableBytes(total) + ". Firmware: " +
           groupSummary(storagePackages, packageChoices.size(), "packages") + ". Backups: " +
           groupSummary(storageBackups, storageBackups.bins, "backups") +
           ". Logs: " + groupSummary(storageLogs, storageLogs.files, "files") +
           " (newest 10 kept). Catalogue, FM-1 record and CA bundle: " + readableBytes(storageOther.bytes) + ".";
}

void chooseStorageAction() {
    switch (storageSelected) {
    case 0: showLocalInfo("Storage", storageDetails()); break;
    case 1: openPackages(); break;
    case 2:
        openBackups();
        backupsFromStorage = localPage == LocalPage::Backups;
        break;
    case 3:
        if (!storageLogs.files) { notice = "No session logs"; dirty = true; }
        else armLocalConfirmation(LocalConfirmation::DeleteLogs, "");
        break;
    case 4: armLocalConfirmation(LocalConfirmation::EraseAll, ""); break;
    default:
#ifdef FM1_CARDPUTER
        showLocalInfo("Export files", "Power down and copy /fm1 from the SD card, or use Home > Web UI / hotspot > Files in a browser.");
#else
        showLocalInfo("Export files", "Join the FM1-Flip hotspot (or your Wi-Fi), open the device address in a browser and use Files > Export.");
#endif
        break;
    }
}

bool removeLogs() {
    std::vector<String> logs;
    File dir = disk->open("/fm1/logs");
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        String name = f.name();
        if (!f.isDirectory()) logs.push_back("/fm1/logs/" + name.substring(name.lastIndexOf('/') + 1));
    }
    dir.close();
    bool ok = true;
    for (const auto& path : logs) ok = disk->remove(path) && ok;
    return ok;
}

#ifdef FM1_CARDPUTER
// The SD card itself is never formatted; only the app's /fm1 tree is removed.
bool removeTree(const String& path) {
    std::vector<String> files, dirs;
    File dir = disk->open(path);
    if (!dir || !dir.isDirectory()) return false;
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        String name = f.name();
        String child = path + "/" + name.substring(name.lastIndexOf('/') + 1);
        (f.isDirectory() ? dirs : files).push_back(child);
    }
    dir.close();
    bool ok = true;
    for (const auto& file : files) ok = disk->remove(file) && ok;
    for (const auto& sub : dirs) ok = removeTree(sub) && disk->rmdir(sub) && ok;
    return ok;
}
#endif

bool eraseAllStorage() {
#ifdef FM1_CARDPUTER
    bool ok = removeTree("/fm1");
#else
    LittleFS.end();
    bool ok = LittleFS.format() && LittleFS.begin(false);
    storageReady = ok;
#endif
    if (storageReady)
        for (auto path : {"/fm1", "/fm1/packages", "/fm1/backups", "/fm1/logs"}) disk->mkdir(path);
    loadCatalogue();
    transportStorageReset();
    return ok;
}

void openHistory() {
    historyChoices.clear();
    JsonDocument record;
    if (!deserializeJson(record, transportDeviceRecordJson())) {
        JsonArray history = record["history"].as<JsonArray>();
        for (int i = int(history.size()) - 1; i >= 0; --i) {
            JsonObject event = history[i];
            String product = event["product"] | "Unknown firmware";
            String kind = event["kind"] | "operation";
            historyChoices.push_back({standard(product), standard(kind + " / boot identity confirmed"), "OK", false});
        }
    }
    historySelected = 0;
    localPage = LocalPage::History;
    dirty = true;
}

void armLocalConfirmation(LocalConfirmation action, const String& path = "") {
    localConfirmation = action;
    localReview = true;
    localConfirmationPath = path;
    localHeldSince = 0;
    localHeldFired = false;
    localReleased = false;
    actionMenu = false;
    dirty = true;
}

void chooseBackupAction() {
    if (backupSelected >= backupChoices.size()) return;
    const auto backup = backupChoices[backupSelected];
    if (backupAction == 0) {
        String label = backupLabel(backup);
        if (pendingAction.arm(fm1::ActionKind::Restore, backup.path.c_str(), label.c_str())) {
            confirmInstall = true;
            dirty = true;
        }
    } else if (backupAction == 1) {
#ifdef FM1_CARDPUTER
        showLocalInfo("Export backup", "Power down and copy " + backup.path + " from the SD card. Local web export is also available.");
#else
        showLocalInfo("Export backup", "Join FM1-Flip Wi-Fi and open 192.168.4.1 > Files to export " + backupLabel(backup) + ".");
#endif
    } else if (!transportCanDeleteBackup(backup.path)) {
        notice = "Backup unavailable or operation in progress";
        dirty = true;
    } else armLocalConfirmation(LocalConfirmation::DeleteBackup, backup.path);
}

void completeLocalConfirmation() {
    if (jobBusy.load() || exportActive || transportStatus().busy || pendingAction.active()) return;
    if (localConfirmation == LocalConfirmation::Format) {
#ifndef FM1_CARDPUTER
        if (!storageReady && LittleFS.format() && LittleFS.begin(false)) {
            storageReady = true;
            for (auto path : {"/fm1", "/fm1/packages", "/fm1/backups", "/fm1/logs"}) disk->mkdir(path);
            loadCatalogue();
            localPage = LocalPage::Home;
            notice = "Internal storage initialized";
        } else notice = "Storage initialization failed";
#endif
    } else if (localConfirmation == LocalConfirmation::DeleteBackup) {
        String path = localConfirmationPath;
        if (path.startsWith("/fm1/backups/") && path.endsWith(".bin") && safePath(path) &&
            transportDeleteBackup(path)) {
            localConfirmation = LocalConfirmation::None;
            localReview = false;
            openBackups();
            notice = "Backup deleted";
        } else notice = "Backup unavailable or operation in progress";
    } else if (localConfirmation == LocalConfirmation::DeletePackage) {
        String path = localConfirmationPath;
        if (path.startsWith("/fm1/packages/") && path.endsWith(".fwsc") && safePath(path) && disk->remove(path)) {
            disk->remove(path + ".json");
            loadCatalogue();
            localConfirmation = LocalConfirmation::None;
            localReview = false;
            openPackages();
            notice = "Firmware package deleted";
        } else notice = "Cannot delete package";
    } else if (localConfirmation == LocalConfirmation::DeleteLogs) {
        bool ok = removeLogs();
        localConfirmation = LocalConfirmation::None;
        localReview = false;
        openStorage();
        notice = ok ? "Session logs deleted" : "Some logs could not be deleted";
    } else if (localConfirmation == LocalConfirmation::EraseAll) {
        bool ok = eraseAllStorage();
        localConfirmation = LocalConfirmation::None;
        localReview = false;
        if (storageReady) openStorage(true);
        else localPage = LocalPage::Setup;
        notice = ok ? "Storage erased" : "Storage erase failed";
    }
    localConfirmation = LocalConfirmation::None;
    localReview = false;
    localConfirmationPath = "";
    dirty = true;
}

void showHome() {
    networkPrompt=false;networkWaiting=false;pendingNetworkJob=Job{};
    if (wifiConnecting) { WiFi.disconnect(false); wifiConnecting = false; }
    confirmInstall = false;
    pendingAction.cancel();
    localConfirmation = LocalConfirmation::None;
    localReview = false;
    localConfirmationPath = "";
    localHeldSince = 0;
    localHeldFired = false;
    localReleased = false;
    searchPicker = false;
    detail = false;
    localPage = LocalPage::Home;
    actionMenu = true;
    homeMenu = true;
    actionIndex = homeIndex;
    dirty = true;
}

void backLocal() {
    if(networkPrompt||networkWaiting){showHome();return;}
    if (confirmInstall) { confirmInstall = false; pendingAction.cancel(); }
    else if (wifiConnecting) { WiFi.disconnect(false); wifiConnecting = false; localPage = LocalPage::Wifi; }
    else if (localConfirmation != LocalConfirmation::None) { localConfirmation = LocalConfirmation::None; localReview = false; }
    else if (searchPicker) { searchPicker = false; localPage = editorReturnPage; actionMenu = editorReturnMenu; }
    else if (actionMenu) {
        if (!homeMenu) actionMenu = false;
    }
    else if (localPage == LocalPage::Info) {
        localPage = infoReturnPage;
        actionMenu = infoReturnMenu;
        detail = infoReturnDetail;
        homeMenu = infoReturnHome;
    }
    else if (localPage == LocalPage::BackupActions) localPage = LocalPage::Backups;
    else if (localPage == LocalPage::StoragePackages) openStorage();
    else if (localPage == LocalPage::Backups && backupsFromStorage) openStorage();
    else if (localPage == LocalPage::History) { localPage = LocalPage::Home; mainTab = 2; }
    else if (localPage != LocalPage::Home) showHome();
    else if (detail) detail = false;
    else showHome();
    dirty = true;
}

void openActionMenu(bool contextual = false) {
    if (confirmInstall || localConfirmation != LocalConfirmation::None || wifiConnecting) return;
    if (!contextual) { showHome(); return; }
    actionMenu = true;
    homeMenu = false;
    actionIndex = 0;
    int index = selectedIndex();
    if (index >= 0) actionIndex = readEntry(index).path.isEmpty() ? 4 : 5;
    dirty = true;
}

void moveLocal(int direction) {
    if(networkPrompt){networkPromptIndex=(networkPromptIndex+1)%2;dirty=true;return;}
    if(networkWaiting)return;
    if (wifiConnecting) return;
    if (confirmInstall || localConfirmation != LocalConfirmation::None) return;
    if (searchPicker) { textEditor.moveCharacter(direction); dirty = true; return; }
    auto step = [direction](size_t& value, size_t count) {
        if (count) value = (value + count + direction) % count;
    };
    if (actionMenu) step(actionIndex, LOCAL_ACTION_COUNT);
    else if (localPage == LocalPage::Wifi) step(wifiSelected, wifiChoices.size() + 1);
    else if (localPage == LocalPage::Backups) step(backupSelected, backupChoices.size());
    else if (localPage == LocalPage::WebUi) step(hotspotSelected,3);
    else if (localPage == LocalPage::BackupActions) step(backupAction, 3);
    else if (localPage == LocalPage::Storage) step(storageSelected, STORAGE_ROWS);
    else if (localPage == LocalPage::StoragePackages) step(packageSelected, packageChoices.size());
    else if (localPage == LocalPage::History) step(historySelected, historyChoices.size());
    else if (localPage == LocalPage::Info) {
        if (direction > 0) localInfoOffset = localInfoOffset + 60 < localInfoBody.length() ? localInfoOffset + 60 : 0;
        else localInfoOffset = localInfoOffset >= 60 ? localInfoOffset - 60 : 0;
    }
    else if (detail) {
        int index = selectedIndex();
        if (index >= 0) {
            size_t length = readEntry(index).description.length();
            if (direction > 0) detailOffset = detailOffset + 60 < length ? detailOffset + 60 : 0;
            else detailOffset = detailOffset >= 60 ? detailOffset - 60 : 0;
        }
    } else if (mainTab == 2) setMainTab(0);
    else selectStep(direction);
    dirty = true;
}

void activateLocal() {
    if(networkPrompt){chooseNetworkPrompt();return;}
    if(networkWaiting){backLocal();return;}
    if (wifiConnecting) { backLocal(); return; }
    if (confirmInstall || localConfirmation != LocalConfirmation::None) return;
    if (searchPicker) {
        auto result = textEditor.choose();
        if (result == local::EditorResult::Done) completeEditor();
        else if (result == local::EditorResult::Cancelled) backLocal();
    }
    else if (actionMenu) { if (homeMenu) homeIndex = actionIndex; chooseAction(); }
    else if (localPage == LocalPage::WebUi) {
        if(hotspotSelected==0){if(!setWifiPower(!wifiEnabled))showLocalInfo("Wi-Fi error","Could not change Wi-Fi power.");else {localPage=LocalPage::WebUi;dirty=true;}return;}
        if(hotspotSelected==1 && !setHotspot(!apEnabled))showLocalInfo("Hotspot error","Could not change the access point. Retry after the current network operation.");
        else showLocalInfo("Web UI",webAccessDetails());
    }
    else if (localPage == LocalPage::Wifi) chooseWifi();
    else if (localPage == LocalPage::Backups && !backupChoices.empty()) {
        localPage = LocalPage::BackupActions;
        backupAction = 0;
    } else if (localPage == LocalPage::BackupActions) chooseBackupAction();
    else if (localPage == LocalPage::Storage) chooseStorageAction();
    else if (localPage == LocalPage::StoragePackages) {
        if (packageSelected < packageChoices.size())
            armLocalConfirmation(LocalConfirmation::DeletePackage, packageChoices[packageSelected].path);
    }
    else if (localPage == LocalPage::History) backLocal();
    else if (localPage == LocalPage::Setup) {
#ifndef FM1_CARDPUTER
        armLocalConfirmation(LocalConfirmation::Format);
#else
        showLocalInfo("SD card needed", "Insert a FAT32 microSD card, then restart. The card is never formatted automatically.");
#endif
    } else if (localPage == LocalPage::Info) backLocal();
    else if (mainTab == 2) openHistory();
    else if (detail) openActionMenu(true);
    else { detail = true; detailOffset = 0; }
    dirty = true;
}

String actionSubtitle(size_t index) {
    switch (index) {
    case 0: return "Browse firmware from the Hub";
    case 1: return "Packages saved on this device";
    case 2: return query.isEmpty() ? "Name, maker, tags or description" : query;
    case 3: return "Pin or unpin the selected firmware";
    case 4: return "Save a package for offline use";
    case 5: return "No backup saved; Create backup is optional";
    case 6: return "Identify the connected FM-1";
    case 7: return "Read and verify the full flash";
    case 8: return "Restore or manage stored backups";
    case 9: return WiFi.status() == WL_CONNECTED ? WiFi.SSID() : "Scan networks and enter a password";
    case 10: return "Cached locally; at most once a day";
    case 11: return categoryFilter;
    case 12: return releaseFilter == "stable" ? "Not marked beta" : releaseFilter;
    case 13: return compatibilityFilter + " / maker declaration";
    case 14: return sortOrder == "popular" ? "Most downloaded on the Hub" : sortOrder;
    case 15: return favoritesOnly ? "Showing favorites" : "Showing all entries";
    case 16: return storageReady ? readableBytes(freeSpace()) + " free of " + readableBytes(totalSpace()) : "Initialize storage explicitly";
    case 17: return apEnabled ? "Hotspot ON / browser access" : "Hotspot OFF / enable when needed";
    case 18: return "Detailed result of the last operation";
    case 19: return "FM-1 identity, separate from downloads";
    default: return "Return to Home";
    }
}

void appendWindow(ui::Frame& frame, const std::vector<ui::Row>& rows, size_t current) {
    size_t begin = current > 1 ? current - 1 : 0;
    if (begin + 3 > rows.size()) begin = rows.size() > 3 ? rows.size() - 3 : 0;
    for (size_t i = begin; i < rows.size() && frame.rows.size() < 3; ++i) {
        auto row = rows[i];
        row.selected = i == current;
        frame.rows.push_back(row);
    }
}

ui::Frame buildLocalFrame(const TransportStatus& transport) {
    ui::Frame frame;
    frame.wifiConnected=wifiEnabled && WiFi.status()==WL_CONNECTED;
    frame.connected = transport.mode != TransportMode::Disconnected;
    frame.right = transport.mode == TransportMode::Rom ? "USB ROM" : transport.firmwareLive ? "USB LIVE" : "OFFLINE";
#ifdef FM1_CARDPUTER
    const char* browseHint = ";/. Move / Open , Back Fn+F Home";
    const char* menuHint = ";/. Move  / Choose Esc/, Back";
    const char* confirmHint = "Hold Enter or / 2s  Esc/, Cancel";
#else
    const char* browseHint = "A Choose B Next Hold A Back";
    const char* menuHint = "A Choose B Next Hold A Back";
    const char* confirmHint = "Hold B 2s  Hold A Cancel";
#endif
    frame.footer = browseHint;
    if(networkPrompt){
        frame.screen=ui::Screen::Menu;frame.title="Wi-Fi is off";frame.heading="Connect and continue?";
        std::vector<ui::Row> rows={{"Connect Wi-Fi","Continue the download / refresh","",false},{"Cancel","Keep the radio off","",false}};
        appendWindow(frame,rows,networkPromptIndex);frame.footer=menuHint;return frame;
    }
    if(networkWaiting&&!wifiConnecting){frame.screen=ui::Screen::Progress;frame.title="Preparing download";frame.heading="Waiting for network";frame.body="Waiting for the IP address and clock synchronization.";frame.footer="Back cancels";return frame;}
    if (confirmInstall || localConfirmation != LocalConfirmation::None) {
        frame.screen = ui::Screen::Confirm;
        frame.warning = true;
        frame.title = "Review";
        frame.eyebrow = "PHYSICAL CONFIRMATION REQUIRED";
        frame.badge = "HOLD TO CONTINUE";
        frame.footer = confirmHint;
        if (confirmInstall) {
            auto snapshot = pendingAction.snapshot();
            frame.heading = snapshot.kind == fm1::ActionKind::Install ? "Install firmware?" : "Restore backup?";
            frame.subtitle = snapshot.label;
            frame.body = snapshot.kind == fm1::ActionKind::Install
                ? "Connects FM-1, checks hardware and installs. No backup is saved. For a backup, choose Create backup first."
                : "Writes FM-1 flash. Keep USB connected. A fresh backup / compatible route is required.";
        } else if (localConfirmation == LocalConfirmation::Format) {
            frame.heading = "Initialize storage?";
            frame.body = "Erases the internal filesystem, including any saved packages. Release, then hold to initialize.";
        } else if (localConfirmation == LocalConfirmation::DeletePackage) {
            frame.heading = "Delete firmware?";
            frame.subtitle = standard(localConfirmationPath.substring(localConfirmationPath.lastIndexOf('/') + 1));
            for (const auto& package : packageChoices)
                if (package.path == localConfirmationPath) frame.subtitle = standard(package.label);
            frame.body = "Removes this downloaded package. Download it again from the Hub if you need it later.";
        } else if (localConfirmation == LocalConfirmation::DeleteLogs) {
            frame.heading = "Delete session logs?";
            frame.body = "Removes all connect / backup session logs. Backups and firmware are kept.";
        } else if (localConfirmation == LocalConfirmation::EraseAll) {
            frame.heading = "Erase all storage?";
#ifdef FM1_CARDPUTER
            frame.body = "Deletes /fm1 on the SD card: every backup, downloaded firmware, log, catalogue and FM-1 record. Export backups first.";
#else
            frame.body = "Formats internal storage: every backup, downloaded firmware, log, catalogue and FM-1 record. Export backups first.";
#endif
        } else {
            frame.heading = "Delete backup?";
            frame.body = "Permanently removes this backup, even if it is your last copy. Export it first if needed.";
        }
        return frame;
    }
    if (transport.busy || jobBusy.load()) {
        frame.screen = ui::Screen::Progress;
        frame.busy = true;
        frame.title = jobBusy.load() ? "Importing" : "FM-1";
        frame.eyebrow = jobBusy.load() ? "DOWNLOAD / VALIDATE / SAVE" : "KEEP USB AND POWER CONNECTED";
        frame.heading = standard(transport.stage);
        frame.body = standard(transport.detail);
        frame.completed = transport.completed;
        frame.total = transport.total;
        if (jobBusy.load()) {
            xSemaphoreTake(jobMutex, portMAX_DELAY);
            frame.heading = standard(jobStage);
            frame.body = standard(jobDetail);
            if (jobStage == "Catalogue/network") { frame.heading = "Fetching catalogue"; frame.body = "Saving the latest Firmware Hub metadata."; }
            else if (jobStage == "Downloading") { frame.heading = "Downloading package"; frame.body = "Saving bytes and checking the source SHA-256."; }
            else if (jobStage == "Queued") frame.body = "Preparing network and storage.";
            else if (jobStage == "Validating") frame.body = "Checking package structure and integrity.";
            frame.completed = jobCompleted;
            frame.total = jobTotal;
            xSemaphoreGive(jobMutex);
        }
        #ifdef FM1_CARDPUTER
        frame.footer = (transportCanCancel() || jobBusy.load()) ? "Esc/, Cancel; keep USB connected" : "Please wait - do not disconnect";
#else
        frame.footer = (transportCanCancel() || jobBusy.load()) ? "Hold A Cancel; keep USB connected" : "Please wait - do not disconnect";
#endif
        return frame;
    }
    if (wifiConnecting) {
        frame.screen = ui::Screen::Progress; frame.busy = true;
        frame.title = "Joining Wi-Fi"; frame.eyebrow = standard(editingSsid);
        frame.heading = "Connecting to network";
        frame.body = "Waiting for authentication and an IP address (up to 30 seconds).";
        frame.footer = "Back cancels joining";
        return frame;
    }
    if (searchPicker) {
        frame.screen = ui::Screen::Editor;
        frame.title = textEditor.target() == local::EditTarget::Password ? "Wi-Fi key" : textEditor.target() == local::EditTarget::Ssid ? "Network name" : "Search";
        frame.eyebrow = textEditor.target() == local::EditTarget::Password ? standard(editingSsid) : "TYPE OR USE THE CHARACTER PICKER";
        frame.right = std::to_string(textEditor.value().size()) + "/" + std::to_string(textEditor.limit());
        frame.editorValue = textEditor.displayedValue();
        if (frame.editorValue.size() > 32) frame.editorValue = frame.editorValue.substr(frame.editorValue.size() - 32);
        frame.editorCharacter = textEditor.characterLabel();
        frame.editorGroup = textEditor.groupLabel();
#ifdef FM1_CARDPUTER
        frame.editorGroup = "keyboard";
        frame.body = "Backspace deletes. Esc cancels.";
        frame.footer = textEditor.target() == local::EditTarget::Password ? "Tab Show/Hide  Enter Join" : "Type  Backspace Delete  Enter Save";
#else
        frame.body = "Hold B: previous. Choose OK to save; DEL to erase.";
        frame.footer = textEditor.target() == local::EditTarget::Password ? "A Add B Next Hold A Back" : "A Add B Next Hold A Back";
#endif
        return frame;
    }
    if (actionMenu) {
        frame.screen = ui::Screen::Menu;
        frame.title = "FM-1 Flip";
        frame.eyebrow = homeMenu ? "HOME" : "FIRMWARE OPTIONS";
        frame.right = std::to_string(actionIndex + 1) + "/" + std::to_string(LOCAL_ACTION_COUNT);
        std::vector<ui::Row> rows;
        rows.reserve(LOCAL_ACTION_COUNT);
        for (size_t i = 0; i < LOCAL_ACTION_COUNT; ++i) rows.push_back({LOCAL_ACTIONS[i], standard(actionSubtitle(i)), "", false});
        appendWindow(frame, rows, actionIndex);
        frame.footer = menuHint;
        return frame;
    }
    if (localPage == LocalPage::WebUi) {
        frame.screen=ui::Screen::Menu;frame.title="Web UI / hotspot";
        frame.eyebrow=!wifiEnabled?"WI-FI RADIO OFF":apEnabled?"FM1-FLIP HOTSPOT ON":"HOTSPOT OFF";
        std::vector<ui::Row> rows={{wifiEnabled?"Turn Wi-Fi off":"Turn Wi-Fi on","Radio power / saved setting","",false},{apEnabled?"Turn hotspot off":"Turn hotspot on","Saved across restarts","",false},{"Browser access details","Addresses and hotspot password","",false}};
        appendWindow(frame,rows,hotspotSelected);frame.footer=menuHint;return frame;
    }
    if (localPage == LocalPage::Wifi) {
        frame.screen = ui::Screen::Networks;
        frame.title = "Wi-Fi";
        frame.eyebrow = wifiScanning ? "SCANNING NEARBY NETWORKS..." : "CHOOSE A NETWORK";
        std::vector<ui::Row> rows;
        for (const auto& choice : wifiChoices) rows.push_back({standard(choice.ssid), choice.open ? "Open network" : "Password required", std::to_string(choice.rssi), false});
        rows.push_back({"Enter SSID manually", "For hidden networks", "", false});
        if (!wifiScanning) appendWindow(frame, rows, wifiSelected);
        else { frame.heading = "Looking for networks"; frame.body = "Looking for nearby 2.4 GHz networks."; }
        frame.footer = menuHint;
        return frame;
    }
    if (localPage == LocalPage::Backups) {
        frame.screen = ui::Screen::Backups;
        frame.title = "Backups";
        frame.eyebrow = "RECENT BACKUPS ON THIS DEVICE";
        frame.right = std::to_string(backupChoices.size()) + " FILES";
        std::vector<ui::Row> rows;
        for (const auto& backup : backupChoices) rows.push_back({standard(backupLabel(backup)), standard(readableBytes(backup.size)), "", false});
        appendWindow(frame, rows, backupSelected);
        if (rows.empty()) { frame.heading = "No backups yet"; frame.body = "Connect the FM-1, then choose Create backup from Home."; }
        frame.footer = menuHint;
        return frame;
    }
    if (localPage == LocalPage::BackupActions && backupSelected < backupChoices.size()) {
        frame.screen = ui::Screen::Menu;
        frame.title = "Backup";
        const auto& backup = backupChoices[backupSelected];
        frame.eyebrow = standard(backupLabel(backup) + " / " + readableBytes(backup.size));
        bool deletable = transportCanDeleteBackup(backup.path);
        std::vector<ui::Row> rows = {{"Restore on FM-1", "Physical hold before writing", "", false},
            {"Export information", "Keep a copy outside this device", "", false},
            {"Delete backup", deletable ? "Permanent; export first if needed" : "Unavailable / operation in progress", deletable ? "" : "LOCK", false}};
        appendWindow(frame, rows, backupAction);
        frame.footer = menuHint;
        return frame;
    }
    if (localPage == LocalPage::Storage) {
        const uint64_t total = totalSpace(), free = freeSpace();
        const unsigned used = total ? unsigned(((total - free) * 100 + total / 2) / total) : 0;
        frame.screen = ui::Screen::Menu;
        frame.title = "Storage";
        frame.eyebrow = standard(readableBytes(free) + " FREE OF " + readableBytes(total));
        frame.right = std::to_string(used) + "% USED";
        std::vector<ui::Row> rows = {
            {"Space", standard(readableBytes(free) + " free / " + readableBytes(total - free) + " used"), "", false},
            {"Downloaded firmware", standard(groupSummary(storagePackages, packageChoices.size(), "packages")), "", false},
            {"Backups", standard(groupSummary(storageBackups, storageBackups.bins, "backups")), "", false},
            {"Session logs", standard(groupSummary(storageLogs, storageLogs.files, "logs")), "", false},
            {"Erase all storage", "Backups, firmware, logs and records", "", false},
            {"Export files", "Copy files off this device", "", false}};
        appendWindow(frame, rows, storageSelected);
        frame.footer = menuHint;
        return frame;
    }
    if (localPage == LocalPage::StoragePackages) {
        frame.screen = ui::Screen::Backups;
        frame.title = "Firmware";
        frame.eyebrow = "DOWNLOADED PACKAGES / CHOOSE TO DELETE";
        frame.right = std::to_string(packageChoices.size()) + " FILES";
        std::vector<ui::Row> rows;
        for (const auto& package : packageChoices) rows.push_back({standard(package.label), standard(readableBytes(package.size)), "", false});
        appendWindow(frame, rows, packageSelected);
        if (rows.empty()) { frame.heading = "No downloaded firmware"; frame.body = "Packages saved from the Hub or uploaded in the browser appear here."; }
        frame.footer = menuHint;
        return frame;
    }
    if (localPage == LocalPage::History) {
        frame.screen = ui::Screen::Loaded;
        frame.title = "History";
        frame.eyebrow = "BOOT-CONFIRMED OPERATIONS";
        appendWindow(frame, historyChoices, historySelected);
        if (historyChoices.empty()) { frame.heading = "No confirmed installs"; frame.body = "An operation appears here only after the FM-1 reports the expected boot identity."; }
        frame.footer = menuHint;
        return frame;
    }
    if (localPage == LocalPage::Setup || (!storageReady && localPage == LocalPage::Home && mainTab != 2)) {
        frame.screen = ui::Screen::Setup;
        frame.title = "Storage";
        frame.eyebrow = "SETUP NEEDED";
        frame.warning = true;
#ifdef FM1_CARDPUTER
        frame.badge = "SD CARD NOT MOUNTED";
        frame.body = "Insert a FAT32 microSD card and restart. Storage is never formatted automatically.";
        frame.footer = "Enter Help  Fn+F Home";
#else
        frame.badge = "INTERNAL STORAGE UNAVAILABLE";
        frame.body = "Initialize explicitly to use saved packages and backups. This erases the internal filesystem.";
        frame.footer = "A Initialize A+B Home";
#endif
        return frame;
    }
    if (localPage == LocalPage::Info) {
        frame.screen = ui::Screen::Info;
        frame.title = standard(localInfoTitle);
        frame.eyebrow = "DEVICE INFORMATION";
        frame.body = standard(localInfoBody.substring(localInfoOffset));
        #ifdef FM1_CARDPUTER
        frame.footer = ";/. Scroll  Enter Back";
#else
        frame.footer = "B Scroll Hold A Back";
#endif
        return frame;
    }
    if (mainTab == 2) {
        frame.screen = ui::Screen::Loaded;
        frame.title = "Loaded";
        frame.eyebrow = transport.firmwareLive ? "IDENTIFIED ON THE CONNECTED FM-1" : "LAST OBSERVED / NOT LIVE";
        frame.badge = transport.firmwareLive ? "LIVE IDENTITY VERIFIED" : transport.firmwareKnown ? "SAVED OBSERVATION" : "NOT IDENTIFIED";
        frame.heading = transport.firmwareKnown ? standard(transport.firmwareProduct) : "Connect your FM-1";
        frame.body = transport.firmwareLive ? "Reported by the FM-1 itself. Downloaded packages are a separate collection." : transport.firmwareKnown ? "Cached identity from a previous observation. Connect to verify what is running now." : "Actions > Connect identifies the FM-1. A gallery selection never changes what is loaded.";
        #ifdef FM1_CARDPUTER
        frame.footer = "Enter History  Fn+F Home";
#else
        frame.footer = "A History A+B Home";
#endif
        return frame;
    }
    if (detail) {
        frame.screen = ui::Screen::Detail;
        frame.title = "Firmware";
        int index = selectedIndex();
        if (index >= 0) {
            auto entry = readEntry(index);
            frame.eyebrow = standard(entry.author + " / " + entry.version);
            frame.badge = entry.path.isEmpty() ? (entry.downloadable ? "NOT DOWNLOADED" : "MAKER INSTALLER") : "DOWNLOADED";
            frame.heading = standard(entry.name);
            frame.subtitle = standard(String(entry.beta ? "Beta" : "Non-beta") + " / chip " + gallery::status(gallery::compatibility(entry.chip.c_str(), transportFlashId())));
            detailOffset = std::min<size_t>(detailOffset, entry.description.length());
            frame.body = standard(entry.description.substring(detailOffset, detailOffset + 180));
        }
        #ifdef FM1_CARDPUTER
        frame.footer = ";/. Scroll / Options Esc/, Back";
#else
        frame.footer = "A Options  B Scroll Hold A Back";
#endif
        return frame;
    }
    frame.screen = ui::Screen::Browse;
    frame.title = mainTab == 1 ? "Downloaded" : "Discover";
    size_t count = matchCount();
    frame.right = count ? std::to_string(selected + 1) + "/" + std::to_string(count) : "0";
    String sortLabel = sortOrder == "popular" ? "POPULAR" : sortOrder == "newest" ? "NEWEST" : "NAME";
    frame.eyebrow = standard((query.isEmpty() ? sortLabel : "SEARCH: " + query) + (favoritesOnly ? " / FAVORITES" : "") + (WiFi.status() == WL_CONNECTED ? " / Wi-Fi" : ""));
    if (searching) { frame.heading = "Finding firmware..."; frame.body = mainTab == 1 ? "Searching downloaded packages." : "Search includes the full cached catalogue."; }
    else if (!count) {
        frame.heading = mainTab == 1 ? "Nothing downloaded yet" : "No matching firmware";
        frame.body = mainTab == 1 ? "Choose firmware in Discover, then Download package. It will stay available offline." : "Clear search or filters. For a fresh catalogue, connect Wi-Fi and choose Refresh catalogue.";
    } else {
        size_t begin = selected > 1 ? selected - 1 : 0;
        if (begin + 3 > count) begin = count > 3 ? count - 3 : 0;
        for (size_t position = begin; position < count && frame.rows.size() < 3; ++position) {
            auto entry = readEntry(matched[position]);
            String badge = entry.favorite ? "*" : !entry.path.isEmpty() ? "SAVED" : entry.beta ? "BETA" : "";
            frame.rows.push_back({standard(entry.name), standard(entry.author + " / " + entry.version), standard(badge), position == selected});
        }
    }
    if (noticeUntil && int32_t(noticeUntil - millis()) > 0) frame.footer = standard(notice);
    return frame;
}

void draw() {
    if (!screenReady) return;
    ui::Frame frame = buildLocalFrame(transportStatus());
    ui::renderIfChanged(frame, screenCanvas, repaintGate, millis());
    dirty = repaintGate.deferred();
    lastPaint = millis();
}

void paintLocalTick() {
    static uint32_t checkedAt = 0;
    static String previousStatus;
    static int previousWifi = -100;
    static bool wasBusy = false, wasImport = false;
    uint32_t now = millis();
    if (!dirty && uint32_t(now - checkedAt) < 50) return;
    checkedAt = now;
    auto transport = transportStatus();
    String signature = transport.stage + "|" + transport.detail + "|" + String(transport.completed) + "|" + String(transport.total) + "|" + String(transport.busy) + "|" + transport.firmwareProduct + "|" + String(transport.firmwareLive) + "|" + String(int(transport.mode));
    String importStage, importDetail;
    xSemaphoreTake(jobMutex, portMAX_DELAY);
    importStage = jobStage; importDetail = jobDetail;
    signature += "|" + jobStage + "|" + jobDetail + "|" + String(jobCompleted) + "|" + String(jobTotal) + "|" + String(jobBusy.load());
    xSemaphoreGive(jobMutex);
    if (signature != previousStatus) { previousStatus = signature; dirty = true; }
    bool busy = transport.busy || jobBusy.load();
    if (wasBusy && !busy) {
        if (wasImport && (importStage == "Import failed" || importStage == "Import cancelled")) showLocalInfo(importStage, importDetail);
        else if (!wasImport && (transport.stage == "Error" || transport.stage == "Cancelled")) showLocalInfo(transport.stage == "Cancelled" ? "Cancelled" : "Could not finish", transport.detail);
        else if (wasImport) notice = importStage == "Catalogue unchanged" ? "Catalogue already current" : job.catalogue ? "Catalogue updated" : "Package saved and checked";
        else notice = transport.stage;
    }
    if (busy) wasImport = jobBusy.load();
    wasBusy = busy;
    // An FM-1 that goes away between operations (cable pulled, switched off)
    // is otherwise only visible in the transport state; say so on screen.
    static bool wasConnected = false;
    if (wasConnected && !transport.connected && !busy) { notice = "FM-1 disconnected"; dirty = true; }
    wasConnected = transport.connected;
    int wifi = WiFi.status();
    if (wifi != previousWifi) {
        previousWifi = wifi;
        dirty = true;
    }
    if (notice != lastNotice) {
        lastNotice = notice;
        if (notice != "Hardware validation pending") noticeUntil = now + 5000;
        dirty = true;
    }
    if (noticeUntil && int32_t(now - noticeUntil) >= 0) { noticeUntil = 0; dirty = true; }
    if (dirty) draw();
}

void beginLocalDisplay() {
    bool indexed = true;
#ifdef FM1_CARDPUTER
    screenBuffer.setPsram(false);
    screenBuffer.setColorDepth(4);
#else
    indexed = !psramFound();
    screenBuffer.setPsram(!indexed);
    screenBuffer.setColorDepth(indexed ? 4 : 16);
#endif
    if (!screenBuffer.createSprite(DISPLAY.width(), DISPLAY.height())) {
        screenBuffer.setPsram(false);
        screenBuffer.setColorDepth(4);
        indexed = true;
        if (!screenBuffer.createSprite(DISPLAY.width(), DISPLAY.height())) {
            DISPLAY.fillScreen(TFT_BLACK);
            DISPLAY.setTextColor(TFT_WHITE);
            DISPLAY.setTextSize(1);
            DISPLAY.setCursor(12, 30);
            DISPLAY.println("Display buffer unavailable.\nRestart the device.");
            return;
        }
    }
    screenCanvas.setIndexed(indexed);
    if (indexed) {
        screenBuffer.createPalette();
        for (uint8_t i = 0; i < 16; ++i) screenBuffer.setPaletteColor(i, ui::rgb(static_cast<ui::Color>(i)));
    }
    screenBuffer.setTextWrap(false);
    screenReady = true;
    ui::Frame boot;
    boot.title = "FM-1 Flip";
    boot.eyebrow = "HOME";
    boot.heading = "Opening your library";
    boot.body = "Loading your firmware library.";
    boot.footer = "One moment...";
    ui::render(boot, screenCanvas);
}
