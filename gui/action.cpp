/*
	Copyright 2013 bigbiff/Dees_Troy TeamWin
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

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <dirent.h>
#include <private/android_filesystem_config.h>
#include <android-base/properties.h>

#include <string>
#include <sstream>
#include "../partitions.hpp"
#include "../twrp-functions.hpp"
#include "../twrpRepacker.hpp"
#include "../openrecoveryscript.hpp"

#include "twinstall/adb_install.h"

#include "fuse_sideload.h"
#include "blanktimer.hpp"
#include "twinstall.h"

extern "C" {
#include "../twcommon.h"
#include "../variables.h"
#include "cutils/properties.h"
#include "twinstall/adb_install.h"
};
#include "set_metadata.h"
#include "minuitwrp/minui.h"

#include "rapidxml.hpp"
#include "objects.hpp"
#include "tw_atomic.hpp"
#include "../SHRPMAIN.hpp"
#include "../SHRPINIT.hpp"
#include "../SHRPTOOLS.hpp"
#include "../SHRPTHEME.hpp"
#include "../SHRPFILETOOLS.hpp"

GUIAction::mapFunc GUIAction::mf;
std::set<string> GUIAction::setActionsRunningInCallerThread;
static string zip_queue[10];
static int zip_queue_index;
pid_t sideload_child_pid;
extern std::vector<users_struct> Users_List;
extern GUITerminal* term;

static void *ActionThread_work_wrapper(void *data);

class ActionThread
{
public:
	ActionThread();
	~ActionThread();

	void threadActions(GUIAction *act);
	void run(void *data);
private:
	friend void *ActionThread_work_wrapper(void*);
	struct ThreadData
	{
		ActionThread *this_;
		GUIAction *act;
		ThreadData(ActionThread *this_, GUIAction *act) : this_(this_), act(act) {}
	};

	pthread_t m_thread;
	bool m_thread_running;
	pthread_mutex_t m_act_lock;
};

static ActionThread action_thread;	// for all kinds of longer running actions
static ActionThread cancel_thread;	// for longer running "cancel" actions

static void *ActionThread_work_wrapper(void *data)
{
	static_cast<ActionThread::ThreadData*>(data)->this_->run(data);
	return NULL;
}

ActionThread::ActionThread()
{
	m_thread_running = false;
	pthread_mutex_init(&m_act_lock, NULL);
}

ActionThread::~ActionThread()
{
	pthread_mutex_lock(&m_act_lock);
	if (m_thread_running) {
		pthread_mutex_unlock(&m_act_lock);
		pthread_join(m_thread, NULL);
	} else {
		pthread_mutex_unlock(&m_act_lock);
	}
	pthread_mutex_destroy(&m_act_lock);
}

void ActionThread::threadActions(GUIAction *act)
{
	pthread_mutex_lock(&m_act_lock);
	if (m_thread_running) {
		pthread_mutex_unlock(&m_act_lock);
		LOGERR("Another threaded action is already running -- not running %u actions starting with '%s'\n",
				act->mActions.size(), act->mActions[0].mFunction.c_str());
	} else {
		m_thread_running = true;
		pthread_mutex_unlock(&m_act_lock);
		ThreadData *d = new ThreadData(this, act);
		pthread_create(&m_thread, NULL, &ActionThread_work_wrapper, d);
	}
}

void ActionThread::run(void *data)
{
	ThreadData *d = (ThreadData*)data;
	GUIAction* act = d->act;

	std::vector<GUIAction::Action>::iterator it;
	for (it = act->mActions.begin(); it != act->mActions.end(); ++it)
		act->doAction(*it);

	pthread_mutex_lock(&m_act_lock);
	m_thread_running = false;
	pthread_mutex_unlock(&m_act_lock);
	delete d;
}

GUIAction::GUIAction(xml_node<>* node)
	: GUIObject(node)
{
	xml_node<>* child;
	xml_node<>* actions;
	xml_attribute<>* attr;

	if (!node)  return;

	if (mf.empty()) {
#define ADD_ACTION(n) mf[#n] = &GUIAction::n
#define ADD_ACTION_EX(name, func) mf[name] = &GUIAction::func
		// These actions will be run in the caller's thread
		ADD_ACTION(reboot);
		ADD_ACTION(home);
		ADD_ACTION(key);
		ADD_ACTION(page);
		ADD_ACTION(reload);
		ADD_ACTION(customReload);
		ADD_ACTION(readBackup);
		ADD_ACTION(set);
		ADD_ACTION(clear);
		ADD_ACTION(mount);
		ADD_ACTION(unmount);
		ADD_ACTION_EX("umount", unmount);
		ADD_ACTION(restoredefaultsettings);
		ADD_ACTION(copylog);
		ADD_ACTION(compute);
		ADD_ACTION_EX("addsubtract", compute);
		ADD_ACTION(setguitimezone);
		ADD_ACTION(overlay);
		ADD_ACTION(queuezip);
		ADD_ACTION(cancelzip);
		ADD_ACTION(queueclear);
		ADD_ACTION(sleep);
		ADD_ACTION(sleepcounter);
		ADD_ACTION(appenddatetobackupname);
		ADD_ACTION(generatebackupname);
		ADD_ACTION(checkpartitionlist);
		ADD_ACTION(getpartitiondetails);
		ADD_ACTION(screenshot);
		ADD_ACTION(setbrightness);
		ADD_ACTION(fileexists);
		ADD_ACTION(killterminal);
		ADD_ACTION(checkbackupname);
		ADD_ACTION(adbsideloadcancel);
		ADD_ACTION(fixsu);
		ADD_ACTION(startmtp);
		ADD_ACTION(stopmtp);
		ADD_ACTION(cancelbackup);
		ADD_ACTION(checkpartitionlifetimewrites);
		ADD_ACTION(mountsystemtoggle);
		ADD_ACTION(setlanguage);
		ADD_ACTION(checkforapp);
		ADD_ACTION(togglebacklight);
		ADD_ACTION(enableadb);
		ADD_ACTION(enablefastboot);
		ADD_ACTION(changeterminal);
		ADD_ACTION(flashlight);
		ADD_ACTION(shrp_magisk_um);
		ADD_ACTION(unmapsuperdevices);

		// remember actions that run in the caller thread
		for (mapFunc::const_iterator it = mf.begin(); it != mf.end(); ++it)
			setActionsRunningInCallerThread.insert(it->first);

		// These actions will run in a separate thread
		ADD_ACTION(flash);
		ADD_ACTION(wipe);
		ADD_ACTION(refreshsizes);
		ADD_ACTION(nandroid);
		ADD_ACTION(fixcontexts);
		ADD_ACTION(fixpermissions);
		ADD_ACTION(dd);
		ADD_ACTION(partitionsd);
		ADD_ACTION(cmd);
		ADD_ACTION(terminalcommand);
		ADD_ACTION(reinjecttwrp);
		ADD_ACTION(decrypt);
		ADD_ACTION(adbsideload);
		ADD_ACTION(openrecoveryscript);
		ADD_ACTION(installsu);
		ADD_ACTION(decrypt_backup);
		ADD_ACTION(repair);
		ADD_ACTION(resize);
		ADD_ACTION(changefilesystem);
		ADD_ACTION(flashimage);
		ADD_ACTION(twcmd);
		ADD_ACTION(setbootslot);
		ADD_ACTION(installapp);
		ADD_ACTION(uninstalltwrpsystemapp);
		ADD_ACTION(repackimage);
		ADD_ACTION(reflashtwrp);
		ADD_ACTION(fixabrecoverybootloop);
		ADD_ACTION(applycustomtwrpfolder);
#ifndef TW_EXCLUDE_NANO
		ADD_ACTION(editfile);
#endif
		ADD_ACTION(shrp_init);
		ADD_ACTION(shrp_magisk_info);
		ADD_ACTION(shrp_magisk_mi);
		ADD_ACTION(shrp_zip_init);
		ADD_ACTION(sig);
		ADD_ACTION(unlock);
		ADD_ACTION(set_lock);
		ADD_ACTION(reset_lock);
		ADD_ACTION(c_repack);
		ADD_ACTION(flashOP);
		ADD_ACTION(clearInput);
		ADD_ACTION(getFileInfo);
		ADD_ACTION(themeInit);
		ADD_ACTION(SetColor);
		ADD_ACTION(applyDefaultTheme);
		ADD_ACTION(applyCustomTheme);
		ADD_ACTION(fileManagerOp);
		ADD_ACTION(fTools);
		ADD_ACTION(revDir);
		ADD_ACTION(flashBridge);
		ADD_ACTION(mergesnapshots);
	}

	// First, get the action
	actions = FindNode(node, "actions");
	if (actions)	child = FindNode(actions, "action");
	else			child = FindNode(node, "action");

	if (!child) return;

	while (child)
	{
		Action action;

		attr = child->first_attribute("function");
		if (!attr)  return;

		action.mFunction = attr->value();
		action.mArg = child->value();
		mActions.push_back(action);

		child = child->next_sibling("action");
	}

	// Now, let's get either the key or region
	child = FindNode(node, "touch");
	if (child)
	{
		attr = child->first_attribute("key");
		if (attr)
		{
			std::vector<std::string> keys = TWFunc::Split_String(attr->value(), "+");
			for (size_t i = 0; i < keys.size(); ++i)
			{
				const int key = getKeyByName(keys[i]);
				mKeys[key] = false;
			}
		}
		else
		{
			attr = child->first_attribute("x");
			if (!attr)  return;
			mActionX = atol(attr->value());
			attr = child->first_attribute("y");
			if (!attr)  return;
			mActionY = atol(attr->value());
			attr = child->first_attribute("w");
			if (!attr)  return;
			mActionW = atol(attr->value());
			attr = child->first_attribute("h");
			if (!attr)  return;
			mActionH = atol(attr->value());
		}
	}
}

int GUIAction::NotifyTouch(TOUCH_STATE state, int x __unused, int y __unused)
{
	if (state == TOUCH_RELEASE)
		doActions();

	return 0;
}

int GUIAction::NotifyKey(int key, bool down)
{
	std::map<int, bool>::iterator itr = mKeys.find(key);
	if (itr == mKeys.end())
		return 1;

	bool prevState = itr->second;
	itr->second = down;

	// If there is only one key for this action, wait for key up so it
	// doesn't trigger with multi-key actions.
	// Else, check if all buttons are pressed, then consume their release events
	// so they don't trigger one-button actions and reset mKeys pressed status
	if (mKeys.size() == 1) {
		if (!down && prevState) {
			doActions();
			return 0;
		}
	} else if (down) {
		for (itr = mKeys.begin(); itr != mKeys.end(); ++itr) {
			if (!itr->second)
				return 1;
		}

		// Passed, all req buttons are pressed, reset them and consume release events
		HardwareKeyboard *kb = PageManager::GetHardwareKeyboard();
		for (itr = mKeys.begin(); itr != mKeys.end(); ++itr) {
			kb->ConsumeKeyRelease(itr->first);
			itr->second = false;
		}

		doActions();
		return 0;
	}

	return 1;
}

int GUIAction::NotifyVarChange(const std::string& varName, const std::string& value)
{
	GUIObject::NotifyVarChange(varName, value);

	if (varName.empty() && !isConditionValid() && mKeys.empty() && !mActionW)
		doActions();
	else if ((varName.empty() || IsConditionVariable(varName)) && isConditionValid() && isConditionTrue())
		doActions();

	return 0;
}

void GUIAction::simulate_progress_bar(void)
{
	gui_msg("simulating=Simulating actions...");
	for (int i = 0; i < 5; i++)
	{
		if (PartitionManager.stop_backup.get_value()) {
			DataManager::SetValue("tw_cancel_backup", 1);
			gui_msg("backup_cancel=Backup Cancelled");
			DataManager::SetValue("ui_progress", 0);
			PartitionManager.stop_backup.set_value(0);
			return;
		}
		usleep(500000);
		DataManager::SetValue("ui_progress", i * 20);
	}
}

int GUIAction::flash_zip(std::string filename, int* wipe_cache)
{
	int ret_val = 0;

	DataManager::SetValue("ui_progress", 0);
	DataManager::SetValue("ui_portion_size", 0);
	DataManager::SetValue("ui_portion_start", 0);

	if (filename.empty())
	{
		LOGERR("No file specified.\n");
		return -1;
	}

	if (!TWFunc::Path_Exists(filename)) {
		if (!PartitionManager.Mount_By_Path(filename, true)) {
			return -1;
		}
		if (!TWFunc::Path_Exists(filename)) {
			gui_msg(Msg(msg::kError, "unable_to_locate=Unable to locate {1}.")(filename));
			return -1;
		}
	}

	if (simulate) {
		simulate_progress_bar();
	} else {
		ret_val = TWinstall_zip(filename.c_str(), wipe_cache, (bool) !DataManager::GetIntValue(TW_SKIP_DIGEST_CHECK_ZIP_VAR));
		PartitionManager.Unlock_Block_Partitions();
		// Now, check if we need to ensure TWRP remains installed...
		struct stat st;
		if (stat("/system/bin/installTwrp", &st) == 0)
		{
			DataManager::SetValue("tw_operation", "Configuring TWRP");
			DataManager::SetValue("tw_partition", "");
			gui_msg("config_twrp=Configuring TWRP...");
			if (TWFunc::Exec_Cmd("/system/bin/installTwrp reinstall") < 0)
			{
				gui_msg("config_twrp_err=Unable to configure TWRP with this kernel.");
			}
		}
	}

	// Done
	DataManager::SetValue("ui_progress", 100);
	DataManager::SetValue("ui_progress", 0);
	DataManager::SetValue("ui_portion_size", 0);
	DataManager::SetValue("ui_portion_start", 0);
	return ret_val;
}

GUIAction::ThreadType GUIAction::getThreadType(const GUIAction::Action& action)
{
	string func = gui_parse_text(action.mFunction);
	bool needsThread = setActionsRunningInCallerThread.find(func) == setActionsRunningInCallerThread.end();
	if (needsThread) {
		if (func == "cancelbackup")
			return THREAD_CANCEL;
		else
			return THREAD_ACTION;
	}
	return THREAD_NONE;
}

int GUIAction::doActions()
{
	if (mActions.size() < 1)
		return -1;

	// Determine in which thread to run the actions.
	// Do it for all actions at once before starting, so that we can cancel the whole batch if the thread is already busy.
	ThreadType threadType = THREAD_NONE;
	std::vector<Action>::iterator it;
	for (it = mActions.begin(); it != mActions.end(); ++it) {
		ThreadType tt = getThreadType(*it);
		if (tt == THREAD_NONE)
			continue;
		if (threadType == THREAD_NONE)
			threadType = tt;
		else if (threadType != tt) {
			LOGERR("Can't mix normal and cancel actions in the same list.\n"
				"Running the whole batch in the cancel thread.\n");
			threadType = THREAD_CANCEL;
			break;
		}
	}

	// Now run the actions in the desired thread.
	switch (threadType) {
		case THREAD_ACTION:
			action_thread.threadActions(this);
			break;

		case THREAD_CANCEL:
			cancel_thread.threadActions(this);
			break;

		default: {
			// no iterators here because theme reloading might kill our object
			const size_t cnt = mActions.size();
			for (size_t i = 0; i < cnt; ++i)
				doAction(mActions[i]);
		}
	}

	return 0;
}

int GUIAction::doAction(Action action)
{
	DataManager::GetValue(TW_SIMULATE_ACTIONS, simulate);

	std::string function = gui_parse_text(action.mFunction);
	std::string arg = gui_parse_text(action.mArg);

	// find function and execute it
	mapFunc::const_iterator funcitr = mf.find(function);
	if (funcitr != mf.end())
		return (this->*funcitr->second)(arg);

	LOGERR("Unknown action '%s'\n", function.c_str());
	return -1;
}

void GUIAction::operation_start(const string operation_name)
{
	LOGINFO("operation_start: '%s'\n", operation_name.c_str());
	time(&Start);
	DataManager::SetValue(TW_ACTION_BUSY, 1);
	DataManager::SetValue("ui_progress", 0);
	DataManager::SetValue("ui_portion_size", 0);
	DataManager::SetValue("ui_portion_start", 0);
	DataManager::SetValue("tw_operation", operation_name);
	DataManager::SetValue("tw_operation_state", 0);
	DataManager::SetValue("tw_operation_status", 0);
	bool tw_ab_device = TWFunc::get_log_dir() != CACHE_LOGS_DIR;
	DataManager::SetValue("tw_ab_device", tw_ab_device);
}

void GUIAction::operation_end(const int operation_status)
{
	time_t Stop;
	int simulate_fail;
	DataManager::SetValue("ui_progress", 100);
	if (simulate) {
		DataManager::GetValue(TW_SIMULATE_FAIL, simulate_fail);
		if (simulate_fail != 0)
			DataManager::SetValue("tw_operation_status", 1);
		else
			DataManager::SetValue("tw_operation_status", 0);
	} else {
		if (operation_status != 0) {
			DataManager::SetValue("tw_operation_status", 1);
		}
		else {
			DataManager::SetValue("tw_operation_status", 0);
		}
	}
	DataManager::SetValue("tw_operation_state", 1);
	DataManager::SetValue(TW_ACTION_BUSY, 0);
	blankTimer.resetTimerAndUnblank();
	property_set("twrp.action_complete", "1");
	time(&Stop);

#ifndef TW_NO_HAPTICS
	if ((int) difftime(Stop, Start) > 10)
		DataManager::Vibrate("tw_action_vibrate");
#endif

	LOGINFO("operation_end - status=%d\n", operation_status);
}

int GUIAction::reboot(std::string arg)
{
	sync();
	DataManager::SetValue("tw_gui_done", 1);
	DataManager::SetValue("tw_reboot_arg", arg);

	return 0;
}

int GUIAction::home(std::string arg __unused)
{
	gui_changePage("main");
	return 0;
}

int GUIAction::key(std::string arg)
{
	const int key = getKeyByName(arg);
	PageManager::NotifyKey(key, true);
	PageManager::NotifyKey(key, false);
	return 0;
}

int GUIAction::page(std::string arg)
{
	property_set("twrp.action_complete", "0");
	std::string page_name = gui_parse_text(arg);
	return gui_changePage(page_name);
}

int GUIAction::reload(std::string arg __unused)
{
	PageManager::RequestReload();
	// The actual reload is handled in pages.cpp in PageManager::RunReload()
	// The reload will occur on the next Update or Render call and will
	// be performed in the rendoer thread instead of the action thread
	// to prevent crashing which could occur when we start deleting
	// GUI resources in the action thread while we attempt to render
	// with those same resources in another thread.
	return 0;
}

int GUIAction::customReload(std::string arg __unused)
{
	PageManager::RequestCustomReload(arg);
	// The actual reload is handled in pages.cpp in PageManager::RunReload()
	// The reload will occur on the next Update or Render call and will
	// be performed in the rendoer thread instead of the action thread
	// to prevent crashing which could occur when we start deleting
	// GUI resources in the action thread while we attempt to render
	// with those same resources in another thread.
	return 0;
}

int GUIAction::readBackup(std::string arg __unused)
{
	string Restore_Name;

	DataManager::GetValue("tw_restore", Restore_Name);
	PartitionManager.Set_Restore_Files(Restore_Name);
	return 0;
}

int GUIAction::set(std::string arg)
{
	if (arg.find('=') != string::npos)
	{
		string varName = arg.substr(0, arg.find('='));
		string value = arg.substr(arg.find('=') + 1, string::npos);

		DataManager::GetValue(value, value);
		DataManager::SetValue(varName, value);
	}
	else
		DataManager::SetValue(arg, "1");
	return 0;
}

int GUIAction::clear(std::string arg)
{
	DataManager::SetValue(arg, "0");
	return 0;
}

int GUIAction::mount(std::string arg)
{
	if (arg == "usb") {
		DataManager::SetValue(TW_ACTION_BUSY, 1);
		if (!simulate)
			PartitionManager.usb_storage_enable();
		else
			gui_msg("simulating=Simulating actions...");
	} else if (!simulate) {
		PartitionManager.Mount_By_Path(arg, true);
		PartitionManager.Add_MTP_Storage(arg);
	} else
		gui_msg("simulating=Simulating actions...");
	return 0;
}

int GUIAction::unmount(std::string arg)
{
	if (arg == "usb") {
		if (!simulate)
			PartitionManager.usb_storage_disable();
		else
			gui_msg("simulating=Simulating actions...");
		DataManager::SetValue(TW_ACTION_BUSY, 0);
	} else if (!simulate) {
		PartitionManager.UnMount_By_Path(arg, true);
	} else
		gui_msg("simulating=Simulating actions...");
	return 0;
}

int GUIAction::restoredefaultsettings(std::string arg __unused)
{
	operation_start("Restore Defaults");
	if (simulate) // Simulated so that people don't accidently wipe out the "simulation is on" setting
		gui_msg("simulating=Simulating actions...");
	else {
		//SHRP
		funcRet F;
		F = FlashManager::funcInit("restoredefaultsettings");
		//SHRP
		if(arg!="theme"){
			DataManager::ResetDefaults();
			PartitionManager.Update_System_Details();
			PartitionManager.Mount_Current_Storage(true);
		}
	}
	operation_end(0);
	return 0;
}

int GUIAction::copylog(std::string arg __unused)
{
	operation_start("Copy Log");
	if (!simulate)
	{
		string dst, curr_storage;
		int copy_kernel_log = 0;
		int copy_logcat = 1;

		DataManager::GetValue("tw_include_kernel_log", copy_kernel_log);
		DataManager::GetValue("tw_include_logcat", copy_logcat);
		PartitionManager.Mount_Current_Storage(true);
		curr_storage = DataManager::GetCurrentStoragePath();
		dst = curr_storage + "/recovery.log";
		TWFunc::copy_file("/tmp/recovery.log", dst.c_str(), 0755);
		tw_set_default_metadata(dst.c_str());
		if (copy_kernel_log)
			TWFunc::copy_kernel_log(curr_storage);
		if (copy_logcat)
			TWFunc::copy_logcat(curr_storage);
		sync();
		gui_msg(Msg("copy_log=Copied recovery log to {1}")(dst));
	} else
		simulate_progress_bar();
	operation_end(0);
	return 0;
}


int GUIAction::compute(std::string arg)
{
	if (arg.find("+") != string::npos)
	{
		string varName = arg.substr(0, arg.find('+'));
		string string_to_add = arg.substr(arg.find('+') + 1, string::npos);
		int amount_to_add = atoi(string_to_add.c_str());
		int value;

		DataManager::GetValue(varName, value);
		DataManager::SetValue(varName, value + amount_to_add);
		return 0;
	}
	if (arg.find("-") != string::npos)
	{
		string varName = arg.substr(0, arg.find('-'));
		string string_to_subtract = arg.substr(arg.find('-') + 1, string::npos);
		int amount_to_subtract = atoi(string_to_subtract.c_str());
		int value;

		DataManager::GetValue(varName, value);
		value -= amount_to_subtract;
		if (value <= 0)
			value = 0;
		DataManager::SetValue(varName, value);
		return 0;
	}
	if (arg.find("*") != string::npos)
	{
		string varName = arg.substr(0, arg.find('*'));
		string multiply_by_str = gui_parse_text(arg.substr(arg.find('*') + 1, string::npos));
		int multiply_by = atoi(multiply_by_str.c_str());
		int value;

		DataManager::GetValue(varName, value);
		DataManager::SetValue(varName, value*multiply_by);
		return 0;
	}
	if (arg.find("/") != string::npos)
	{
		string varName = arg.substr(0, arg.find('/'));
		string divide_by_str = gui_parse_text(arg.substr(arg.find('/') + 1, string::npos));
		int divide_by = atoi(divide_by_str.c_str());
		int value;

		if (divide_by != 0)
		{
			DataManager::GetValue(varName, value);
			DataManager::SetValue(varName, value/divide_by);
		}
		return 0;
	}
	LOGERR("Unable to perform compute '%s'\n", arg.c_str());
	return -1;
}

int GUIAction::setguitimezone(std::string arg __unused)
{
	string SelectedZone;
	DataManager::GetValue(TW_TIME_ZONE_GUISEL, SelectedZone); // read the selected time zone into SelectedZone
	string Zone = SelectedZone.substr(0, SelectedZone.find(';')); // parse to get time zone
	string DSTZone = SelectedZone.substr(SelectedZone.find(';') + 1, string::npos); // parse to get DST component

	int dst;
	DataManager::GetValue(TW_TIME_ZONE_GUIDST, dst); // check wether user chose to use DST

	string offset;
	DataManager::GetValue(TW_TIME_ZONE_GUIOFFSET, offset); // pull in offset

	string NewTimeZone = Zone;
	if (offset != "0")
		NewTimeZone += ":" + offset;

	if (dst != 0)
		NewTimeZone += DSTZone;

	DataManager::SetValue(TW_TIME_ZONE_VAR, NewTimeZone);
	DataManager::update_tz_environment_variables();
	return 0;
}

int GUIAction::overlay(std::string arg)
{
	return gui_changeOverlay(arg);
}

int GUIAction::queuezip(std::string arg __unused)
{
	if (zip_queue_index >= 10) {
		gui_msg("max_queue=Maximum zip queue reached!");
		return 0;
	}
	DataManager::GetValue("tw_filename", zip_queue[zip_queue_index]);
	if (strlen(zip_queue[zip_queue_index].c_str()) > 0) {
		zip_queue_index++;
		DataManager::SetValue(TW_ZIP_QUEUE_COUNT, zip_queue_index);
	}
	return 0;
}

int GUIAction::cancelzip(std::string arg __unused)
{
	if (zip_queue_index <= 0) {
		gui_msg("min_queue=Minimum zip queue reached!");
		return 0;
	} else {
		zip_queue_index--;
		DataManager::SetValue(TW_ZIP_QUEUE_COUNT, zip_queue_index);
	}
	return 0;
}

int GUIAction::queueclear(std::string arg __unused)
{
	zip_queue_index = 0;
	DataManager::SetValue(TW_ZIP_QUEUE_COUNT, zip_queue_index);
	return 0;
}

int GUIAction::sleep(std::string arg)
{
	operation_start("Sleep");
	usleep(atoi(arg.c_str()));
	operation_end(0);
	return 0;
}

int GUIAction::sleepcounter(std::string arg)
{
	operation_start("SleepCounter");
	// Ensure user notices countdown in case it needs to be cancelled
	blankTimer.resetTimerAndUnblank();
	int total = atoi(arg.c_str());
	for (int t = total; t > 0; t--) {
		int progress = (int)(((float)(total-t)/(float)total)*100.0);
		DataManager::SetValue("ui_progress", progress);
		::sleep(1);
		DataManager::SetValue("tw_sleep", t-1);
	}
	DataManager::SetValue("ui_progress", 100);
	operation_end(0);
	return 0;
}

int GUIAction::appenddatetobackupname(std::string arg __unused)
{
	operation_start("AppendDateToBackupName");
	string Backup_Name;
	DataManager::GetValue(TW_BACKUP_NAME, Backup_Name);
	Backup_Name += TWFunc::Get_Current_Date();
	if (Backup_Name.size() > MAX_BACKUP_NAME_LEN)
		Backup_Name.resize(MAX_BACKUP_NAME_LEN);
	DataManager::SetValue(TW_BACKUP_NAME, Backup_Name);
	PageManager::NotifyKey(KEY_END, true);
	PageManager::NotifyKey(KEY_END, false);
	operation_end(0);
	return 0;
}

int GUIAction::generatebackupname(std::string arg __unused)
{
	operation_start("GenerateBackupName");
	TWFunc::Auto_Generate_Backup_Name();
	operation_end(0);
	return 0;
}

int GUIAction::checkpartitionlist(std::string arg)
{
	string List, part_path;
	int count = 0;

	if (arg.empty())
		arg = "tw_wipe_list";
	DataManager::GetValue(arg, List);
	LOGINFO("checkpartitionlist list '%s'\n", List.c_str());
	if (!List.empty()) {
		size_t start_pos = 0, end_pos = List.find(";", start_pos);
		while (end_pos != string::npos && start_pos < List.size()) {
			part_path = List.substr(start_pos, end_pos - start_pos);
			LOGINFO("checkpartitionlist part_path '%s'\n", part_path.c_str());
			if (part_path == "/and-sec" || part_path == "DALVIK" || part_path == "INTERNAL") {
				// Do nothing
			} else {
				count++;
			}
			start_pos = end_pos + 1;
			end_pos = List.find(";", start_pos);
		}
		DataManager::SetValue("tw_check_partition_list", count);
	} else {
		DataManager::SetValue("tw_check_partition_list", 0);
	}
	return 0;
}

int GUIAction::getpartitiondetails(std::string arg)
{
	string List, part_path;

	if (arg.empty())
		arg = "tw_wipe_list";
	DataManager::GetValue(arg, List);
	LOGINFO("getpartitiondetails list '%s'\n", List.c_str());
	if (!List.empty()) {
		size_t start_pos = 0, end_pos = List.find(";", start_pos);
		part_path = List;
		while (end_pos != string::npos && start_pos < List.size()) {
			part_path = List.substr(start_pos, end_pos - start_pos);
			LOGINFO("getpartitiondetails part_path '%s'\n", part_path.c_str());
			if (part_path == "/and-sec" || part_path == "DALVIK" || part_path == "INTERNAL") {
				// Do nothing
			} else {
				DataManager::SetValue("tw_partition_path", part_path);
				break;
			}
			start_pos = end_pos + 1;
			end_pos = List.find(";", start_pos);
		}
		if (!part_path.empty()) {
			TWPartition* Part = PartitionManager.Find_Partition_By_Path(part_path);
			if (Part) {
				unsigned long long mb = 1048576;

				DataManager::SetValue("tw_partition_name", Part->Display_Name);
				DataManager::SetValue("tw_partition_mount_point", Part->Mount_Point);
				DataManager::SetValue("tw_partition_file_system", Part->Current_File_System);
				DataManager::SetValue("tw_partition_size", Part->Size / mb);
				DataManager::SetValue("tw_partition_used", Part->Used / mb);
				DataManager::SetValue("tw_partition_free", Part->Free / mb);
				DataManager::SetValue("tw_partition_backup_size", Part->Backup_Size / mb);
				DataManager::SetValue("tw_partition_removable", Part->Removable);
				DataManager::SetValue("tw_partition_is_present", Part->Is_Present);

				if (Part->Can_Repair())
					DataManager::SetValue("tw_partition_can_repair", 1);
				else
					DataManager::SetValue("tw_partition_can_repair", 0);
				if (Part->Can_Resize())
					DataManager::SetValue("tw_partition_can_resize", 1);
				else
					DataManager::SetValue("tw_partition_can_resize", 0);
				if (TWFunc::Path_Exists("/system/bin/mkfs.fat"))
					DataManager::SetValue("tw_partition_vfat", 1);
				else
					DataManager::SetValue("tw_partition_vfat", 0);
				if (TWFunc::Path_Exists("/system/bin/mkexfatfs"))
					DataManager::SetValue("tw_partition_exfat", 1);
				else
					DataManager::SetValue("tw_partition_exfat", 0);
				if (TWFunc::Path_Exists("/system/bin/mkfs.f2fs") || TWFunc::Path_Exists("/system/bin/make_f2fs"))
					DataManager::SetValue("tw_partition_f2fs", 1);
				else
					DataManager::SetValue("tw_partition_f2fs", 0);
				if (TWFunc::Path_Exists("/system/bin/mke2fs"))
					DataManager::SetValue("tw_partition_ext", 1);
				else
					DataManager::SetValue("tw_partition_ext", 0);
				return 0;
			} else {
				LOGERR("Unable to locate partition: '%s'\n", part_path.c_str());
			}
		}
	}
	DataManager::SetValue("tw_partition_name", "");
	DataManager::SetValue("tw_partition_file_system", "");
	// Set this to 0 to prevent trying to partition this device, just in case
	DataManager::SetValue("tw_partition_removable", 0);
	return 0;
}

int GUIAction::screenshot(std::string arg __unused)
{
	time_t tm;
	char path[256];
	int path_len;
	uid_t uid = AID_MEDIA_RW;
	gid_t gid = AID_MEDIA_RW;

	const std::string storage = DataManager::GetCurrentStoragePath();
	if (PartitionManager.Is_Mounted_By_Path(storage)) {
		snprintf(path, sizeof(path), "%s/Pictures/Screenshots/", storage.c_str());
	} else {
		strcpy(path, "/tmp/");
	}

	if (!TWFunc::Create_Dir_Recursive(path, 0775, uid, gid))
		return 0;

	tm = time(NULL);
	path_len = strlen(path);

	// Screenshot_2014-01-01-18-21-38.png
	strftime(path+path_len, sizeof(path)-path_len, "Screenshot_%Y-%m-%d-%H-%M-%S.png", localtime(&tm));

	int res = gr_save_screenshot(path);
	if (res == 0) {
		chmod(path, 0666);
		chown(path, uid, gid);

		gui_msg(Msg("screenshot_saved=Screenshot was saved to {1}")(path));

		// blink to notify that the screenshot was taken
		gr_color(255, 255, 255, 255);
		gr_fill(0, 0, gr_fb_width(), gr_fb_height());
		gr_flip();
		gui_forceRender();
	} else {
		gui_err("screenshot_err=Failed to take a screenshot!");
	}
	return 0;
}

int GUIAction::setbrightness(std::string arg)
{
	return TWFunc::Set_Brightness(arg);
}

int GUIAction::fileexists(std::string arg)
{
	struct stat st;
	string newpath = arg + "/.";

	operation_start("FileExists");
	if (stat(arg.c_str(), &st) == 0 || stat(newpath.c_str(), &st) == 0)
		operation_end(0);
	else
		operation_end(1);
	return 0;
}

void GUIAction::reinject_after_flash()
{
	if (DataManager::GetIntValue(TW_HAS_INJECTTWRP) == 1 && DataManager::GetIntValue(TW_INJECT_AFTER_ZIP) == 1) {
		gui_msg("injecttwrp=Injecting TWRP into boot image...");
		if (simulate) {
			simulate_progress_bar();
		} else {
			TWPartition* Boot = PartitionManager.Find_Partition_By_Path("/boot");
			if (Boot == NULL || Boot->Current_File_System != "emmc")
				TWFunc::Exec_Cmd("injecttwrp --dump /tmp/backup_recovery_ramdisk.img /tmp/injected_boot.img --flash");
			else {
				string injectcmd = "injecttwrp --dump /tmp/backup_recovery_ramdisk.img /tmp/injected_boot.img --flash bd=" + Boot->Actual_Block_Device;
				TWFunc::Exec_Cmd(injectcmd);
			}
			gui_msg("done=Done.");
		}
	}
}

int GUIAction::ozip_decrypt(string zip_path)
{
	if (!TWFunc::Path_Exists("/system/bin/ozip_decrypt")) {
            return 1;
        }
    gui_msg("ozip_decrypt_decryption=Starting Ozip Decryption...");
	TWFunc::Exec_Cmd("ozip_decrypt " + (string)TW_OZIP_DECRYPT_KEY + " '" + zip_path + "'");
    gui_msg("ozip_decrypt_finish=Ozip Decryption Finished!");
	return 0;
}

int GUIAction::flash(std::string arg)
{
	int i, ret_val = 0, wipe_cache = 0;
	// We're going to jump to this page first, like a loading page
	gui_changePage(arg);
	//SHRP
	funcRet F;
	F = FlashManager::funcInit("flash");
	//SHRP
	for (i=0; i<zip_queue_index; i++) {
		string zip_path = zip_queue[i];
		size_t slashpos = zip_path.find_last_of('/');
		string zip_filename = (slashpos == string::npos) ? zip_path : zip_path.substr(slashpos + 1);
		operation_start("Flashing");
		if((zip_path.substr(zip_path.size() - 4, 4)) == "ozip")
		{
			if((ozip_decrypt(zip_path)) != 0)
			{
		LOGERR("Unable to find ozip_decrypt!");
				break;
			}
			zip_filename = (zip_filename.substr(0, zip_filename.size() - 4)).append("zip");
			zip_path = (zip_path.substr(0, zip_path.size() - 4)).append("zip");
			if (!TWFunc::Path_Exists(zip_path)) {
				LOGERR("Unable to find decrypted zip");
				break;
			}
		}
		DataManager::SetValue("tw_filename", zip_path);
		DataManager::SetValue("tw_file", zip_filename);
		DataManager::SetValue(TW_ZIP_INDEX, (i + 1));

		TWFunc::SetPerformanceMode(true);
		ret_val = flash_zip(zip_path, &wipe_cache);
		TWFunc::SetPerformanceMode(false);
		if (ret_val != 0) {
			gui_msg(Msg(msg::kError, "zip_err=Error installing zip file '{1}'")(zip_path));
			ret_val = 1;
			break;
		}
	}
	zip_queue_index = 0;

	if (wipe_cache) {
		gui_msg("zip_wipe_cache=One or more zip requested a cache wipe -- Wiping cache now.");
		PartitionManager.Wipe_By_Path("/cache");
	}

	reinject_after_flash();
	PartitionManager.Update_System_Details();
	//SHRP
	FlashManager::funcPost("flash", F);
	//SHRP
	operation_end(ret_val);
	// This needs to be after the operation_end call so we change pages before we change variables that we display on the screen
	DataManager::SetValue(TW_ZIP_QUEUE_COUNT, zip_queue_index);
	return 0;
}

int GUIAction::wipe(std::string arg)
{
	operation_start("Format");
	DataManager::SetValue("tw_partition", arg);
	int ret_val = false;
	//SHRP
	funcRet F;
	F = FlashManager::funcInit("wipe");
	//SHRP

	if (simulate) {
		simulate_progress_bar();
	} else {
		if (arg == "data")
			ret_val = PartitionManager.Factory_Reset();
		else if (arg == "battery")
			ret_val = PartitionManager.Wipe_Battery_Stats();
		else if (arg == "rotate")
			ret_val = PartitionManager.Wipe_Rotate_Data();
		else if (arg == "dalvik")
			ret_val = PartitionManager.Wipe_Dalvik_Cache();
		else if (arg == "DATAMEDIA") {
			ret_val = PartitionManager.Format_Data();
		} else if (arg == "INTERNAL") {
			int has_datamedia;

			DataManager::GetValue(TW_HAS_DATA_MEDIA, has_datamedia);
			if (has_datamedia) {
				ret_val = PartitionManager.Wipe_Media_From_Data();
			} else {
				ret_val = PartitionManager.Wipe_By_Path(DataManager::GetCurrentStoragePath());
			}
		} else if (arg == "EXTERNAL") {
			string External_Path;

			DataManager::GetValue(TW_EXTERNAL_PATH, External_Path);
			ret_val = PartitionManager.Wipe_By_Path(External_Path);
		} else if (arg == "ANDROIDSECURE") {
			ret_val = PartitionManager.Wipe_Android_Secure();
		} else if (arg == "LIST") {
			string Wipe_List, wipe_path;
			bool skip = false;
			ret_val = true;

			DataManager::GetValue("tw_wipe_list", Wipe_List);
			LOGINFO("wipe list '%s'\n", Wipe_List.c_str());
			if (!Wipe_List.empty()) {
				size_t start_pos = 0, end_pos = Wipe_List.find(";", start_pos);
				while (end_pos != string::npos && start_pos < Wipe_List.size()) {
					wipe_path = Wipe_List.substr(start_pos, end_pos - start_pos);
					LOGINFO("wipe_path '%s'\n", wipe_path.c_str());
					if (wipe_path == "/and-sec") {
						if (!PartitionManager.Wipe_Android_Secure()) {
							gui_msg("and_sec_wipe_err=Unable to wipe android secure");
							ret_val = false;
							break;
						} else {
							skip = true;
						}
					} else if (wipe_path == "DALVIK") {
						if (!PartitionManager.Wipe_Dalvik_Cache()) {
							gui_err("dalvik_wipe_err=Failed to wipe dalvik");
							ret_val = false;
							break;
						} else {
							skip = true;
						}
					} else if (wipe_path == "INTERNAL") {
						if (!PartitionManager.Wipe_Media_From_Data()) {
							ret_val = false;
							break;
						} else {
							skip = true;
						}
					}
					if (!skip) {
						if (!PartitionManager.Wipe_By_Path(wipe_path)) {
							gui_msg(Msg(msg::kError, "unable_to_wipe=Unable to wipe {1}.")(wipe_path));
							ret_val = false;
							break;
						} else if (wipe_path == DataManager::GetCurrentStoragePath()) {
							arg = wipe_path;
						}
					} else {
						skip = false;
					}
					start_pos = end_pos + 1;
					end_pos = Wipe_List.find(";", start_pos);
				}
			}
		} else
			ret_val = PartitionManager.Wipe_By_Path(arg);
#ifndef TW_OEM_BUILD
		if (arg == DataManager::GetSettingsStoragePath()) {
			// If we wiped the settings storage path, recreate the TWRP folder and dump the settings
			string Storage_Path = DataManager::GetSettingsStoragePath();

			if (PartitionManager.Mount_By_Path(Storage_Path, true)) {
				LOGINFO("Making TWRP folder and saving settings.\n");
				Storage_Path += "/TWRP";
				mkdir(Storage_Path.c_str(), 0777);
				DataManager::Flush();
			} else {
				LOGERR("Unable to recreate TWRP folder and save settings.\n");
			}
		}
#endif
	}
	PartitionManager.Update_System_Details();
	if (ret_val)
		ret_val = 0; // 0 is success
	else
		ret_val = 1; // 1 is failure
	//SHRP
	FlashManager::funcPost("wipe", F);
	//SHRP
	operation_end(ret_val);
	return 0;
}

int GUIAction::refreshsizes(std::string arg __unused)
{
	operation_start("Refreshing Sizes");
	if (simulate) {
		simulate_progress_bar();
	} else
		PartitionManager.Update_System_Details();
	operation_end(0);
	return 0;
}

int GUIAction::nandroid(std::string arg)
{
	if (simulate) {
		PartitionManager.stop_backup.set_value(0);
		DataManager::SetValue("tw_partition", "Simulation");
		simulate_progress_bar();
		operation_end(0);
	} else {
		operation_start("Nandroid");
		int ret = 0;

		if (arg == "backup") {
			string Backup_Name;
			DataManager::GetValue(TW_BACKUP_NAME, Backup_Name);
			string auto_gen = gui_lookup("auto_generate", "(Auto Generate)");
			if (Backup_Name == auto_gen || Backup_Name == gui_lookup("curr_date", "(Current Date)") || Backup_Name == "0" || Backup_Name == "(" || PartitionManager.Check_Backup_Name(Backup_Name, true, true) == 0) {
				ret = PartitionManager.Run_Backup(false);
				DataManager::SetValue("tw_encrypt_backup", 0); // reset value so we don't encrypt every subsequent backup
				if (!PartitionManager.stop_backup.get_value()) {
					if (ret == false)
						ret = 1; // 1 for failure
					else
						ret = 0; // 0 for success
					DataManager::SetValue("tw_cancel_backup", 0);
				} else {
					DataManager::SetValue("tw_cancel_backup", 1);
					gui_msg("backup_cancel=Backup Cancelled");
					ret = 0;
				}
			} else {
				operation_end(1);
				return -1;
			}
			DataManager::SetValue(TW_BACKUP_NAME, auto_gen);
		} else if (arg == "restore") {
			//SHRP
			funcRet F;
			F = FlashManager::funcInit("nandroid");
			//SHRP
			string Restore_Name;
			int gui_adb_backup;

			DataManager::GetValue("tw_restore", Restore_Name);
			DataManager::GetValue("tw_enable_adb_backup", gui_adb_backup);
			if (gui_adb_backup) {
				DataManager::SetValue("tw_operation_state", 1);
				if (TWFunc::stream_adb_backup(Restore_Name) == 0)
					ret = 0; // success
				else
					ret = 1; // failure
				DataManager::SetValue("tw_enable_adb_backup", 0);
				ret = 0; // assume success???
			} else {
				if (PartitionManager.Run_Restore(Restore_Name))
					ret = 0; // success
				else
					ret = 1; // failure
			}
			//SHRP
			FlashManager::funcPost("nandroid", F);
			//SHRP
		} else {
			operation_end(1); // invalid arg specified, fail
			return -1;
		}
		operation_end(ret);
		return ret;
	}
	return 0;
}

int GUIAction::cancelbackup(std::string arg __unused) {
	if (simulate) {
		PartitionManager.stop_backup.set_value(1);
	}
	else {
		int op_status = PartitionManager.Cancel_Backup();
		if (op_status != 0)
			op_status = 1; // failure
	}

	return 0;
}

int GUIAction::fixcontexts(std::string arg __unused)
{
	int op_status = 0;

	operation_start("Fix Contexts");
	LOGINFO("fix contexts started!\n");
	if (simulate) {
		simulate_progress_bar();
	} else {
		op_status = PartitionManager.Fix_Contexts();
		if (op_status != 0)
			op_status = 1; // failure
	}
	operation_end(op_status);
	return 0;
}

int GUIAction::fixpermissions(std::string arg)
{
	return fixcontexts(arg);
}

int GUIAction::dd(std::string arg)
{
	operation_start("imaging");

	if (simulate) {
		simulate_progress_bar();
	} else {
		string cmd = "dd " + arg;
		TWFunc::Exec_Cmd(cmd);
	}
	operation_end(0);
	return 0;
}

int GUIAction::partitionsd(std::string arg __unused)
{
	operation_start("Partition SD Card");
	int ret_val = 0;

	if (simulate) {
		simulate_progress_bar();
	} else {
		int allow_partition;
		DataManager::GetValue(TW_ALLOW_PARTITION_SDCARD, allow_partition);
		if (allow_partition == 0) {
			gui_err("no_real_sdcard=This device does not have a real SD Card! Aborting!");
		} else {
			if (!PartitionManager.Partition_SDCard())
				ret_val = 1; // failed
		}
	}
	operation_end(ret_val);
	return 0;

}

int GUIAction::cmd(std::string arg)
{
	int op_status = 0;

	operation_start("Command");
	LOGINFO("Running command: '%s'\n", arg.c_str());
	if (simulate) {
		simulate_progress_bar();
	} else {
		op_status = TWFunc::Exec_Cmd(arg);
		if (op_status != 0)
			op_status = 1;
	}

	operation_end(op_status);
	return 0;
}

int GUIAction::terminalcommand(std::string arg)
{
	int op_status = 0;
	string cmdpath, command;

	DataManager::GetValue("tw_terminal_location", cmdpath);
	operation_start("CommandOutput");
	gui_print("%s # %s\n", cmdpath.c_str(), arg.c_str());
	if (simulate) {
		simulate_progress_bar();
		operation_end(op_status);
	} else if (arg == "exit") {
		LOGINFO("Exiting terminal\n");
		operation_end(op_status);
		page("main");
	} else {
		command = "cd \"" + cmdpath + "\" && " + arg + " 2>&1";;
		LOGINFO("Actual command is: '%s'\n", command.c_str());
		DataManager::SetValue("tw_terminal_state", 1);
		DataManager::SetValue("tw_background_thread_running", 1);
		FILE* fp;
		char line[512];

		fp = popen(command.c_str(), "r");
		if (fp == NULL) {
			LOGERR("Error opening command to run (%s).\n", strerror(errno));
		} else {
			int fd = fileno(fp), has_data = 0, check = 0, keep_going = -1;
			struct timeval timeout;
			fd_set fdset;

			while (keep_going)
			{
				FD_ZERO(&fdset);
				FD_SET(fd, &fdset);
				timeout.tv_sec = 0;
				timeout.tv_usec = 400000;
				has_data = select(fd+1, &fdset, NULL, NULL, &timeout);
				if (has_data == 0) {
					// Timeout reached
					DataManager::GetValue("tw_terminal_state", check);
					if (check == 0) {
						keep_going = 0;
					}
				} else if (has_data < 0) {
					// End of execution
					keep_going = 0;
				} else {
					// Try to read output
					if (fgets(line, sizeof(line), fp) != NULL)
						gui_print("%s", line); // Display output
					else
						keep_going = 0; // Done executing
				}
			}
			fclose(fp);
		}
		DataManager::SetValue("tw_operation_status", 0);
		DataManager::SetValue("tw_operation_state", 1);
		DataManager::SetValue("tw_terminal_state", 0);
		DataManager::SetValue("tw_background_thread_running", 0);
		DataManager::SetValue(TW_ACTION_BUSY, 0);
	}
	return 0;
}

int GUIAction::killterminal(std::string arg __unused)
{
	LOGINFO("Sending kill command...\n");
	operation_start("KillCommand");
	DataManager::SetValue("tw_operation_status", 0);
	DataManager::SetValue("tw_operation_state", 1);
	DataManager::SetValue("tw_terminal_state", 0);
	DataManager::SetValue("tw_background_thread_running", 0);
	DataManager::SetValue(TW_ACTION_BUSY, 0);
	return 0;
}

int GUIAction::reinjecttwrp(std::string arg __unused)
{
	int op_status = 0;
	operation_start("ReinjectTWRP");
	gui_msg("injecttwrp=Injecting TWRP into boot image...");
	if (simulate) {
		simulate_progress_bar();
	} else {
		TWFunc::Exec_Cmd("injecttwrp --dump /tmp/backup_recovery_ramdisk.img /tmp/injected_boot.img --flash");
		gui_msg("done=Done.");
	}

	operation_end(op_status);
	return 0;
}

int GUIAction::checkbackupname(std::string arg __unused)
{
	int op_status = 0;

	operation_start("CheckBackupName");
	if (simulate) {
		simulate_progress_bar();
	} else {
		string Backup_Name;
		DataManager::GetValue(TW_BACKUP_NAME, Backup_Name);
		op_status = PartitionManager.Check_Backup_Name(Backup_Name, true, true);
		if (op_status != 0)
			op_status = 1;
	}

	operation_end(op_status);
	return 0;
}

int GUIAction::decrypt(std::string arg __unused)
{
	int op_status = 0;

	operation_start("Decrypt");
	if (simulate) {
		simulate_progress_bar();
	} else {
		string Password;
		string userID;
		DataManager::GetValue("tw_crypto_password", Password);

		if (DataManager::GetIntValue(TW_IS_FBE)) {  // for FBE
			DataManager::GetValue("tw_crypto_user_id", userID);
			if (userID != "") {
				op_status = PartitionManager.Decrypt_Device(Password, atoi(userID.c_str()));
				if (userID != "0") {
					if (op_status != 0)
						op_status = 1;
					operation_end(op_status);
	          		return 0;
				}
			} else {
				LOGINFO("User ID not found\n");
				op_status = 1;
			}
		::sleep(1);
		} else {  // for FDE
			op_status = PartitionManager.Decrypt_Device(Password);
		}

		if (op_status != 0)
			op_status = 1;
		else {
			DataManager::SetValue(TW_IS_ENCRYPTED, 0);

			int has_datamedia;

			// Check for a custom theme and load it if exists
			DataManager::GetValue(TW_HAS_DATA_MEDIA, has_datamedia);
			if (has_datamedia != 0) {
				if (tw_get_default_metadata(DataManager::GetSettingsStoragePath().c_str()) != 0) {
					LOGINFO("Failed to get default contexts and file mode for storage files.\n");
				} else {
					LOGINFO("Got default contexts and file mode for storage files.\n");
				}
			}
			PartitionManager.Decrypt_Adopted();
#ifdef SHRP_EXPRESS_USE_DATA
			/*
			Trying to fetch theme and other datas.
			This is essential because if data is decrpyted then 
			first init will not able to find shrp path.
			*/
			Express::updateSHRPBasePath();

#ifdef SHRP_EXPRESS
			Express::init();
			SHRP::handleLock();
#endif
#endif
		}
	}

	operation_end(op_status);
	return 0;
}

int GUIAction::adbsideload(std::string arg __unused)
{
	operation_start("Sideload");
	if (simulate) {
		simulate_progress_bar();
		operation_end(0);
	} else {
		//SHRP
		funcRet F;
		F = FlashManager::funcInit("adbsideload");
		//SHRP
		gui_msg("start_sideload=Starting ADB sideload feature...");
		bool mtp_was_enabled = TWFunc::Toggle_MTP(false);

		// wait for the adb connection
		Device::BuiltinAction reboot_action = Device::REBOOT_BOOTLOADER;
		int ret = twrp_sideload("/", &reboot_action);
		sideload_child_pid = GetMiniAdbdPid();
		DataManager::SetValue("tw_has_cancel", 0); // Remove cancel button from gui now that the zip install is going to start

		if (ret != 0) {
			if (ret == -2)
				gui_msg("need_new_adb=You need adb 1.0.32 or newer to sideload to this device.");
			ret = 1; // failure
		} else {
			int wipe_cache = 0;
			int wipe_dalvik = 0;
			DataManager::GetValue("tw_wipe_dalvik", wipe_dalvik);
			if (wipe_cache || DataManager::GetIntValue("tw_wipe_cache"))
				PartitionManager.Wipe_By_Path("/cache");
			if (wipe_dalvik)
				PartitionManager.Wipe_Dalvik_Cache();
		}
		TWFunc::Toggle_MTP(mtp_was_enabled);
		reinject_after_flash();
		operation_end(ret);
	}
	return 0;
}

int GUIAction::adbsideloadcancel(std::string arg __unused)
{
	//SHRP
	funcRet F;
	F.expressBACKUP_ret = Express::is_backupped();
	FlashManager::funcPost("adbsideloadcancel", F);
	//SHRP
	struct stat st;
	DataManager::SetValue("tw_has_cancel", 0); // Remove cancel button from gui
	gui_msg("cancel_sideload=Cancelling ADB sideload...");
	LOGINFO("Signaling child sideload process to exit.\n");
	// Calling stat() on this magic filename signals the minadbd
	// subprocess to shut down.
	stat(FUSE_SIDELOAD_HOST_EXIT_PATHNAME, &st);
	sideload_child_pid = GetMiniAdbdPid();
	if (!sideload_child_pid) {
		LOGERR("Unable to get child ID\n");
		return 0;
	}
	::sleep(1);
	LOGINFO("Killing child sideload process.\n");
	kill(sideload_child_pid, SIGTERM);
	int status;
	LOGINFO("Waiting for child sideload process to exit.\n");
	waitpid(sideload_child_pid, &status, 0);
	sideload_child_pid = 0;
	DataManager::SetValue("tw_page_done", "1"); // For OpenRecoveryScript support
	return 0;
}

int GUIAction::openrecoveryscript(std::string arg __unused)
{
	operation_start("OpenRecoveryScript");
	if (simulate) {
		simulate_progress_bar();
		operation_end(0);
	} else {
		int op_status = OpenRecoveryScript::Run_OpenRecoveryScript_Action();
		operation_end(op_status);
	}
	return 0;
}

int GUIAction::installsu(std::string arg __unused)
{
	int op_status = 0;

	operation_start("Install SuperSU");
	if (simulate) {
		simulate_progress_bar();
	} else {
		LOGERR("Installing SuperSU was deprecated from TWRP.\n");
	}

	operation_end(op_status);
	return 0;
}

int GUIAction::fixsu(std::string arg __unused)
{
	int op_status = 0;

	operation_start("Fixing Superuser Permissions");
	if (simulate) {
		simulate_progress_bar();
	} else {
		LOGERR("Fixing su permissions was deprecated from TWRP.\n");
		LOGERR("4.3+ ROMs with SELinux will always lose su perms.\n");
	}

	operation_end(op_status);
	return 0;
}

int GUIAction::decrypt_backup(std::string arg __unused)
{
	int op_status = 0;

	operation_start("Try Restore Decrypt");
	if (simulate) {
		simulate_progress_bar();
	} else {
		string Restore_Path, Filename, Password;
		DataManager::GetValue("tw_restore", Restore_Path);
		Restore_Path += "/";
		DataManager::GetValue("tw_restore_password", Password);
		TWFunc::SetPerformanceMode(true);
		if (TWFunc::Try_Decrypting_Backup(Restore_Path, Password))
			op_status = 0; // success
		else
			op_status = 1; // fail
		TWFunc::SetPerformanceMode(false);
	}

	operation_end(op_status);
	return 0;
}

int GUIAction::repair(std::string arg __unused)
{
	int op_status = 0;

	operation_start("Repair Partition");
	if (simulate) {
		simulate_progress_bar();
	} else {
		string part_path;
		DataManager::GetValue("tw_partition_mount_point", part_path);
		if (PartitionManager.Repair_By_Path(part_path, true)) {
			op_status = 0; // success
		} else {
			op_status = 1; // fail
		}
	}

	operation_end(op_status);
	return 0;
}

int GUIAction::resize(std::string arg __unused)
{
	int op_status = 0;

	operation_start("Resize Partition");
	if (simulate) {
		simulate_progress_bar();
	} else {
		string part_path;
		DataManager::GetValue("tw_partition_mount_point", part_path);
		if (PartitionManager.Resize_By_Path(part_path, true)) {
			op_status = 0; // success
		} else {
			op_status = 1; // fail
		}
	}

	operation_end(op_status);
	return 0;
}

int GUIAction::changefilesystem(std::string arg __unused)
{
	int op_status = 0;

	operation_start("Change File System");
	if (simulate) {
		simulate_progress_bar();
	} else {
		string part_path, file_system;
		DataManager::GetValue("tw_partition_mount_point", part_path);
		DataManager::GetValue("tw_action_new_file_system", file_system);
		if (PartitionManager.Wipe_By_Path(part_path, file_system)) {
			op_status = 0; // success
		} else {
			gui_err("change_fs_err=Error changing file system.");
			op_status = 1; // fail
		}
	}
	PartitionManager.Update_System_Details();
	operation_end(op_status);
	return 0;
}

int GUIAction::startmtp(std::string arg __unused)
{
	int op_status = 0;

	operation_start("Start MTP");
	if (PartitionManager.Enable_MTP())
		op_status = 0; // success
	else
		op_status = 1; // fail

	operation_end(op_status);
	return 0;
}

int GUIAction::stopmtp(std::string arg __unused)
{
	int op_status = 0;

	operation_start("Stop MTP");
	if (PartitionManager.Disable_MTP())
		op_status = 0; // success
	else
		op_status = 1; // fail

	operation_end(op_status);
	return 0;
}

int GUIAction::flashimage(std::string arg __unused)
{
	int op_status = 0;
	bool flag = true;

	operation_start("Flash Image");
	string path, filename;
	DataManager::GetValue("tw_zip_location", path);
	DataManager::GetValue("tw_file", filename);
	//SHRP
	funcRet F;
	F = FlashManager::funcInit("flashimage");
	//SHRP

#ifdef AB_OTA_UPDATER
	string target = DataManager::GetStrValue("tw_flash_partition");
	unsigned int pos = target.find_last_of(';');
	string mount_point = pos != string::npos ? target.substr(0, pos) : "";
	TWPartition* t_part = PartitionManager.Find_Partition_By_Path(mount_point);
	bool flash_in_both_slots = DataManager::GetIntValue("tw_flash_both_slots") ? true : false;

	if (t_part != NULL && (flash_in_both_slots && t_part->SlotSelect)) 
	{
		string current_slot = PartitionManager.Get_Active_Slot_Display();
		bool pre_op_status = PartitionManager.Flash_Image(path, filename);

		PartitionManager.Override_Active_Slot(current_slot == "A" ? "B" : "A");
		op_status = (int) !(pre_op_status && PartitionManager.Flash_Image(path, filename));
		PartitionManager.Override_Active_Slot(current_slot);

		DataManager::SetValue("tw_flash_both_slots", 0);
		flag = false;
	}
#endif
	if (flag)
	{
		if (PartitionManager.Flash_Image(path, filename))
			op_status = 0; // success
		else
			op_status = 1; // fail
	}
	
	//SHRP
	FlashManager::funcPost("flashimage", F);
	//SHRP
	operation_end(op_status);
	return 0;
}

int GUIAction::twcmd(std::string arg)
{
	operation_start("TWRP CLI Command");
	if (simulate)
		simulate_progress_bar();
	else
		OpenRecoveryScript::Run_CLI_Command(arg.c_str());
	operation_end(0);
	return 0;
}

int GUIAction::getKeyByName(std::string key)
{
	if (key == "home")		return KEY_HOMEPAGE;  // note: KEY_HOME is cursor movement (like KEY_END)
	else if (key == "menu")		return KEY_MENU;
	else if (key == "back")	 	return KEY_BACK;
	else if (key == "search")	return KEY_SEARCH;
	else if (key == "voldown")	return KEY_VOLUMEDOWN;
	else if (key == "volup")	return KEY_VOLUMEUP;
	else if (key == "power") {
		int ret_val;
		DataManager::GetValue(TW_POWER_BUTTON, ret_val);
		if (!ret_val)
			return KEY_POWER;
		else
			return ret_val;
	}

	return atol(key.c_str());
}

int GUIAction::checkpartitionlifetimewrites(std::string arg)
{
	int op_status = 0;
	TWPartition* sys = PartitionManager.Find_Partition_By_Path(arg);

	operation_start("Check Partition Lifetime Writes");
	if (sys) {
		if (sys->Check_Lifetime_Writes() != 0)
			DataManager::SetValue("tw_lifetime_writes", 1);
		else
			DataManager::SetValue("tw_lifetime_writes", 0);
		op_status = 0; // success
	} else {
		DataManager::SetValue("tw_lifetime_writes", 1);
		op_status = 1; // fail
	}

	operation_end(op_status);
	return 0;
}

int GUIAction::mountsystemtoggle(std::string arg)
{
	int op_status = 0;
	bool remount_system = PartitionManager.Is_Mounted_By_Path(PartitionManager.Get_Android_Root_Path());
	bool remount_vendor = PartitionManager.Is_Mounted_By_Path("/vendor");

	operation_start("Toggle System Mount");
	if (!PartitionManager.UnMount_By_Path(PartitionManager.Get_Android_Root_Path(), true)) {
		op_status = 1; // fail
	} else {
		TWPartition* Part = PartitionManager.Find_Partition_By_Path(PartitionManager.Get_Android_Root_Path());
		if (Part) {
			if (arg == "0") {
				DataManager::SetValue("tw_mount_system_ro", 0);
				Part->Change_Mount_Read_Only(false);
			} else {
				DataManager::SetValue("tw_mount_system_ro", 1);
				Part->Change_Mount_Read_Only(true);
			}
			if (remount_system) {
				Part->Mount(true);
			}
			op_status = 0; // success
		} else {
			op_status = 1; // fail
		}
		Part = PartitionManager.Find_Partition_By_Path("/vendor");
		if (Part) {
			if (arg == "0") {
				Part->Change_Mount_Read_Only(false);
			} else {
				Part->Change_Mount_Read_Only(true);
			}
			if (remount_vendor) {
				Part->Mount(true);
			}
			op_status = 0; // success
		} else {
			op_status = 1; // fail
		}
	}

	operation_end(op_status);
	return 0;
}

int GUIAction::setlanguage(std::string arg __unused)
{
	int op_status = 0;

	operation_start("Set Language");
	PageManager::LoadLanguage(DataManager::GetStrValue("tw_language"));
	PageManager::RequestCustomReload();
	op_status = 0; // success

	operation_end(op_status);
	return 0;
}

int GUIAction::togglebacklight(std::string arg __unused)
{
	blankTimer.toggleBlank();
	return 0;
}

int GUIAction::setbootslot(std::string arg)
{
	operation_start("Set Boot Slot");
	if (!simulate) {
		if (PartitionManager.Find_Partition_By_Path("/vendor")) {
			if (!PartitionManager.UnMount_By_Path("/vendor", false)) {
				// PartitionManager failed to unmount /vendor, this should not happen,
				// but in case it does, do a lazy unmount
				LOGINFO("WARNING: vendor partition could not be unmounted normally!\n");
				umount2("/vendor", MNT_DETACH);
			}
		}
		PartitionManager.Set_Active_Slot(arg);
	} else {
		simulate_progress_bar();
	}
	operation_end(0);
	return 0;
}

int GUIAction::checkforapp(std::string arg __unused)
{
	operation_start("Check for TWRP App");
	if (!simulate)
	{
		TWFunc::checkforapp();
	} else
		simulate_progress_bar();

	operation_end(0);
	return 0;
}

int GUIAction::installapp(std::string arg __unused)
{
	int op_status = 1;
	operation_start("Install TWRP App");
	if (!simulate)
	{
		if (DataManager::GetIntValue("tw_mount_system_ro") > 0 || DataManager::GetIntValue("tw_app_install_system") == 0) {
			if (PartitionManager.Mount_By_Path("/data", true)) {
				string install_path = "/data/app";
				string context = "u:object_r:apk_data_file:s0";
				if (!TWFunc::Path_Exists(install_path)) {
					if (mkdir(install_path.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH)) {
						LOGERR("Error making %s directory: %s\n", install_path.c_str(), strerror(errno));
						goto exit;
					}
					if (chown(install_path.c_str(), 1000, 1000)) {
						LOGERR("chown %s error: %s\n", install_path.c_str(), strerror(errno));
						goto exit;
					}
					if (setfilecon(install_path.c_str(), (security_context_t)context.c_str()) < 0) {
						LOGERR("setfilecon %s error: %s\n", install_path.c_str(), strerror(errno));
						goto exit;
					}
				}
				install_path += "/me.twrp.twrpapp-1";
				if (mkdir(install_path.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH)) {
					LOGERR("Error making %s directory: %s\n", install_path.c_str(), strerror(errno));
					goto exit;
				}
				if (chown(install_path.c_str(), 1000, 1000)) {
					LOGERR("chown %s error: %s\n", install_path.c_str(), strerror(errno));
					goto exit;
				}
				if (setfilecon(install_path.c_str(), (security_context_t)context.c_str()) < 0) {
					LOGERR("setfilecon %s error: %s\n", install_path.c_str(), strerror(errno));
					goto exit;
				}
				install_path += "/base.apk";
				if (TWFunc::copy_file("/system/bin/me.twrp.twrpapp.apk", install_path, 0644)) {
					LOGERR("Error copying apk file\n");
					goto exit;
				}
				if (chown(install_path.c_str(), 1000, 1000)) {
					LOGERR("chown %s error: %s\n", install_path.c_str(), strerror(errno));
					goto exit;
				}
				if (setfilecon(install_path.c_str(), (security_context_t)context.c_str()) < 0) {
					LOGERR("setfilecon %s error: %s\n", install_path.c_str(), strerror(errno));
					goto exit;
				}
				sync();
				sync();
			}
		} else {
			if (PartitionManager.Mount_By_Path(PartitionManager.Get_Android_Root_Path(), true)) {
				string base_path = PartitionManager.Get_Android_Root_Path();
				if (TWFunc::Path_Exists(PartitionManager.Get_Android_Root_Path() + "/system"))
					base_path += "/system"; // For devices with system as a root file system (e.g. Pixel)
				string install_path = base_path + "/priv-app";
				string context = "u:object_r:system_file:s0";
				if (!TWFunc::Path_Exists(install_path))
					install_path = base_path + "/app";
				if (TWFunc::Path_Exists(install_path)) {
					install_path += "/twrpapp";
					LOGINFO("Installing app to '%s'\n", install_path.c_str());
					if (mkdir(install_path.c_str(), S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) == 0) {
						if (setfilecon(install_path.c_str(), (security_context_t)context.c_str()) < 0) {
							LOGERR("setfilecon %s error: %s\n", install_path.c_str(), strerror(errno));
							goto exit;
						}
						install_path += "/me.twrp.twrpapp.apk";
						if (TWFunc::copy_file("/system/bin/me.twrp.twrpapp.apk", install_path, 0644)) {
							LOGERR("Error copying apk file\n");
							goto exit;
						}
						if (setfilecon(install_path.c_str(), (security_context_t)context.c_str()) < 0) {
							LOGERR("setfilecon %s error: %s\n", install_path.c_str(), strerror(errno));
							goto exit;
						}

						// System apps require their permissions to be pre-set via an XML file in /etc/permissions
						string permission_path = base_path + "/etc/permissions/privapp-permissions-twrpapp.xml";
						if (TWFunc::copy_file("/system/bin/privapp-permissions-twrpapp.xml", permission_path, 0644)) {
							LOGERR("Error copying permission file\n");
							goto exit;
						}
						if (chown(permission_path.c_str(), 1000, 1000)) {
							LOGERR("chown %s error: %s\n", permission_path.c_str(), strerror(errno));
							goto exit;
						}
						if (setfilecon(permission_path.c_str(), (security_context_t)context.c_str()) < 0) {
							LOGERR("setfilecon %s error: %s\n", permission_path.c_str(), strerror(errno));
							goto exit;
						}

						sync();
						sync();
						PartitionManager.UnMount_By_Path(PartitionManager.Get_Android_Root_Path(), true);
						op_status = 0;
					} else {
						LOGERR("Error making app directory '%s': %s\n", strerror(errno));
					}
				}
			}
		}
	} else
		simulate_progress_bar();
exit:
	TWFunc::checkforapp();
	operation_end(0);
	return 0;
}

int GUIAction::uninstalltwrpsystemapp(std::string arg __unused)
{
	int op_status = 1;
	operation_start("Uninstall TWRP System App");
	if (!simulate)
	{
		int Mount_System_RO = DataManager::GetIntValue("tw_mount_system_ro");
		TWPartition* Part = PartitionManager.Find_Partition_By_Path(PartitionManager.Get_Android_Root_Path());
		if (!Part) {
			LOGERR("Unabled to find system partition.\n");
			goto exit;
		}
		if (!Part->UnMount(true)) {
			goto exit;
		}
		if (Mount_System_RO > 0) {
			DataManager::SetValue("tw_mount_system_ro", 0);
			Part->Change_Mount_Read_Only(false);
		}
		if (Part->Mount(true)) {
			string base_path = PartitionManager.Get_Android_Root_Path();
			if (TWFunc::Path_Exists(PartitionManager.Get_Android_Root_Path() + "/system"))
				base_path += "/system"; // For devices with system as a root file system (e.g. Pixel)
			string uninstall_path = base_path + "/priv-app";
			if (!TWFunc::Path_Exists(uninstall_path))
				uninstall_path = base_path + "/app";
			uninstall_path += "/twrpapp";
			if (TWFunc::Path_Exists(uninstall_path)) {
				LOGINFO("Uninstalling TWRP App from '%s'\n", uninstall_path.c_str());
				if (TWFunc::removeDir(uninstall_path, false) == 0) {
					sync();
					op_status = 0;
					DataManager::SetValue("tw_app_installed_in_system", 0);
					DataManager::SetValue("tw_app_install_status", 0);
				} else {
					LOGERR("Unable to remove TWRP app from system.\n");
				}
			} else {
				LOGINFO("didn't find TWRP app in '%s'\n", uninstall_path.c_str());
			}
		}
		Part->UnMount(true);
		if (Mount_System_RO > 0) {
			DataManager::SetValue("tw_mount_system_ro", Mount_System_RO);
			Part->Change_Mount_Read_Only(true);
		}
	} else
		simulate_progress_bar();
exit:
	TWFunc::checkforapp();
	operation_end(0);
	return 0;
}

int GUIAction::repackimage(std::string arg __unused)
{
	int op_status = 1;
	twrpRepacker repacker;

	operation_start("Repack Image");
	if (!simulate)
	{
		std::string path = DataManager::GetStrValue("tw_filename");
		Repack_Options_struct Repack_Options;
		Repack_Options.Disable_Verity = false;
		Repack_Options.Disable_Force_Encrypt = false;
		Repack_Options.Backup_First = DataManager::GetIntValue("tw_repack_backup_first") != 0;
		if (DataManager::GetIntValue("tw_repack_kernel") == 1)
			Repack_Options.Type = REPLACE_KERNEL;
		else
			Repack_Options.Type = REPLACE_RAMDISK;
		if (!repacker.Repack_Image_And_Flash(path, Repack_Options))
			goto exit;
	} else
		simulate_progress_bar();
	op_status = 0;
exit:
	operation_end(op_status);
	return 0;
}

int GUIAction::reflashtwrp(std::string arg __unused)
{
	int op_status = 1;
	twrpRepacker repacker;

	operation_start("Repack Image");
	if (!simulate)
	{
		if (!repacker.Flash_Current_Twrp())
		goto exit;
	} else
		simulate_progress_bar();
	op_status = 0;
exit:
	operation_end(op_status);
	return 0;
}
int GUIAction::fixabrecoverybootloop(std::string arg __unused)
{
	int op_status = 1;
	twrpRepacker repacker;

	operation_start("Repack Image");
	if (!simulate)
	{
		if (!TWFunc::Path_Exists("/system/bin/magiskboot")) {
			LOGERR("Image repacking tool not present in this TWRP build!");
			goto exit;
		}
		DataManager::SetProgress(0);
		TWPartition* part = PartitionManager.Find_Partition_By_Path("/boot");
		if (part)
			gui_msg(Msg("unpacking_image=Unpacking {1}...")(part->Display_Name));
		else {
			gui_msg(Msg(msg::kError, "unable_to_locate=Unable to locate {1}.")("/boot"));
			goto exit;
		}
		if (!repacker.Backup_Image_For_Repack(part, REPACK_ORIG_DIR, DataManager::GetIntValue("tw_repack_backup_first") != 0, gui_lookup("repack", "Repack")))
			goto exit;
		DataManager::SetProgress(.25);
		gui_msg("fixing_recovery_loop_patch=Patching kernel...");
		std::string command = "cd " REPACK_ORIG_DIR " && /system/bin/magiskboot hexpatch kernel 77616E745F696E697472616D667300 736B69705F696E697472616D667300";
		if (TWFunc::Exec_Cmd(command) != 0) {
			gui_msg(Msg(msg::kError, "fix_recovery_loop_patch_error=Error patching kernel."));
			goto exit;
		}
		std::string header_path = REPACK_ORIG_DIR;
		header_path += "header";
		if (TWFunc::Path_Exists(header_path)) {
			command = "cd " REPACK_ORIG_DIR " && sed -i \"s|$(grep '^cmdline=' header | cut -d= -f2-)|$(grep '^cmdline=' header | cut -d= -f2- | sed -e 's/skip_override//' -e 's/  */ /g' -e 's/[ \t]*$//')|\" header";
			if (TWFunc::Exec_Cmd(command) != 0) {
				gui_msg(Msg(msg::kError, "fix_recovery_loop_patch_error=Error patching kernel."));
				goto exit;
			}
		}
		DataManager::SetProgress(.5);
		gui_msg(Msg("repacking_image=Repacking {1}...")(part->Display_Name));
		command = "cd " REPACK_ORIG_DIR " && /system/bin/magiskboot repack " REPACK_ORIG_DIR "boot.img";
		if (TWFunc::Exec_Cmd(command) != 0) {
			gui_msg(Msg(msg::kError, "repack_error=Error repacking image."));
			goto exit;
		}
		DataManager::SetProgress(.75);
		std::string path = REPACK_ORIG_DIR;
		std::string file = "new-boot.img";
		DataManager::SetValue("tw_flash_partition", "/boot;");
		if (!PartitionManager.Flash_Image(path, file)) {
			LOGINFO("Error flashing new image\n");
			goto exit;
		}
		DataManager::SetProgress(1);
		TWFunc::removeDir(REPACK_ORIG_DIR, false);
	} else
		simulate_progress_bar();
	op_status = 0;
exit:
	operation_end(op_status);
	return 0;
}


int GUIAction::enableadb(std::string arg __unused) {
	android::base::SetProperty("sys.usb.config", "none");
	android::base::SetProperty("sys.usb.config", "adb");
	return 0;
}

int GUIAction::enablefastboot(std::string arg __unused) {
	android::base::SetProperty("sys.usb.config", "none");
	android::base::SetProperty("sys.usb.config", "fastboot");
	return 0;
}

int GUIAction::changeterminal(std::string arg) {
	bool res = true;
	std::string resp, cmd = "cd " + arg;
	DataManager::GetValue("tw_terminal_location", resp);
	if (arg.empty() && !resp.empty()) {
		cmd = "cd /";
		for (uint8_t iter = 0; iter < cmd.size(); iter++)
			term->NotifyCharInput(cmd.at(iter));
		term->NotifyCharInput(13);
		DataManager::SetValue("tw_terminal_location", "");
		return 0;
	}
	if (term != NULL && !arg.empty()) {
		DataManager::SetValue("tw_terminal_location", arg);
		if (term->status()) {
			for (uint8_t iter = 0; iter < cmd.size(); iter++)
				term->NotifyCharInput(cmd.at(iter));
			term->NotifyCharInput(13);
		}
		else if (chdir(arg.c_str()) != 0) {
			LOGINFO("Unable to change dir to %s\n", arg.c_str());
			res = false;
		}
	}
	else {
		res = false;
		LOGINFO("Unable to switch to Terminal\n");
	}
	if (res)
		gui_changePage("terminalcommand");
	return 0;
}
#ifndef TW_EXCLUDE_NANO
int GUIAction::editfile(std::string arg) {
	if (term != NULL) {
		for (uint8_t iter = 0; iter < arg.size(); iter++)
			term->NotifyCharInput(arg.at(iter));
		term->NotifyCharInput(13);
	}
	else
		LOGINFO("Unable to switch to Terminal\n");
	return 0;
}
#endif

int GUIAction::unmapsuperdevices(std::string arg __unused) {
	int op_status = 1;

	operation_start("Remove Super Devices");
	if (simulate) {
		simulate_progress_bar();
	} else {
		if (PartitionManager.Unmap_Super_Devices()) {
			op_status = 0;
		}
	}

	operation_end(op_status);
	return 0;
}

int GUIAction::applycustomtwrpfolder(string arg __unused)
{
	operation_start("ChangingTWRPFolder");
	string storageFolder = DataManager::GetCurrentStoragePath();
	string newFolder = storageFolder + '/' + arg;
	string newBackupFolder = newFolder + "/BACKUPS/" + DataManager::GetStrValue("device_id");
	string prevFolder = storageFolder + DataManager::GetStrValue(TW_RECOVERY_FOLDER_VAR);
	bool ret = false;

	if (TWFunc::Path_Exists(newFolder)) {
		gui_msg(Msg(msg::kError, "tw_folder_exists=A folder with that name already exists!"));
	} else {
		ret = true;
	}

	if (newFolder != prevFolder && ret) {
		ret = TWFunc::Exec_Cmd("mv -f \"" + prevFolder + "\" \"" + newFolder + '\"') != 0 ? false : true;
	} else {
		gui_msg(Msg(msg::kError, "tw_folder_exists=A folder with that name already exists!"));
	}

	if (ret) ret = TWFunc::Recursive_Mkdir(newBackupFolder) ? true : false;


	if (ret) {
		DataManager::SetValue(TW_RECOVERY_FOLDER_VAR, '/' + arg);
		DataManager::SetValue(TW_BACKUPS_FOLDER_VAR, newBackupFolder);
	}
	operation_end((int)!ret);
	return 0;
}

int GUIAction::mergesnapshots(string arg __unused) {
	int op_status = 1;
	if (PartitionManager.Check_Pending_Merges()) {
		op_status = 0;
	}
	operation_end(op_status);
	return 0;
}

//SHRP_GUI_Funcs()
int GUIAction::shrp_init(std::string arg __unused){
	LOGINFO("Running GUI function : shrp_init\n");
	if(!TWFunc::Path_Exists("/data/adb/magisk")){
		LOGINFO("shrp_init : Magisk Not Installed\n");
		DataManager::SetValue("c_magisk_status",1);
	}else{
		LOGINFO("shrp_init : Magisk Found\n");
		DataManager::SetValue("c_magisk_status",0);
	}
	if(!TWFunc::Path_Exists("/sdcard/SHRP")){
		LOGINFO("shrp_init : SHRP Resources Not Found at /sdcard/SHRP\n");
		LOGINFO("shrp_init : Fix this issue by reflashing SHRP ZIP\n");
		DataManager::SetValue("c_shrp_resource_status",1);
	}else{
		LOGINFO("shrp_init : SHRP Resources Found\n");
		DataManager::SetValue("c_shrp_resource_status",0);
	}
	LOGINFO("Closed : shrp_init\n");
	return 0;
}

int GUIAction::shrp_magisk_info(std::string arg __unused){
	if (!DataManager::GetIntValue("c_magisk_status")) TWFunc::Exec_Cmd("sh /twres/scripts/magisk_ver.sh");
	string core_only_1="/cache/.disable_magisk";
	string core_only_2="/data/cache/.disable_magisk";
	uint64_t h1=0;
	float v;
	if(TWFunc::Path_Exists("/tmp/magisk_var.txt")){
		TWFunc::read_file("/tmp/magisk_var.txt",h1);
		if(h1<1000){
			DataManager::SetValue("c_magisk_ver","N/A");
			//DataManager::SetValue("c_magisk_update","1");
			DataManager::SetValue("shrpMagiskRoot", "1");
			DataManager::SetValue("shrpMagiskUnroot", "1");
			DataManager::SetValue("shrpMagiskUpdate", "0");
		}else{
			v=(float)h1/1000;
			DataManager::SetValue("c_magisk_ver",v);
			float tmp;
			DataManager::GetValue("c_magisk_stock_var", tmp);
			if(tmp>v){
				//DataManager::SetValue("c_magisk_update","1");
				DataManager::SetValue("shrpMagiskRoot", "0");
				DataManager::SetValue("shrpMagiskUnroot", "1");
				DataManager::SetValue("shrpMagiskUpdate", "1");
			}else{
				DataManager::SetValue("shrpMagiskRoot", "1");
				DataManager::SetValue("shrpMagiskUnroot", "1");
				DataManager::SetValue("shrpMagiskUpdate", "0");
			}
		}
	}else{
		LOGINFO("Magisk Version Not Found\n");
		DataManager::SetValue("shrpMagiskRoot", "1");
		DataManager::SetValue("shrpMagiskUnroot", "0");
		DataManager::SetValue("shrpMagiskUpdate", "0");
	}
	if(TWFunc::Path_Exists(core_only_1)||TWFunc::Path_Exists(core_only_2)){
		DataManager::SetValue("core",1);
	}else{
		DataManager::SetValue("core",0);
	}
	return 0;
}

int GUIAction::shrp_magisk_mi(std::string arg __unused){//SHRP Magisk Module Information Gatherer
	char chr[50];
	string name,version,author,module_path,path_1;
	stringstream x;
	DataManager::GetValue("c_magisk_path", module_path);
	DataManager::GetValue("c_magisk_name", path_1);
	module_path=module_path+path_1+"/module.prop";
	if(TWFunc::Path_Exists(module_path)){
		int i=0;
		FILE *f=fopen(module_path.c_str(),"r");
		string tmp;
		while(i<5){
			fgets(chr,50,f);
			tmp=chr;
			tmp=tmp.substr(0,tmp.find_first_of('='));
			if(tmp == "name"){
				name=chr;
			}else if(tmp == "version"){
				version=chr;
			}else if(tmp == "author"){
				author=chr;
			}
			i++;
		}
		fclose(f);

		//Fixes the invalid char issue
		{x<<name;x>>name;}
		{x<<version;x>>version;}
		{x<<author;x>>author;}

		name=name.substr(name.find_first_of('=')+1,name.length());
		version=version.substr(version.find_first_of('=')+1,version.length());
		author=author.substr(author.find_first_of('=')+1,author.length());
	}else{
		name="N/A";
		version="N/A";
		author="N/A";
	}
	DataManager::SetValue("c_mm_name",name);
	DataManager::SetValue("c_mm_ver",version);
	DataManager::SetValue("c_mm_author",author);
	return 0;
}

int GUIAction::shrp_magisk_um(std::string arg __unused){//SHRP Magisk Module Uninstaller

	string modulePath, uninstallShell;
	DataManager::GetValue("modulePath", modulePath);
	uninstallShell = modulePath + "/uninstall.sh";

	if(TWFunc::Path_Exists(uninstallShell)){
		TWFunc::Exec_Cmd("sh "+uninstallShell);
	}
	TWFunc::Exec_Cmd("rm -rf "+modulePath);

	return gui_changeOverlay("");
}

int GUIAction::flashlight(std::string arg __unused){
	LOGINFO("Running GUI function : flashlight\n");
	string cmd,max_b,trigger;
	int temp,switch_tmp;
	temp=switch_tmp=0;
	DataManager::GetValue("c_flashlight_status", trigger);
#ifdef SHRP_CUSTOM_FLASHLIGHT
	LOGINFO("flashlight : Using Custom flashlight path\n");
	DataManager::GetValue("c_flashlight_max_brightness", max_b);
	if(trigger=="0"){
		DataManager::SetValue("c_flashlight_status","1");
		cmd="echo " + max_b + " > " + DataManager::GetStrValue("c_flashlight_path_1");
		TWFunc::Exec_Cmd(cmd);
		if(DataManager::GetStrValue("c_flashlight_path_2").size()>3){
			cmd="echo " + max_b + " > " + DataManager::GetStrValue("c_flashlight_path_2");
			TWFunc::Exec_Cmd(cmd);
		}
		if(DataManager::GetStrValue("c_flashlight_path_3").size()>3){
			cmd="echo 1 > " + DataManager::GetStrValue("c_flashlight_path_3");
			TWFunc::Exec_Cmd(cmd);
		}
	}else{
		DataManager::SetValue("c_flashlight_status","0");
		cmd="echo 0 > " + DataManager::GetStrValue("c_flashlight_path_1");
		TWFunc::Exec_Cmd(cmd);
		if(DataManager::GetStrValue("c_flashlight_path_2").size()>3){
			cmd="echo 0 > " + DataManager::GetStrValue("c_flashlight_path_2");
			TWFunc::Exec_Cmd(cmd);
		}
		if(DataManager::GetStrValue("c_flashlight_path_3").size()>3){
			cmd="echo 0 > " + DataManager::GetStrValue("c_flashlight_path_3");
			TWFunc::Exec_Cmd(cmd);
		}
	}
#else
	LOGINFO("flashlight : Trying to find flashlight path\n");
	if(TWFunc::Path_Exists("/sys/class/leds/")){

		//Searching the path and fetching the max-brightness of flash
		if(TWFunc::Path_Exists("/sys/class/leds/led:torch/")){
			temp=1;
			TWFunc::read_file("/sys/class/leds/led:torch/max_brightness",max_b);
		}else if(TWFunc::Path_Exists("/sys/class/leds/led:torch_0/")){
			temp=2;
			TWFunc::read_file("/sys/class/leds/led:torch_0/max_brightness",max_b);
		}else if(TWFunc::Path_Exists("/sys/class/leds/led:flash/")){
			temp=3;
			TWFunc::read_file("/sys/class/leds/led:flash/max_brightness",max_b);
		}else if(TWFunc::Path_Exists("/sys/class/leds/led:flashlight/")){
			temp=4;
			TWFunc::read_file("/sys/class/leds/led:flashlight/max_brightness",max_b);
		}else if(TWFunc::Path_Exists("/sys/class/leds/led:torch-light/")){
			temp=5;
			TWFunc::read_file("/sys/class/leds/led:torch-light/max_brightness",max_b);
		}else{
			LOGINFO("flashlight : FlashLight Does not support on your device\n");
			return 0;
		}
		//Searching the switch path [if available]
		if(TWFunc::Path_Exists("/sys/class/leds/led:switch/")){
			switch_tmp=1;
		}else if(TWFunc::Path_Exists("/sys/class/leds/led:switch_0/")){
			switch_tmp=2;
		}

		//On/OFF flashlight
		if(trigger=="0"){
			DataManager::SetValue("c_flashlight_status","1");

			//Turning on flash
			if(temp==1){
				cmd="echo " + max_b + " > /sys/class/leds/led:torch/brightness";
				TWFunc::Exec_Cmd(cmd);
			}else if(temp==2){
				cmd="echo " + max_b + " > /sys/class/leds/led:torch_0/brightness";
				TWFunc::Exec_Cmd(cmd);
				if(TWFunc::Path_Exists("/sys/class/leds/led:torch_1/")){
					cmd="echo " + max_b + " > /sys/class/leds/led:torch_1/brightness";
					TWFunc::Exec_Cmd(cmd);
				}
			}else if(temp==3){
				cmd="echo " + max_b + " > /sys/class/leds/led:flash/brightness";
				TWFunc::Exec_Cmd(cmd);
			}else if(temp==4){
				cmd="echo " + max_b + " > /sys/class/leds/led:flashlight/brightness";
				TWFunc::Exec_Cmd(cmd);
			}else if(temp==5){
				cmd="echo " + max_b + " > /sys/class/leds/led:torch-light/brightness";
				TWFunc::Exec_Cmd(cmd);
			}
			//Turning on switch [If available]
			if(switch_tmp==1){
				cmd="echo 1 > /sys/class/leds/led:switch/brightness";
				TWFunc::Exec_Cmd(cmd);
			}else if(switch_tmp==2){
				cmd="echo 1 > /sys/class/leds/led:switch_0/brightness";
				TWFunc::Exec_Cmd(cmd);
			}
		}else{
			DataManager::SetValue("c_flashlight_status","0");

			//Turning OFF flash
			if(temp==1){
				TWFunc::Exec_Cmd("echo 0 > /sys/class/leds/led:torch/brightness");
			}else if(temp==2){
				TWFunc::Exec_Cmd("echo 0 > /sys/class/leds/led:torch_0/brightness");
				if(TWFunc::Path_Exists("/sys/class/leds/led:torch_1/")){
					TWFunc::Exec_Cmd("echo 0 > /sys/class/leds/led:torch_1/brightness");
				}
			}else if(temp==3){
				TWFunc::Exec_Cmd("echo 0 > /sys/class/leds/led:flash/brightness");
			}else if(temp==4){
				TWFunc::Exec_Cmd("echo 0 > /sys/class/leds/led:flashlight/brightness");
			}else if(temp==5){
				TWFunc::Exec_Cmd("echo 0 > /sys/class/leds/led:torch-light/brightness");
			}
			//Turning OFF switch [If available]
			if(switch_tmp==1){
				TWFunc::Exec_Cmd("echo 0 > /sys/class/leds/led:switch/brightness");
			}else if(switch_tmp==2){
				TWFunc::Exec_Cmd("echo 0 > /sys/class/leds/led:switch_0/brightness");
			}
		}
	}else{
		LOGINFO("flashlight : FlashLight does not support on your device\n");
	}
#endif
	return 0;
}

int GUIAction::sig(std::string arg __unused){
	int size,free;
	unsigned long long mb = 1048576;
	string partition;
	TWPartition* ptr;
	vector<storageInfo> storage;
	storageInfo sinfo;
	sinfo.storageLocation="androidSystemPath";
	sinfo.freePercentageVar="c_s_p";
	sinfo.freeStrVar="c_s_status";
	storage.push_back(sinfo);
	sinfo.storageLocation="internal_storage_location";
	sinfo.freePercentageVar="c_i_p";
	sinfo.freeStrVar="c_i_status";
	storage.push_back(sinfo);
	sinfo.storageLocation="external_storage_location";
	sinfo.freePercentageVar="c_e_p";
	sinfo.freeStrVar="c_e_status";
	storage.push_back(sinfo);
	sinfo.storageLocation="usb_otg_location";
	sinfo.freePercentageVar="c_o_p";
	sinfo.freeStrVar="c_o_status";
	storage.push_back(sinfo);

	for(auto it=storage.begin();it<storage.end();it++){
		DataManager::GetValue(it->storageLocation, partition);
		ptr = PartitionManager.Find_Partition_By_Path(partition);
		if(partition == "" || partition == " " || ptr == NULL){
			DataManager::SetValue(it->freeStrVar,"Unavailable");
			DataManager::SetValue(it->freePercentageVar,"0");
		}else{
			size = ptr->Size / mb;
			free = ptr->Free / mb;
			process_space(size,free,*it);
		}
	}
	return 0;
}

int GUIAction::unlock(std::string arg){
	Hasher H;
	if(!H.LockPassInit(arg)){
		//PageManager::ChangePage("c_recBlocked");
		gui_changePage("c_recBlocked");
		return 0;
	}

	if(H.lock_pass!="1" && H.lock_pass!="2"){
		//PageManager::ChangePage("main2");
		gui_changePage("main2");
	}else{
		if(H.isPassCorrect()){
			property_set("shrp.lock", "0");
			PartitionManager.Enable_MTP();
			DataManager::SetValue("main_pass",arg.c_str());
			//PageManager Will Change The Page
			DataManager::SetValue("passNotMatched","0");
			//PageManager::ChangePage("main2");
			gui_changePage("main2");
		}else{
			property_set("shrp.lock", "1");
			LOGINFO("%s: Failed verifying the given password!\n", __func__);
			//PageManager Will Loop The Page
			DataManager::SetValue("passNotMatched","1");
			DataManager::SetValue("password","");
			//H.lock_pass=="1" ? PageManager::ChangePage("c_pass_capture") : PageManager::ChangePage("c_patt_capture");
			H.lock_pass=="1" ? gui_changePage("c_pass_capture") : gui_changePage("c_patt_capture");
		}
	}
	return 0;
}

int GUIAction::set_lock(std::string arg){
	FILE *f;
	string pass;
	if(DataManager::GetStrValue("lockVal1")==DataManager::GetStrValue("lockVal2")){
		DataManager::GetValue("lockVal2", pass);
		f=fopen("twres/slts","w");
		if(f==NULL){
			//Failed To Create File
		}else{
			pass=arg+Hasher::doHash(pass);
			//Write that File
			fputs(pass.c_str(),f);
			fclose(f);
		}
#ifndef SHRP_EXPRESS
#ifdef SHRP_AB
#ifdef TW_HAS_RECOVERY_PARTITION
		TWFunc::Exec_Cmd("sh /twres/scripts/create_envAB_REC.sh;");
#else
		TWFunc::Exec_Cmd("sh /twres/scripts/create_envAB.sh;");
#endif		
#else
		TWFunc::Exec_Cmd("export recoveryBlock="+DataManager::GetStrValue("shrp_rec")+"; sh /twres/scripts/create_env.sh;");
#endif
#endif
		GUIAction::c_repack("lock");
		//PageManager::ChangePage("lockDone");
		gui_changePage("lockDone");
	}else{
		DataManager::SetValue("lockVal1","");
		DataManager::SetValue("lockVal2","");
		DataManager::SetValue("lockVal_notMatched","1");
		//DataManager::GetIntValue("lockType") == 1 ? PageManager::ChangePage("passwordP1") : PageManager::ChangePage("pattP1");
		DataManager::GetIntValue("lockType") == 1 ? gui_changePage("passwordP1") : gui_changePage("pattP1");
	}


	return 0;
}

int GUIAction::reset_lock(std::string arg __unused){
	if(DataManager::GetStrValue("main_pass") == DataManager::GetStrValue("lockVal1")){
		FILE *f;
		f=fopen("twres/slts","w");
		fputs("0",f);
		fclose(f);
#ifndef SHRP_EXPRESS
#ifdef SHRP_AB
#ifdef TW_HAS_RECOVERY_PARTITION
		TWFunc::Exec_Cmd("sh /twres/scripts/create_envAB_REC.sh;");
#else
		TWFunc::Exec_Cmd("sh /twres/scripts/create_envAB.sh;");
#endif		
#else
		TWFunc::Exec_Cmd("export recoveryBlock="+DataManager::GetStrValue("shrp_rec")+"; sh /twres/scripts/create_env.sh;");
#endif
#endif
		GUIAction::c_repack("lock");
		//PageManager::ChangePage("lockDone");
		gui_changePage("lockDone");
	}else{
		DataManager::SetValue("lockVal1","");
		DataManager::SetValue("lockVal2","");
		DataManager::SetValue("lockVal_notMatched","1");
		//DataManager::GetIntValue("lockType") == 1 ? PageManager::ChangePage("passwordReset") : PageManager::ChangePage("pattReset");
		DataManager::GetIntValue("lockType") == 1 ? gui_changePage("passwordReset") : gui_changePage("pattReset");
	}
	return 0;
}

int GUIAction::c_repack(std::string arg __unused){
#ifdef SHRP_EXPRESS
	Express::updateSHRPBasePath();
	Express::shrpResExp("/twres/",DataManager::GetStrValue("shrpBasePath")+"/etc/shrp/");
#else
	string sync = "sync_all.sh;";
	if(TWFunc::Path_Exists("/twres/images/")){
		LOGINFO("c_repack : /twres/images/ found\n");
		sync = (arg == "lock") ? "syncLock.sh;" : (arg == "theme") ? "sync.sh;" : sync;

		if(TWFunc::Exec_Cmd("sh /twres/scripts/" + sync) != 0){
			LOGINFO("c_repack : Syncing failed\n");
		}else{
#ifdef SHRP_DEV_USE_HEX
			TWFunc::Exec_Cmd("sh /twres/scripts/repack_wHEX.sh;");
#else
			TWFunc::Exec_Cmd("sh /twres/scripts/repack.sh;");
#endif	
#ifdef SHRP_AB
#ifdef TW_HAS_RECOVERY_PARTITION
			string recBlock = DataManager::GetStrValue("shrp_rec");
			recBlock = recBlock != "N/A" ? recBlock : "/dev/block/bootdevice/by-name/recovery";

#ifndef SHRP_DEV_FLASH_BOTH_SLOTS
			string current_slot = PartitionManager.Get_Active_Slot_Display() == "A" ? "a" : "b";

			if (TWFunc::Exec_Cmd("dd if=/tmp/work/newRec.img of=" + recBlock + "_" + current_slot) == 0) {
				LOGINFO("c_repack : Recovery successfully pushed into slot %s\n", current_slot.c_str());
			} else {
				LOGERR("c_repack : Error occured while pushing Recovery into slot %s\n", current_slot.c_str());
			}	
#else
			if (TWFunc::Exec_Cmd("dd if=/tmp/work/newRec.img of=" + recBlock + "_a") == 0) {
				LOGINFO("c_repack : Recovery successfully pushed into slot A\n");
			} else {
				LOGERR("c_repack : Error occured while pushing Recovery into slot A\n");
			}

			if (TWFunc::Exec_Cmd("dd if=/tmp/work/newRec.img of=" + recBlock + "_b") == 0) {
				LOGINFO("c_repack : Recovery successfully pushed into slot B\n");
			} else {
				LOGERR("c_repack : Error occured while pushing Recovery into slot B\n");
			}
#endif
#else
#ifndef SHRP_DEV_FLASH_BOTH_SLOTS
			string current_slot = PartitionManager.Get_Active_Slot_Display() == "A" ? "a" : "b";
			TWFunc::Exec_Cmd("dd if=/tmp/work/newRec.img of=/dev/block/bootdevice/by-name/boot_" + current_slot);
			LOGINFO("c_repack : boot_%s pushed to the block\n", current_slot.c_str());
#else
			TWFunc::Exec_Cmd("dd if=/tmp/work/newRec.img of=/dev/block/bootdevice/by-name/boot_a");
			LOGINFO("c_repack : boot_a pushed to the block\n");

			TWFunc::Exec_Cmd("rm -r /tmp/work");
			TWFunc::Exec_Cmd("sh /twres/scripts/nxtPatch.sh;");
			LOGINFO("c_repack : Environment created for boot_b\n");
			TWFunc::Exec_Cmd("sh /twres/scripts/sync.sh;");
			TWFunc::Exec_Cmd("sh /twres/scripts/repack.sh;");
			TWFunc::Exec_Cmd("dd if=/tmp/work/newRec.img of=/dev/block/bootdevice/by-name/boot_b");
			LOGINFO("c_repack : boot_b pushed to the block\n");
#endif
#endif
#else
			LOGINFO("c_repack : Repacking Successful\n");
			DataManager::SetValue("tw_flash_partition","/recovery;");
			DataManager::SetValue("tw_action","flashimage");
			DataManager::SetValue("tw_has_action2","0");
			DataManager::SetValue("tw_zip_location","/tmp/work");
			DataManager::SetValue("tw_file","newRec.img");
			GUIAction::flashimage("dummy");
			LOGINFO("c_repack : Flashing modified Recovery done\n");
#endif
			TWFunc::Exec_Cmd("rm -r /tmp/work");
		}
	}
#endif
	if(DataManager::GetIntValue("c_devMode")){
		DataManager::SetValue("tw_include_kernel_log", "1");
		GUIAction::copylog("Dummy");
	}
	return 0;
}

int GUIAction::flashOP(std::string arg){
	int s=0;
	arg=minUtils::getExtension(arg);
	DataManager::GetValue("c_queue_enabled",s);


#ifdef SHRP_OZIP_DECRYPT
	if(minUtils::compare(arg, ".zip") || minUtils::compare(arg, ".apk") || minUtils::compare(arg, ".ozip")){
#else
	if(minUtils::compare(arg, ".zip") || minUtils::compare(arg, ".apk")){
#endif
		GUIAction::queuezip("bappa");
		DataManager::SetValue("c_queue_enabled","1");
		//PageManager::ChangePage("flash_confirm");
		gui_changePage("flash_confirm");

	}else if(minUtils::compare(arg,".img") && s == 1){
		//PageManager::ChangePage("flash_confirm");
		gui_changePage("flash_confirm");

	}else if(minUtils::compare(arg,".img") && s == 0){
		//PageManager::ChangePage("flashimage_confirm");
		gui_changePage("flashimage_confirm");
	}
	return 0;
}

int GUIAction::shrp_zip_init(std::string arg){
	arg=arg.substr(arg.find_last_of("/")+1,arg.find_last_of(".")-(arg.find_last_of("/")+1));
	DataManager::SetValue("shrp_zipName",arg.c_str());
	DataManager::SetValue("shrp_zipFolderName",arg.c_str());
	return 0;
}

int GUIAction::clearInput(std::string arg){//<action function="clearInput">valueVariable;currentPage</action>
	vector<string> x = TWFunc::Split_String(arg,";");
	DataManager::SetValue(x.at(0),"");
	//PageManager::ChangePage(x.at(1));
	gui_changePage(x.at(1));
	return 0;
}

int GUIAction::getFileInfo(std::string arg){
	bool isFile = FileManager::isFile(arg);
	string objName = isFile ? FileManager::getFileName(arg) : FileManager::getFolderName(arg);
	string extn = isFile ? minUtils::getExtension(objName) : "";
	bool isTextFile = isFile ? minUtils::isFileEditable(extn) : false;
	bool isTheme = isFile ? minUtils::compare(extn,".stheme") : false;
	bool isZip = isFile ? minUtils::compare(extn,".zip") : false;

	DataManager::SetValue("isTextFile", isTextFile ? 1 : 0);
	DataManager::SetValue("isTheme", isTheme ? 1 : 0);
	DataManager::SetValue("isZip", isZip ? 1 : 0);
	DataManager::SetValue("archiveName", isZip ? objName.substr(0, objName.length() - extn.length()) : "");

	DataManager::SetValue("permText", FileManager::getStrPermission(arg));
	FileManager::UpdateGuiPerms(arg);
	DataManager::SetValue("permChanged", 0);
	DataManager::SetValue("objectSize", FileManager::getSizeStr(arg));
	DataManager::SetValue("newRenameName", objName);
	DataManager::SetValue("isFile", isFile ? 1 : 0);
	return 0;
}

int GUIAction::themeInit(std::string arg __unused){
	bool err=false;
	ThemeManager::initialVarProcess();

	if (TWFunc::Exec_Cmd("cp -r /twres /tmp/bak/;") != 0) {err = true;}
#ifndef SHRP_EXPRESS
#ifdef SHRP_AB
#ifdef TW_HAS_RECOVERY_PARTITION
	if (TWFunc::Exec_Cmd("sh /twres/scripts/create_envAB_REC.sh;") != 0) {
		err=true;
	}
#else
	if (TWFunc::Exec_Cmd("sh /twres/scripts/create_envAB.sh;") != 0) {
		err=true;
	}
#endif
#else
	if(TWFunc::Exec_Cmd("export recoveryBlock="+DataManager::GetStrValue("shrp_rec")+";sh /twres/scripts/create_env.sh;")!=0){
		err=true;
	}
#endif
#endif

	if(err){
		if(TWFunc::Path_Exists("/tmp/bak")){TWFunc::Exec_Cmd("rm -rf /tmp/bak");}
		if(TWFunc::Path_Exists("/tmp/work")){TWFunc::Exec_Cmd("rm -rf /tmp/work");}
		DataManager::SetValue("themeProcessErr",1);
	}else{
		DataManager::SetValue("themeProcessErr",0);
	}
	return 0;
}

int GUIAction::applyDefaultTheme(std::string arg __unused){
	bool err=false;

	ThemeManager TM;
	err=TM.applyThemeResouces() == true ? false : true;
	if(!err){
		err=TM.syncDyanmicVar() == true ? false : true;
	}
	if(!err){
		GUIAction::c_repack("theme");
		if(TWFunc::Path_Exists("/tmp/bak")){TWFunc::Exec_Cmd("rm -rf /tmp/bak");}
		if(TWFunc::Path_Exists("/tmp/work")){TWFunc::Exec_Cmd("rm -rf /tmp/work");}
		GUIAction::customReload("main2");
	}else{
		if(TWFunc::Path_Exists("/tmp/bak")){TWFunc::Exec_Cmd("cp -r /tmp/bak/ /twres/");}
		if(TWFunc::Path_Exists("/tmp/bak")){TWFunc::Exec_Cmd("rm -rf /tmp/bak");}
		if(TWFunc::Path_Exists("/tmp/work")){TWFunc::Exec_Cmd("rm -rf /tmp/work");}
		DataManager::SetValue("themeProcessErr",1);
	}

	return 0;
}

int GUIAction::applyCustomTheme(std::string arg){
	ThemeManager TM;
	bool err;
	if(TM.initCustomTheme(arg)){
		LOGINFO("SHRP CUSTOM THEME Initialization completed\n");
	}else{
		TM.removeTempData();
		LOGINFO("SHRP CUSTOM THEME Initialization failed. Exiting...\n");
		return 0;
	}
	//Creating env for theme patching
	GUIAction::themeInit("dummy");

	//Applying custom theme
	err=TM.applyCustomTheme() == true ? false : true;

	if(!err){
		GUIAction::c_repack("dummy");
		GUIAction::customReload("main2");
	}else{
		if(TWFunc::Path_Exists("/tmp/bak")){TWFunc::Exec_Cmd("cp -r /tmp/bak/ /twres/");}
		if(TWFunc::Path_Exists("/tmp/bak")){TWFunc::Exec_Cmd("rm -rf /tmp/bak");}
		if(TWFunc::Path_Exists("/tmp/work")){TWFunc::Exec_Cmd("rm -rf /tmp/work");}
		DataManager::SetValue("themeProcessErr",1);
	}

	return 0;
}

int GUIAction::SetColor(std::string arg){
	DataManager::SetValue(DataManager::GetStrValue("assignVar"),arg.c_str());
	DataManager::SetValue("colorHolder",arg.c_str());
	DataManager::SetValue("dummy",1);
	return 0;
}


int GUIAction::fileManagerOp(std::string arg __unused){
	bool result = false;

	string from = DataManager::GetStrValue("tw_file_location1");
	string to = DataManager::GetStrValue("tw_file_location2");

	string filePath = DataManager::GetStrValue("tw_filename1");
	bool fileSelected = filePath.length()>2 ? true : false;

	int multiple = DataManager::GetIntValue("selectEnabled");
	string objects = multiple ? DataManager::GetStrValue("mSelectedPathList") : "";

	from = multiple ? objects : fileSelected ? filePath : from;

	bool overwrite = DataManager::GetIntValue("overwriteExisting") ? true : false;

	bool textEditorOp = arg == "replaceLine" || arg == "addLine" || arg == "removeLine" ? true : false;
	long long int lineNo = textEditorOp ? DataManager::GetIntValue("selectedLineNo") : 0;
	string selectedLine = textEditorOp ? DataManager::GetStrValue("selectedLine") : "";
	TextTool T;
	if(textEditorOp) T.getFileData(filePath);

	//LOGINFO("Paths - %s\n",DataManager::GetStrValue("mSelectedPathList").c_str());

	/*
	arg = 	
		remove 
		rename
		move 
		copy 
		setPermGui 
		setPermText
		createFolder
		zipFolder
		unzipFile
		applyCustomTheme
		ImgUnpack [Not Sure]
		ImgRepack [Not Sure]
		Ramdisk	[Not Sure]
	*/


	if(arg == "copy"){
		DataManager::SetValue("fActionName", gui_parse_text("{@fm_copying=Copying}"));
		result = FileManager::copy(from, to, multiple, overwrite);
	}else if(arg == "move"){
		DataManager::SetValue("fActionName", gui_parse_text("{@fm_moving=Moving}"));
		result = FileManager::move(from, to, multiple, overwrite);
	}else if(arg == "remove"){
		DataManager::SetValue("fActionName", gui_parse_text("{@fm_deleting=Deleting}"));
		result = FileManager::remove(from, multiple);
	}else if(arg == "rename"){
		DataManager::SetValue("fActionName", gui_parse_text("{@fm_renaming=Renaming}"));
		string newFileName = DataManager::GetStrValue("newRenameName");
		result = FileManager::rename(from, newFileName, overwrite);
	}else if(arg == "setPermText"){
		result = TWFunc::Exec_Cmd(
			FileManager::setPermission(
				from, DataManager::GetStrValue("newChmod")
			)
		) == 0 ? true : false;
	}else if(arg == "setPermGui"){
		result = TWFunc::Exec_Cmd(
			FileManager::setPermission(
				from, 
				DataManager::GetIntValue("NewpermOwnerR"),
				DataManager::GetIntValue("NewpermOwnerW"),
				DataManager::GetIntValue("NewpermOwnerX"),

				DataManager::GetIntValue("NewpermGroupR"),
				DataManager::GetIntValue("NewpermGroupW"),
				DataManager::GetIntValue("NewpermGroupX"),

				DataManager::GetIntValue("NewpermGlobalR"),
				DataManager::GetIntValue("NewpermGlobalW"),
				DataManager::GetIntValue("NewpermGlobalX")
			)
		) == 0 ? true : false;


	}else if(arg == "createFolder"){
		if(from.find_last_of('/') != from.length()-1){
			from += "/";
		}
		from += DataManager::GetStrValue("newFolderName") + ";";
		result = TWFunc::Exec_Cmd("mkdir -p " + from) == 0 ? 1 : 0;


	}else if(arg == "compress"){
		DataManager::SetValue("fActionName", gui_parse_text("{@fCompressing=Compressing}"));
		string zipName = DataManager::GetStrValue("archiveName") + ".zip";
		result = FileManager::compress(from, zipName, DataManager::GetIntValue("useHighCompression") ? "9" : "0");

	}else if(arg == "compressByLocation"){
		DataManager::SetValue("fActionName", gui_parse_text("{@fCompressing=Compressing}"));
		string zipName = DataManager::GetStrValue("archiveName") + ".zip";
		result = FileManager::compressEx(from, zipName, to, DataManager::GetIntValue("useHighCompression") ? "9" : "0");

	}else if(arg == "extract"){
		DataManager::SetValue("fActionName", gui_parse_text("{@fUpack=Unpacking}"));
		result = FileManager::extract(filePath);


	}else if(arg == "extractByLocation"){
		DataManager::SetValue("fActionName", gui_parse_text("{@fUpack=Unpacking}"));
		result = FileManager::extract(filePath, to);


	}else if(arg == "applyCustomTheme"){
		DataManager::SetValue("fActionName", gui_parse_text("{@fApplyCustomTheme=Applying custom theme}"));
		ThemeManager TM;
		if(TM.initCustomTheme(filePath)){
			LOGINFO("SHRP CUSTOM THEME Initialization completed\n");
			result = true;
		}else{
			TM.removeTempData();
			LOGINFO("SHRP CUSTOM THEME Initialization failed. Exiting...\n");
			result = false;
		}
		//Creating env for theme patching
		if(result) GUIAction::themeInit("dummy");

		//Applying custom theme
		if(result){
			result = TM.applyCustomTheme();
		}

		if(result){
			GUIAction::c_repack("dummy");
		}else{
			if(TWFunc::Path_Exists("/tmp/bak")){TWFunc::Exec_Cmd("cp -r /tmp/bak/ /twres/");}
		}
		if(TWFunc::Path_Exists("/tmp/bak")){TWFunc::Exec_Cmd("rm -rf /tmp/bak");}
		if(TWFunc::Path_Exists("/tmp/work")){TWFunc::Exec_Cmd("rm -rf /tmp/work");}

		if(result) GUIAction::reload("dummy");
	}else if(arg == "md5" || arg == "sha1" || arg == "sha256"){
		DataManager::SetValue("fActionName", gui_parse_text("{@check_for_digest=Checking for Digest file...}"));
		string tmp = filePath;

		if(filePath.find_last_of('.') != string::npos){
			filePath = filePath.substr(0, filePath.find_last_of('.'));
		}

		if(TWFunc::Path_Exists(filePath + "." + arg)){
			string hashValue = FileManager::generate_Hash(tmp, arg);
			T.getFileData(filePath + "." + arg);
			result = minUtils::compare(hashValue, T.fileData[0]);
		}

	}else if(arg == "replaceLine"){

		T.replaceLine(lineNo - 1, selectedLine);
		result = T.pushString(filePath);
		DataManager::SetValue("refresh", 1);


	}else if(arg == "addLine"){

		T.addLine(lineNo - 1, selectedLine);
		result = T.pushString(filePath);
		DataManager::SetValue("refresh", 1);


	}else if(arg == "removeLine"){

		T.removeLine(lineNo - 1);
		result = T.pushString(filePath);
		DataManager::SetValue("refresh", 1);


	}

	DataManager::SetValue("fAction", result ? 1 : -1);


	return 0;
}

int GUIAction::fTools(std::string arg __unused){//Just a GUI Interface of FileSystem Tools
	vector<string> args = TWFunc::Split_String(arg, ";");
	if(args[0] == "getFolderName"){
		//Expecting Two More args
		// <action function="fTools">getFolderName;%PathName%;%storeValueVariableName%</action>
		DataManager::SetValue(args[2].c_str(), FileManager::getFolderName(args[1]));
	}else if(args[0] == "getFileName"){
		//Expecting Two More args
		// <action function="fTools">getFolderName;%PathName%;%storeValueVariableName%</action>
		DataManager::SetValue(args[2].c_str(), FileManager::getFileName(args[1]));
	}else if(args[0] == "getObjectSize"){
		//Expecting Two More args
		// <action function="fTools">getFolderName;%PathName%;%storeValueVariableName%</action>
		DataManager::SetValue(args[2].c_str(), FileManager::getSizeStr(args[1]));
	}else if(args[0] == "refreshPermission"){
		DataManager::SetValue(args[2].c_str(), FileManager::getStrPermission(args[1]));
	}else if(args[0] == "getGuiPermStatus"){
		DataManager::SetValue(args[1].c_str(),(
		DataManager::GetIntValue("permOwnerR") == DataManager::GetIntValue("NewpermOwnerR") &&
		DataManager::GetIntValue("permOwnerW") == DataManager::GetIntValue("NewpermOwnerW") &&
		DataManager::GetIntValue("permOwnerX") == DataManager::GetIntValue("NewpermOwnerX") &&
		DataManager::GetIntValue("permGroupR") == DataManager::GetIntValue("NewpermGroupR") &&
		DataManager::GetIntValue("permGroupW") == DataManager::GetIntValue("NewpermGroupW") &&
		DataManager::GetIntValue("permGroupX") == DataManager::GetIntValue("NewpermGroupX") &&
		DataManager::GetIntValue("permGlobalR") == DataManager::GetIntValue("NewpermGlobalR") &&
		DataManager::GetIntValue("permGlobalW") == DataManager::GetIntValue("NewpermGlobalW") &&
		DataManager::GetIntValue("permGlobalX") == DataManager::GetIntValue("NewpermGlobalX")) ? 0 : 1);

		if(DataManager::GetIntValue(args[1].c_str())){
			Perm P;
			int fPermission = P.calculatePerm(DataManager::GetIntValue("NewpermOwnerR"),
			DataManager::GetIntValue("NewpermOwnerW"),
			DataManager::GetIntValue("NewpermOwnerX"),
			DataManager::GetIntValue("NewpermGroupR"),
			DataManager::GetIntValue("NewpermGroupW"),
			DataManager::GetIntValue("NewpermGroupX"),
			DataManager::GetIntValue("NewpermGlobalR"),
			DataManager::GetIntValue("NewpermGlobalW"),
			DataManager::GetIntValue("NewpermGlobalX"));
			DataManager::SetValue(args[2].c_str(), fPermission);
		}
	}else if(args[0] == "getTextPermStatus"){
		FileManager::UpdateGuiPerms(args[1]);
	}else if(args[0] == "getTextFileLine"){
		if(DataManager::GetStrValue("fManagerAction") == "addLine"){ // If User wants to add line into the textFile it will skip the lineFetching Process [Execption]
			DataManager::SetValue(args[2].c_str(), "");
			return 0;
		}

		TextTool T;
		T.getFileData(args[1]);
		int lineNo = DataManager::GetIntValue("selectedLineNo") - 1;
		DataManager::SetValue(args[2].c_str(), T.getLine(lineNo));
	}else if(args[0] == "getTextFileMaxLine"){
		TextTool T;
		T.getFileData(args[1]);
		unsigned long long int lineNo = T.fileData.size();
		DataManager::SetValue(args[2].c_str(), lineNo);
	}else if(args[0] == "checksumMD5"){


	}else if(args[0] == "setDestination"){
		string dest = args[1] == "fileM1" ? "tw_file_location1" : "tw_file_location2";

		//Checking if system is selected from the sidebar
		if(minUtils::find(args[2], "system")){
			bool isMounted = PartitionManager.Is_Mounted_By_Path(args[2]);
			bool readOnly = DataManager::GetIntValue("tw_mount_system_ro") == 0 ? false : true;
			if(readOnly && !isMounted){
				PartitionManager.Mount_By_Path(args[2], true);
			}else if(!readOnly && !isMounted){
				minUtils::remountSystem(true);
			}
			args[2] = PartitionManager.Get_Android_Root_Path();
		}
		GUIAction::set(dest + "=" + args[2]);
	}

	return 0;

}

int GUIAction::revDir(string arg){
	string path = DataManager::GetStrValue(arg);
	if(DataManager::GetIntValue("selectEnabled") && arg == "tw_file_location1"){
		DataManager::SetValue("selectEnabled", 0);
		return 0;
	}
	if(path == "/"){
		//PageManager::ChangePage("c_refresh");
		gui_changePage("c_refresh");
	}else if(path.find_first_of('/') == string::npos){
		DataManager::SetValue(arg, "/");
	}else{
		DataManager::SetValue(arg, path.substr(0, path.find_last_of('/')));
	}

	return 0;
}

int GUIAction::flashBridge(string arg){
	gui_changeOverlay("");
	return flash(arg);
}
