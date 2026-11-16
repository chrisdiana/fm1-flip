#!/usr/bin/env python3
"""Count metadata reads in the production handheld search scheduler."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'src/device/app.cpp').read_text()
function=source[source.index('void searchTick(){'):source.index('int selectedIndex(){')]
function=function.replace('.isEmpty()', '.empty()')
adapter=r'''
#include <string>
#include <iostream>
#include <algorithm>
using String=std::string;
constexpr size_t MAX_ENTRIES=512;
struct Ref{String localPath;};Ref entryIndex[MAX_ENTRIES];
size_t entryCount=217,matchScan=0,matchedCount=0,selected=0,reads=0;
size_t matched[MAX_ENTRIES]{};
bool searching=true,dirty=false,savedOnly=true,favoritesOnly=false;
String query,categoryFilter="all",releaseFilter="all",compatibilityFilter="all",sortOrder="popular";
void ensureMatches(){}
int readEntry(size_t i){++reads;return int(i);}
bool matches(int i){return !savedOnly || !entryIndex[i].localPath.empty();}
void sortIndices(size_t*,size_t,const String&){}
'''
main=r'''
int main(){
 entryIndex[3].localPath="one.fwsc";entryIndex[200].localPath="old-release.fwsc";
 int ticks=0;while(searching&&ticks<100){searchTick();++ticks;}
 std::cout<<"Downloaded: "<<reads<<" metadata reads, "<<ticks<<" search ticks, "<<matchedCount<<" saved entries\n";
 if(reads!=0||ticks!=1||matchedCount!=2||matched[0]!=3||matched[1]!=200)return 1;
 query="test";searching=true;matchScan=matchedCount=reads=0;
 while(searching)searchTick();
 if(reads!=2||matchedCount!=2)return 2;
 // Inherited filters must still run against saved metadata only.
 query="";categoryFilter="synth";searching=true;matchScan=matchedCount=reads=0;
 while(searching)searchTick();
 if(reads!=2)return 3;
 savedOnly=false;categoryFilter="all";searching=true;matchScan=matchedCount=reads=0;
 searchTick();if(searching||reads||matchedCount!=217)return 4;
 // A reload that removes every saved path immediately yields an empty list.
 for(auto& ref:entryIndex)ref.localPath="";
 savedOnly=true;searching=true;matchScan=matchedCount=reads=0;
 searchTick();if(searching||reads||matchedCount)return 5;
 std::cout<<"Production search fast paths, filtered saved-only reads, and empty library passed\n";
}
'''
with tempfile.TemporaryDirectory(prefix='fm1-local-search-') as name:
 path=Path(name);(path/'main.cpp').write_text(adapter+function+main)
 subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror',str(path/'main.cpp'),'-o',str(path/'test')],check=True)
 subprocess.run([str(path/'test')],check=True)
