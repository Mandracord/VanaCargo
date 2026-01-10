#include "pch.h"
#include "VanaCargoBridge.h"
#include "CoreApi.h"
#include <vcclr.h>

using namespace VanaCargoBridge;
using msclr::interop::marshal_as;

static std::wstring ToWString(String^ value)
{
	if (value == nullptr)
		return std::wstring();

	return marshal_as<std::wstring>(value);
}

static std::wstring GetExeDir()
{
	wchar_t path[MAX_PATH] = {};
	DWORD len = GetModuleFileNameW(NULL, path, MAX_PATH);
	if (len == 0)
		return L".";

	std::wstring full(path, len);
	size_t pos = full.find_last_of(L"\\/");
	if (pos == std::wstring::npos)
		return L".";

	return full.substr(0, pos);
}

CoreBridge::CoreBridge()
{
}

String^ CoreBridge::Ping()
{
	return "VanaCargoCore ready";
}

LoadResult^ CoreBridge::LoadConfigAndCharacters(String^ configPath)
{
	CoreApi api;
	CoreSettings settings;
	std::vector<InventoryTabInfo> tabs;
	std::vector<CharacterInfo> characters;

	bool ok = api.LoadConfig(ToWString(configPath), settings, tabs, characters);
	if (!ok)
		return nullptr;

	LoadResult^ result = gcnew LoadResult();
	ManagedSettings^ managedSettings = gcnew ManagedSettings();
	managedSettings->Region = settings.Region;
	managedSettings->Language = settings.Language;
	managedSettings->CompactList = settings.CompactList;
	managedSettings->FfxiPath = gcnew String(settings.FfxiPath.c_str());
	managedSettings->FindAllEnabled = settings.FindAllEnabled;
	managedSettings->FindAllDataPath = gcnew String(settings.FindAllDataPath.c_str());
	managedSettings->FindAllKeyItemsPath = gcnew String(settings.FindAllKeyItemsPath.c_str());
	result->Settings = managedSettings;

	array<ManagedTabInfo^>^ tabArray = gcnew array<ManagedTabInfo^>((int)tabs.size());
	for (int i = 0; i < (int)tabs.size(); ++i)
	{
		ManagedTabInfo^ tab = gcnew ManagedTabInfo();
		tab->FileName = gcnew String(tabs[i].FileName.c_str());
		tab->DisplayName = gcnew String(tabs[i].DisplayName.c_str());
		tabArray[i] = tab;
	}
	result->Tabs = tabArray;

	array<ManagedCharacter^>^ charArray = gcnew array<ManagedCharacter^>((int)characters.size());
	for (int i = 0; i < (int)characters.size(); ++i)
	{
		ManagedCharacter^ ch = gcnew ManagedCharacter();
		ch->Id = gcnew String(characters[i].Id.c_str());
		ch->Name = gcnew String(characters[i].Name.c_str());
		charArray[i] = ch;
	}
	result->Characters = charArray;

	return result;
}

bool CoreBridge::SaveSettings(String^ configPath, ManagedSettings^ settings)
{
	if (settings == nullptr)
		return false;

	CoreSettings nativeSettings;
	nativeSettings.Region = settings->Region;
	nativeSettings.Language = settings->Language;
	nativeSettings.CompactList = settings->CompactList;
	nativeSettings.FfxiPath = ToWString(settings->FfxiPath);
	nativeSettings.FindAllEnabled = settings->FindAllEnabled;
	nativeSettings.FindAllDataPath = ToWString(settings->FindAllDataPath);
	nativeSettings.FindAllKeyItemsPath = ToWString(settings->FindAllKeyItemsPath);

	CoreApi api;
	return api.SaveSettings(ToWString(configPath), nativeSettings);
}

bool CoreBridge::SaveCharacterDisplayNames(String^ configPath, array<ManagedCharacter^>^ characters)
{
	std::vector<std::pair<std::wstring, std::wstring>> entries;
	if (characters != nullptr)
	{
		entries.reserve(characters->Length);
		for (int i = 0; i < characters->Length; ++i)
		{
			ManagedCharacter^ character = characters[i];
			if (character == nullptr)
				continue;

			entries.push_back(std::make_pair(ToWString(character->Id), ToWString(character->Name)));
		}
	}

	CoreApi api;
	return api.SaveCharacterDisplayNames(ToWString(configPath), entries);
}

array<ManagedTab^>^ CoreBridge::LoadInventoryForCharacter(
	ManagedSettings^ settings,
	ManagedCharacter^ character,
	array<ManagedTabInfo^>^ tabs)
{
	if (settings == nullptr || character == nullptr || tabs == nullptr)
		return nullptr;

	CoreSettings nativeSettings;
	nativeSettings.Region = settings->Region;
	nativeSettings.Language = settings->Language;
	nativeSettings.CompactList = settings->CompactList;
	nativeSettings.FfxiPath = ToWString(settings->FfxiPath);
	nativeSettings.FindAllEnabled = settings->FindAllEnabled;
	nativeSettings.FindAllDataPath = ToWString(settings->FindAllDataPath);
	nativeSettings.FindAllKeyItemsPath = ToWString(settings->FindAllKeyItemsPath);

	CharacterInfo nativeChar;
	nativeChar.Id = ToWString(character->Id);
	nativeChar.Name = ToWString(character->Name);

	std::vector<InventoryTabInfo> nativeTabs;
	nativeTabs.reserve(tabs->Length);
	for (int i = 0; i < tabs->Length; ++i)
	{
		ManagedTabInfo^ tab = tabs[i];
		if (tab == nullptr)
			continue;

		InventoryTabInfo info;
		info.FileName = ToWString(tab->FileName);
		info.DisplayName = ToWString(tab->DisplayName);
		nativeTabs.push_back(info);
	}

	std::vector<InventoryTab> nativeOut;
	CoreApi api;
	if (!api.LoadInventoryForCharacter(nativeSettings, nativeChar, nativeTabs, nativeOut))
		return nullptr;

	array<ManagedTab^>^ managedTabs = gcnew array<ManagedTab^>((int)nativeOut.size());
	for (int i = 0; i < (int)nativeOut.size(); ++i)
	{
		ManagedTab^ managedTab = gcnew ManagedTab();
		ManagedTabInfo^ tabInfo = gcnew ManagedTabInfo();
		tabInfo->FileName = gcnew String(nativeOut[i].Info.FileName.c_str());
		tabInfo->DisplayName = gcnew String(nativeOut[i].Info.DisplayName.c_str());
		managedTab->Info = tabInfo;

		array<ManagedItem^>^ items = gcnew array<ManagedItem^>((int)nativeOut[i].Items.size());
		for (int j = 0; j < (int)nativeOut[i].Items.size(); ++j)
		{
			const CoreItem& src = nativeOut[i].Items[j];
			ManagedItem^ item = gcnew ManagedItem();
			item->Id = src.Id;
			item->Count = src.Count;
			item->Name = gcnew String(src.Name.c_str());
			item->Attr = gcnew String(src.Attr.c_str());
			item->Description = gcnew String(src.Description.c_str());
			item->Slot = gcnew String(src.Slot.c_str());
			item->Races = gcnew String(src.Races.c_str());
			item->Level = gcnew String(src.Level.c_str());
			item->Jobs = gcnew String(src.Jobs.c_str());
			item->Remarks = gcnew String(src.Remarks.c_str());
			item->IconWidth = src.IconWidth;
			item->IconHeight = src.IconHeight;
			item->IconStride = src.IconStride;

			if (!src.IconPixels.empty())
			{
				array<Byte>^ pixels = gcnew array<Byte>((int)src.IconPixels.size());
				if (pixels->Length > 0)
				{
					pin_ptr<Byte> pinned = &pixels[0];
					memcpy(pinned, src.IconPixels.data(), src.IconPixels.size());
				}
				item->IconPixels = pixels;
			}

			items[j] = item;
		}

		managedTab->Items = items;
		managedTabs[i] = managedTab;
	}

	return managedTabs;
}
