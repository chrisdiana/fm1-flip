LocalPage localPage=LocalPage::Home,editorReturnPage=LocalPage::Home,infoReturnPage=LocalPage::Home;
LocalConfirmation localConfirmation=LocalConfirmation::None;
bool savedOnly=false,detail=false,dirty=false,confirmInstall=false,wifiConnecting=false,homeMenu=true;
bool editorReturnMenu=false,infoReturnMenu=false,infoReturnDetail=false,infoReturnHome=true;
bool localReview=false,localHeldFired=false,localReleased=false;
String localConfirmationPath;
uint32_t localHeldSince=0;
size_t homeIndex=0,selected=0;
uint8_t mainTab=0;
struct {bool armed=false;void cancel(){armed=false;}}pendingAction;
struct {void disconnect(bool){}}WiFi;
int selectedIndex(){return 0;}
struct Entry{String path="saved.fwsc";};
Entry readEntry(int){return {};}

bool networkPrompt=false,networkWaiting=false;struct Job{};Job pendingNetworkJob;
