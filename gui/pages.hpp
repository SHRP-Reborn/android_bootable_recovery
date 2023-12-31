/*
	Copyright 2017 TeamWin
	This file is part of TWRP/TeamWin Recovery Project.

	TWRP is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	TWRP is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with TWRP.  If not, see <http://www.gnu.org/licenses/>.
*/

// pages.hpp - Base classes for page manager of GUI

#ifndef _PAGES_HEADER_HPP
#define _PAGES_HEADER_HPP

#include <vector>
#include <map>
#include <string>
#include "ziparchive/zip_archive.h"
#include "rapidxml.hpp"
#include "gui.hpp"
using namespace rapidxml;

enum TOUCH_STATE {
	TOUCH_START = 0,
	TOUCH_DRAG = 1,
	TOUCH_RELEASE = 2,
	TOUCH_HOLD = 3,
	TOUCH_REPEAT = 4
};

struct COLOR {
	unsigned char red;
	unsigned char green;
	unsigned char blue;
	unsigned char alpha;
	COLOR() : red(0), green(0), blue(0), alpha(0) {}
	COLOR(unsigned char r, unsigned char g, unsigned char b, unsigned char a = 255)
		: red(r), green(g), blue(b), alpha(a) {}
};

struct language_struct {
	std::string filename;
	std::string displayvalue;
};

inline bool operator < (const language_struct& language1, const language_struct& language2)
{
	return language1.displayvalue < language2.displayvalue;
}

extern std::vector<language_struct> Language_List;

// Utility Functions
int ConvertStrToColor(std::string str, COLOR* color);
int gui_forceRender(void);
int gui_changePage(std::string newPage);
int gui_changeOverlay(std::string newPage);

class Resource;
class ResourceManager;
class RenderObject;
class ActionObject;
class InputObject;
class MouseCursor;
class GUIObject;
class HardwareKeyboard;

class Page
{
public:
	Page(xml_node<>* page, std::vector<xml_node<>*> *templates);
	virtual ~Page();

	std::string GetName(void)   { return mName; }

public:
	virtual int Render(void);
	virtual int Update(void);
	virtual int NotifyTouch(TOUCH_STATE state, int x, int y);
	virtual int NotifyKey(int key, bool down);
	virtual int NotifyCharInput(int ch);
	virtual int SetKeyBoardFocus(int inFocus);
	virtual int NotifyVarChange(std::string varName, std::string value);
	virtual void SetPageFocus(int inFocus);

protected:
	std::string mName;
	std::vector<GUIObject*> mObjects;
	std::vector<RenderObject*> mRenders;
	std::vector<ActionObject*> mActions;
	std::vector<InputObject*> mInputs;

	ActionObject* mTouchStart;
	COLOR mBackground;

protected:
	bool ProcessNode(xml_node<>* page, std::vector<xml_node<>*> *templates, int depth);
};

struct LoadingContext;

class PageSet
{
public:
	PageSet();
	virtual ~PageSet();

public:
	int Load(LoadingContext& ctx, const std::string& filename);
	int LoadLanguage(char* languageFile, ZipArchiveHandle package);
	void MakeEmergencyConsoleIfNeeded();

	Page* FindPage(std::string name);
	int SetPage(std::string page);
	int SetOverlay(Page* page);
	const ResourceManager* GetResources();

	// Helper routine for identifing if we're the current page
	int IsCurrentPage(Page* page);
	std::string GetCurrentPage() const;

	// These are routing routines
	int Render(void);
	int Update(void);
	int NotifyTouch(TOUCH_STATE state, int x, int y);
	int NotifyKey(int key, bool down);
	int NotifyCharInput(int ch);
	int SetKeyBoardFocus(int inFocus);
	int NotifyVarChange(std::string varName, std::string value);

	void AddStringResource(std::string resource_source, std::string resource_name, std::string value);

protected:
	int LoadDetails(LoadingContext& ctx, xml_node<>* root);
	int LoadPages(LoadingContext& ctx, xml_node<>* pages);
	int LoadVariables(xml_node<>* vars);

protected:
	ResourceManager* mResources;
	std::vector<Page*> mPages;
	Page* mCurrentPage;
	std::vector<Page*> mOverlays; // Special case for popup dialogs and the lock screen
};

class PageManager
{
public:
	// Used by GUI
	static char* LoadFileToBuffer(std::string filename, ZipArchiveHandle package);
	static void LoadLanguageList(ZipArchiveHandle package);
	static void LoadLanguage(std::string filename);
	static int LoadPackage(std::string name, std::string package, std::string startpage);
	static PageSet* SelectPackage(std::string name);
	static int ReloadPackage(std::string name, std::string package);
	static void ReleasePackage(std::string name);
	static int RunReload();
	static void RequestReload();
	static void RequestCustomReload(std::string startPage = "main2");
	static void SetStartPage(const std::string& page_name);

	// Used for actions and pages
	static int ChangePage(std::string name);
	static int ChangeOverlay(std::string name);
	static const ResourceManager* GetResources();
	static std::string GetCurrentPage();

	// Helper to identify if a particular page is the active page
	static int IsCurrentPage(Page* page);

	// These are routing routines
	static int Render(void);
	static int Update(void);
	static int NotifyTouch(TOUCH_STATE state, int x, int y);
	static int NotifyKey(int key, bool down);
	static int NotifyCharInput(int ch);
	static int SetKeyBoardFocus(int inFocus);
	static int NotifyVarChange(std::string varName, std::string value);

	static MouseCursor *GetMouseCursor();
	static void LoadCursorData(xml_node<>* node);

	static HardwareKeyboard *GetHardwareKeyboard();

	static xml_node<>* FindStyle(std::string name);
	static void AddStringResource(std::string resource_source, std::string resource_name, std::string value);

protected:
	static PageSet* FindPackage(std::string name);
	static void LoadLanguageListDir(std::string dir);
	static void Translate_Partition(const char* path, const char* resource_name, const char* default_value);
	static void Translate_Partition(const char* path, const char* resource_name, const char* default_value, const char* storage_resource_name, const char* storage_default_value);
	static void Translate_Partition_Display_Names();

protected:
	static std::map<std::string, PageSet*> mPageSets;
	static PageSet* mCurrentSet;
	static MouseCursor *mMouseCursor;
	static HardwareKeyboard *mHardwareKeyboard;
	static bool mReloadTheme;
	static std::string mStartPage;
	static std::string mCustomStartPage;
	static bool mUseCustomStartPage;
	static LoadingContext* currentLoadingContext;
};

#endif  // _PAGES_HEADER_HPP
