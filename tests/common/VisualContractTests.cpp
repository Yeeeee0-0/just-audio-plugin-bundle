#include "common/ui/VisualAssets.hpp"
#include <cstdlib>
#include <iostream>
static void require(bool ok){if(!ok){std::cerr<<"FAIL contract assertion\n";std::abort();}}
int main(){using namespace just;EditorViewState v;
require(!v.backgroundEnabled && BackgroundSchedule::framesPerSecond(v,true)==0);
v.backgroundEnabled=true;require(BackgroundSchedule::framesPerSecond(v,false)==0 && BackgroundSchedule::framesPerSecond(v,true)==0);
v.backgroundFps=200;require(BackgroundSchedule::framesPerSecond(v,true)==0);v.lowPerformance=true;require(BackgroundSchedule::framesPerSecond(v,true)==0);v.lowPerformance=false;v.reduceMotion=true;require(BackgroundSchedule::framesPerSecond(v,true)==0);
for(const char* p:{"/tmp/test.png","../bad.png","icons/../../bad.png","https://host/icon.png","C:\\a.png",""})require(!safeAssetRelativePath(p));require(safeAssetRelativePath("icons/reverb.png"));
require(std::string(displayProductName("fake_stereo","JUST Fake Stereo"))=="JUST Wider");require(std::string(uiAuthor)=="Yee Huang");require(std::string(uiVersion)=="0.1.0");require(std::string(uiContact)=="yeehuang2002@163.com");
std::cout<<"PASS disabled artwork regardless of legacy preference values, local stable assets, display-only Wider name and exact author\n";
}
