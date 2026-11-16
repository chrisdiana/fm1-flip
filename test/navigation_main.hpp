int main(){
 assert(actionMenu&&homeMenu); // The startup view is Home.
 homeIndex=1;setMainTab(1);selected=7;
 assert(savedOnly&&!actionMenu);
 detail=true;detailOffset=60;
 backLocal();assert(!detail&&!actionMenu&&mainTab==1&&selected==7);
 backLocal();assert(actionMenu&&homeMenu&&actionIndex==1&&selected==7);
 backLocal();assert(actionMenu&&homeMenu); // Back at root is a no-op.
 setMainTab(1);selected=7;detail=true;
 openActionMenu(true);assert(actionMenu&&!homeMenu&&actionIndex==6);
 backLocal();assert(!actionMenu&&detail&&selected==7);
 backLocal();assert(!actionMenu&&!detail&&selected==7);
 showHome();assert(actionMenu&&homeMenu&&!detail);
 // A+B jumps directly Home from an editor or a confirmation, clearing armed work.
 localPage=LocalPage::Wifi;searchPicker=true;showHome();assert(!searchPicker&&localPage==LocalPage::Home&&actionMenu);
 localConfirmation=LocalConfirmation::DeleteBackup;localReview=true;confirmInstall=true;pendingAction.armed=true;
 showHome();assert(!localReview&&!confirmInstall&&!pendingAction.armed&&localConfirmation==LocalConfirmation::None);
 localPage=LocalPage::BackupActions;actionMenu=false;backLocal();assert(localPage==LocalPage::Backups&&!actionMenu);
 backLocal();assert(localPage==LocalPage::Home&&actionMenu);
 setMainTab(2);localPage=LocalPage::History;backLocal();assert(mainTab==2&&!actionMenu&&localPage==LocalPage::Home);
 backLocal();assert(actionMenu&&homeMenu);
 searchPicker=true;actionMenu=false;editorReturnMenu=true;editorReturnPage=LocalPage::Home;
 backLocal();assert(!searchPicker&&actionMenu);
 actionMenu=false;localPage=LocalPage::Info;infoReturnPage=LocalPage::BackupActions;infoReturnMenu=false;infoReturnDetail=false;
 backLocal();assert(localPage==LocalPage::BackupActions&&!actionMenu);
 std::cout<<"Production navigation passed: startup Home, details -> list -> Home, contextual options, editor, confirmations, backups and history\n";
}
