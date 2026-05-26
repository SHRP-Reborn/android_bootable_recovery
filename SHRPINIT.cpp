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

#include <fstream> 
#include "gui/gui.hpp"
#include "data.hpp"
#include "partitions.hpp"
#include <list>
#include "twcommon.h"
#include "cutils/properties.h"

#include "SHRPINIT.hpp"

void SHRP::INIT(){
    printRecDetails();
    genarateDate();
    handleLock();
}

void SHRP::printRecDetails(){
	gui_msg(Msg("|SKYHAWK RECOVERY PROJECT REBORN",0));

	string tmp;
	DataManager::GetValue("shrp_ver",tmp);
	tmp="|Version - "+tmp;
	gui_msg(Msg(tmp.c_str(),0));
	
	tmp = (DataManager::GetStrValue("is_Official") == "true") ? "|Status - Official" : "|Status - Unofficial";
	gui_msg(Msg(tmp.c_str(),0));
	
	DataManager::GetValue("device_code_name",tmp);
	tmp="|Device - "+tmp;
	gui_msg(Msg(tmp.c_str(),0));

#ifdef SHRP_BUILD_DATE
	tmp="|Build - "+DataManager::GetStrValue("buildNo");
	gui_msg(Msg(tmp.c_str(),0));
#endif
}

void SHRP::genarateDate() {
    time_t seconds = time(nullptr);
    struct tm *t = localtime(&seconds);
    if (!t) return;

    static const char* months[] = {
        "Jan","Feb","Mar","Apr","May","Jun",
        "Jul","Aug","Sep","Oct","Nov","Dec"
    };
    static const char* wdays[] = {
        "Sun, ","Mon, ","Tue, ","Wed, ","Thu, ","Fri, ","Sat, "
    };

    int m = t->tm_mon;
    string result = wdays[t->tm_wday] + to_string(t->tm_mday) + " " + months[m];
    DataManager::SetValue("c_lock_screen_date", result);
}

void SHRP::handleLock() {
    std::ifstream f("/sdcard/SHRP/data/slts");
    if (!f) f.open("/twres/slts");
    char lockType = 0;
    if (f && f >> lockType) {
        f.close();
    } else {
        lockType = 69; // uhh i need to find out why it's locked when there's no such file
    }

    const char* dest;
    int lockStatus;
    const char* shrpLockVal;
    if (lockType == '1') {
		PartitionManager.Disable_MTP();
        dest = "c_pass_capture";
        lockStatus = 1;
        shrpLockVal = "1";
    } else if (lockType == '2') {
		PartitionManager.Disable_MTP();
        dest = "c_patt_capture";
        lockStatus = 2;
        shrpLockVal = "1";
    } else if (lockType == 69) {
		PartitionManager.Disable_MTP();
        dest = "c_recBlocked";
        lockStatus = 69;
        shrpLockVal = "1";
    } else {
        dest = "main2";
        lockStatus = 0;
        shrpLockVal = "0";
    }

    DataManager::SetValue("c_target_destination", dest);
    DataManager::SetValue("recLockStatus", lockStatus);
    property_set("shrp.lock", shrpLockVal);
}