#pragma once

#include <string>
#include <vector>

struct CoreSettings
{
	int Region;
	int Language;
	bool CompactList;
	std::wstring FfxiPath;
	bool FindAllEnabled;
	std::wstring FindAllDataPath;
	std::wstring FindAllKeyItemsPath;
};

struct InventoryTabInfo
{
	std::wstring FileName;
	std::wstring DisplayName;
};

struct CharacterInfo
{
	std::wstring Id;
	std::wstring Name;
};

struct CoreItem
{
	int Id;
	int Count;
	std::wstring Name;
	std::wstring Attr;
	std::wstring Description;
	std::wstring Slot;
	std::wstring Races;
	std::wstring Level;
	std::wstring Jobs;
	std::wstring Remarks;
	int IconWidth;
	int IconHeight;
	int IconStride;
	std::vector<unsigned char> IconPixels;
};

struct InventoryTab
{
	InventoryTabInfo Info;
	std::vector<CoreItem> Items;
};

class CoreApi
{
public:
	bool LoadConfig(const std::wstring &configPath,
		CoreSettings &settings,
		std::vector<InventoryTabInfo> &tabs,
		std::vector<CharacterInfo> &characters);

	bool LoadInventoryForCharacter(const CoreSettings &settings,
		const CharacterInfo &character,
		const std::vector<InventoryTabInfo> &tabs,
		std::vector<InventoryTab> &outTabs);

	bool SaveSettings(const std::wstring &configPath, const CoreSettings &settings);
	bool SaveCharacterDisplayNames(const std::wstring &configPath,
		const std::vector<std::pair<std::wstring, std::wstring>> &entries);
};
