/*
Copyright 2019 - 2020 SKYHAWK RECOVERY PROJECT

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
#include <string>
#include <fstream>
#include "twcommon.h"
#include "twrp-functions.hpp"
#include "data.hpp"
#include "gui/gui.hpp"

#include "SHRPFILETOOLS.hpp"

#include "SHRPTHEME.hpp"

void ThemeManager::initialVarProcess(){
    DataManager::SetValue("extern_primaryColor", DataManager::GetStrValue("primaryColor"));
    DataManager::SetValue("extern_secondaryColor", DataManager::GetStrValue("secondaryColor"));
    DataManager::SetValue("extern_accentColor", DataManager::GetStrValue("accentColor"));
    DataManager::SetValue("extern_backgroundColor", DataManager::GetStrValue("backgroundColor"));
    DataManager::SetValue("extern_navbarColor", DataManager::GetStrValue("navbarColor"));
    DataManager::SetValue("extern_dashboardTextColor", DataManager::GetStrValue("dashboardTextColor"));
}

int ThemeManager::hexToInt(string str){
	int ret;
	stringstream ss;
	ss << std::hex << str;
	ss >> ret;
	return ret;
}

bool ThemeManager::isColorDark(string str){
	int r,g,b;
	int count = 0;
	if(str.length() >= 7){
		r = hexToInt(str.substr(1, 2));
		count += r > 175 ? 1 : 0;
		
		g = hexToInt(str.substr(3, 2));
		count += g > 175 ? 1 : 0;
		
		b = hexToInt(str.substr(5, 2));
		count += b > 175 ? 1 : 0;	
	}
	
	return count > 1 ? false : true;
}

string ThemeManager::getColor(string str){
    string tmp;
    for(auto it = colors.begin(); it < colors.end(); it++){
        if(DataManager::GetStrValue(str) == it->colorValue){
            tmp = it->colorName;
            break;
        }
        tmp = "NULL";
    }
    tmp = ((str == "navbarColor" || str == "dashboardTextColor" || str == "accentColor") && tmp == "black") ? "dark" : tmp;
    return tmp == "NULL" ? tmp : str == "navbarColor" ? "c_"+ tmp : tmp;
}

string ThemeManager::get_subBackgroundColor(){
    string bgColor = DataManager::GetStrValue("backgroundColor");
    if(bgColor == DataManager::GetStrValue("clrBlack")){
        return "#0d0d0d";
    }else if(bgColor == DataManager::GetStrValue("clrDark")){
        return "#272727";
    }else if(bgColor == DataManager::GetStrValue("clrDeepBlue")){
        return "#1d1f3b";
    }else if(bgColor == DataManager::GetStrValue("clrDarkBlue")){
        return "#272c2f";
    }else if(bgColor == DataManager::GetStrValue("clrDarkGreen")){
        return "#222727";
    }else if(bgColor == DataManager::GetStrValue("clrDarkPurple")){
        return "#242026";
    }else if(bgColor == DataManager::GetStrValue("clrDarkViolate")){
        return "#312d44";
    }else if(bgColor == DataManager::GetStrValue("clrWhite")){
        return "#fafafa";
    }else{
        return DataManager::GetStrValue("subBackgroundColor");
    }
}


bool ThemeManager::syncDyanmicVar(){
    bool ret = TWFunc::Exec_Cmd(
        "export primaryColor="+DataManager::GetStrValue("primaryColor")+";"+ \
        "export secondaryColor="+DataManager::GetStrValue("secondaryColor")+";"+ \
        "export accentColor="+DataManager::GetStrValue("accentColor")+";"+ \
        "export backgroundColor="+DataManager::GetStrValue("backgroundColor")+";"+ \
        "export subBackgroundColor="+get_subBackgroundColor()+";"+ \
        "export navbarColor="+DataManager::GetStrValue("navbarColor")+";"+ \
        "export dashboardTextColor="+DataManager::GetStrValue("dashboardTextColor")+";"+ \
        "export batteryBarEnabled="+DataManager::GetStrValue("batteryBarEnabled")+";"+ \
        "export statusBarEnabled="+DataManager::GetStrValue("statusBarEnabled")+";"+ \
        "export batteryIconEnabled="+DataManager::GetStrValue("batteryIconEnabled")+";"+ \
        "export batteryPercentageEnabled="+DataManager::GetStrValue("batteryPercentageEnabled")+";"+ \
        "export clockEnabled="+DataManager::GetStrValue("clockEnabled")+";"+ \
        "export centeredClockEnabled="+DataManager::GetStrValue("centeredClockEnabled")+";"+ \
        "export cpuTempEnabled="+DataManager::GetStrValue("cpuTempEnabled")+";"+ \
        "export roundedCornerEnabled="+DataManager::GetStrValue("roundedCornerEnabled")+";"+ \
        "export navbarBackgroundEnabled="+DataManager::GetStrValue("navbarBackgroundEnabled")+";"+ \
        "export dashboardSubTintEnabled="+DataManager::GetStrValue("dashboardSubTintEnabled")+";"+ \
        "export navbarType="+DataManager::GetStrValue("navbarType")+";"+ \
        "export batteryType="+DataManager::GetStrValue("batteryType")+";"+ \
        "export dashboardIconType="+DataManager::GetStrValue("dashboardIconType")+";"+ \
        "export roundedcornerType="+DataManager::GetStrValue("roundedcornerType")+";"+ \
        "cd /twres/scripts/;"+ \
        "sh syncDynamicVar.sh;") == 0 ? true : false;
    return ret;
}

bool ThemeManager::applyThemeResouces(){
    bool ret = true;
    string themeBase = "/twres/themeResources";
    string box = "/twres/images";


    if(DataManager::GetStrValue("extern_accentColor") != DataManager::GetStrValue("accentColor") && getColor("accentColor") != "NULL"){
        if(TWFunc::Exec_Cmd("cp -r "+themeBase+"/accentResources/"+getColor("accentColor")+"/*.png "+box+";") != 0){ret=false;}
    }
    if(DataManager::GetStrValue("extern_backgroundColor") != DataManager::GetStrValue("backgroundColor") && getColor("backgroundColor") != "NULL" && ret){
        if(TWFunc::Exec_Cmd("cp -r "+themeBase+"/backgroundResources/"+getColor("backgroundColor")+"/*.png "+box+";") != 0){ret=false;}
    }

    string bType,bClr;
    bType = DataManager::GetIntValue("batteryType") == 1 ? "/default" : "/circle";
    bClr = !isColorDark(DataManager::GetStrValue("backgroundColor")) ? "/light" : "/dark";
    if(TWFunc::Exec_Cmd("cp -r "+themeBase+"/batteryResources"+bType+bClr+"/*.png "+box+";") != 0){ret=false;}


    if(ret){
        string tmp;
        switch(DataManager::GetIntValue("navbarType")){
            case 1:
                tmp = "c_default";
                break;
            case 2:
                tmp = "c_nxtbit";
                break;
            case 3:
                tmp = "c_samsung";
                break;
            case 4:
                tmp = "c_custom";
                break;
        }
        if(getColor("navbarColor") != "NULL")
            if(TWFunc::Exec_Cmd("cp -r "+themeBase+"/navigationResources/"+tmp+"/"+getColor("navbarColor")+"/*.png "+box+";") != 0){ret=false;}
    }



    if(DataManager::GetStrValue("extern_dashboardIconType") != DataManager::GetStrValue("dashboardIconType") && ret){
        string tmp;
        switch(DataManager::GetIntValue("dashboardIconType")){
            case 1:
                tmp = "default";
                break;
            case 2:
                tmp = "stock";
                break;
            case 3:
                tmp = "aex";
                break;
            case 4:
                tmp = "black";
                break;
            case 5:
                tmp = "white";
                break;
            default:
                tmp = "NULL";
        }
        if(tmp != "NULL"){
            if(TWFunc::Exec_Cmd("cp -r "+themeBase+"/dashboardResources/"+tmp+"/*.png "+box+";") != 0){ret=false;}
        }
    }

    if(DataManager::GetStrValue("extern_dashboardTextColor") != DataManager::GetStrValue("dashboardTextColor") && getColor("dashboardTextColor") != "NULL" && ret){
        if(TWFunc::Exec_Cmd("cp -r "+themeBase+"/dashboardResources/dashboardBackground/"+getColor("dashboardTextColor")+"/*.png "+box+";") != 0){ret=false;}
    }

    if(DataManager::GetStrValue("extern_roundedcornerType") != DataManager::GetStrValue("roundedcornerType") && DataManager::GetIntValue("roundedCornerEnabled") == 1 && ret){
        string tmp;
        switch(DataManager::GetIntValue("roundedcornerType")){
            case 1:
                tmp = "type_1";
                break;
            case 2:
                tmp = "type_2";
                break;
            case 3:
                tmp = "type_3";
                break;
        }
        if(TWFunc::Exec_Cmd("cp -r "+themeBase+"/roundedCornerResources/"+tmp+"/*.png "+box+";") != 0){ret=false;}
    }

    //Dynamic Resources
    bool darkColor = isColorDark(DataManager::GetStrValue("backgroundColor"));
    if(TWFunc::Exec_Cmd("cp -r "+themeBase+"/dynamicResources/"+(darkColor ? "light" : "dark")+"/*.png "+box+";") != 0){ret=false;}

    return ret;
}
