/*
Copyright 2019 - 2020 SKYHAWK RECOVERY PROJECT
Copyright 2020 - 2026 SkyHawk Recovery Project Reborn

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
#include <unistd.h>
#include <sys/stat.h>
#include <string>
#include <vector>
#include "twcommon.h"
#include "twrp-functions.hpp"
#include "gui/gui.hpp"

#include "SHRPTOOLS.hpp"

#include "filesystem.hpp"
namespace fs = ghc::filesystem;

//SHRP minUtils
bool minUtils::compare(string str1,string str2){
    transform(str1.begin(),str1.end(),str1.begin(), ::tolower);
    transform(str2.begin(),str2.end(),str2.begin(), ::tolower);
    return (str1==str2) ? true : false;
}

bool minUtils::isFileEditable(string fileExtension){
	static const std::vector<std::string> extensions = {
        ".txt", ".xml", ".prop", ".sh", ".conf", ".json", ".cfg", ".rc", ".d", ".md"
    };
    if (fileExtension.empty() || fileExtension == "none") return true;
    for (const auto& ext : extensions) {
        if (compare(fileExtension, ext)) return true;
    }
    return false;
}

bool minUtils::find(std::string str,std::string sub){
	return str.find(sub) != std::string::npos;
}
bool minUtils::find(std::string str,std::string sub,int dummy){
	auto it = std::search(str.begin(), str.end(), sub.begin(), sub.end(),
        [](char a, char b) { return ::tolower((unsigned char)a) == ::tolower((unsigned char)b); });
    return it != str.end();
}

void minUtils::remountSystem(bool display) {
    std::string root = PartitionManager.Get_Android_Root_Path();
    if (PartitionManager.Is_Mounted_By_Path(root)) {
        PartitionManager.UnMount_By_Path(root, false);
    }
    struct stat st;
    if (lstat("/system", &st) == 0 && S_ISLNK(st.st_mode)) {
        unlink("/system");
    }
    if (access("/system", F_OK) != 0) {
        mkdir("/system", 0755);
    }
    TWFunc::Exec_Cmd("mount -w " + root, display);
    if (display) {
        gui_msg("remount_system_rw=[i] Remounted system as R/W!");
    }
}

string minUtils::getExtension(string str,string arg){
	return fs::path(str).extension().string();
}