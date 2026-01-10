#include "stdafx.h"
#include "CoreApi.h"

#include "DefaultConfig.h"
#include "FFXIHelper.h"
#include "SimpleIni.h"
#include <unordered_map>

#ifdef GDIPLUS_IMAGE_RESIZING
#include <gdiplus.h>
#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")
#endif

static CStringW ToCString(const std::wstring &value)
{
	return CStringW(value.c_str());
}

static std::wstring ToWString(const CString &value)
{
	return std::wstring(value.GetString());
}

#ifdef GDIPLUS_IMAGE_RESIZING
struct GdiplusScope
{
	ULONG_PTR Token = 0;

	GdiplusScope()
	{
		Gdiplus::GdiplusStartupInput input;
		if (Gdiplus::GdiplusStartup(&Token, &input, NULL) != Gdiplus::Ok)
			Token = 0;
	}

	~GdiplusScope()
	{
		if (Token != 0)
			Gdiplus::GdiplusShutdown(Token);
	}
};

static GdiplusScope g_gdiplusScope;

static bool BuildLegacyIconPixels(const FFXiIconInfo &iconInfo, CoreItem &item)
{
	if (g_gdiplusScope.Token == 0)
		return false;

	BITMAPINFO *bmpInfo = (BITMAPINFO*)&iconInfo.ImageInfo;
	int width = bmpInfo->bmiHeader.biWidth;
	int height = bmpInfo->bmiHeader.biHeight;
	if (width <= 0)
		width = 16;
	if (height == 0)
		height = 16;

	int absHeight = height < 0 ? -height : height;
	HDC hDC = ::GetDC(NULL);
	if (hDC == NULL)
		return false;

	void *pDst = NULL;
	HBITMAP hBitmap = ::CreateDIBSection(hDC, bmpInfo, DIB_RGB_COLORS, &pDst, NULL, 0);
	if (hBitmap == NULL || pDst == NULL)
	{
		if (hBitmap != NULL)
			::DeleteObject(hBitmap);
		::ReleaseDC(NULL, hDC);
		return false;
	}

	memcpy_s(pDst, 1024, &iconInfo.ImageInfo.ImageData, 1024);

	HIMAGELIST imageList = ::ImageList_Create(width, absHeight, ILC_COLOR32 | ILC_MASK, 1, 1);
	if (imageList == NULL)
	{
		::DeleteObject(hBitmap);
		::ReleaseDC(NULL, hDC);
		return false;
	}

	::ImageList_AddMasked(imageList, hBitmap, RGB(0, 0, 0));
	HICON hIcon = ::ImageList_GetIcon(imageList, 0, ILD_TRANSPARENT);

	bool ok = false;
	if (hIcon != NULL)
	{
		Gdiplus::Bitmap *bitmap = Gdiplus::Bitmap::FromHICON(hIcon);
		if (bitmap != NULL)
		{
			const UINT bmpWidth = bitmap->GetWidth();
			const UINT bmpHeight = bitmap->GetHeight();
			Gdiplus::Rect rect(0, 0, (INT)bmpWidth, (INT)bmpHeight);
			Gdiplus::BitmapData data{};

			if (bitmap->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) == Gdiplus::Ok)
			{
				item.IconWidth = (int)bmpWidth;
				item.IconHeight = (int)bmpHeight;
				item.IconStride = item.IconWidth * 4;
				item.IconPixels.clear();
				item.IconPixels.resize((size_t)item.IconStride * item.IconHeight);

				int srcStride = data.Stride;
				unsigned char *srcBase = (unsigned char*)data.Scan0;
				if (srcStride < 0)
				{
					srcBase = srcBase + (size_t)(item.IconHeight - 1) * (size_t)(-srcStride);
					srcStride = -srcStride;
				}

				for (int y = 0; y < item.IconHeight; ++y)
				{
					unsigned char *srcRow = srcBase + (size_t)y * (size_t)srcStride;
					unsigned char *dstRow = item.IconPixels.data() + (size_t)y * (size_t)item.IconStride;
					memcpy(dstRow, srcRow, (size_t)item.IconStride);
				}

				ok = true;
				bitmap->UnlockBits(&data);
			}

			delete bitmap;
		}

		::DestroyIcon(hIcon);
	}

	::ImageList_Destroy(imageList);
	::DeleteObject(hBitmap);
	::ReleaseDC(NULL, hDC);
	return ok;
}
#endif

static void EnsureDefaultConfig(CSimpleIni &ini)
{
	if (ini.GetValue(INI_FILE_CONFIG_SECTION, INI_FILE_GAME_REGION_KEY) == NULL)
		ini.SetLongValue(INI_FILE_CONFIG_SECTION, INI_FILE_GAME_REGION_KEY, INI_FILE_GAME_REGION_VALUE);

	if (ini.GetValue(INI_FILE_CONFIG_SECTION, INI_FILE_LANGUAGE_KEY) == NULL)
		ini.SetLongValue(INI_FILE_CONFIG_SECTION, INI_FILE_LANGUAGE_KEY, INI_FILE_LANGUAGE_VALUE);

	if (ini.GetValue(INI_FILE_CONFIG_SECTION, INI_FILE_COMPACT_LIST_KEY) == NULL)
		ini.SetLongValue(INI_FILE_CONFIG_SECTION, INI_FILE_COMPACT_LIST_KEY, INI_FILE_COMPACT_LIST_VALUE ? 1L : 0L);

	if (ini.GetValue(INI_FILE_CONFIG_SECTION, INI_FILE_FFXIAH_SERVER_KEY) == NULL)
		ini.SetValue(INI_FILE_CONFIG_SECTION, INI_FILE_FFXIAH_SERVER_KEY, INI_FILE_FFXIAH_SERVER_VALUE);

	if (ini.GetValue(INI_FILE_CONFIG_SECTION, INI_FILE_FFXIAH_CACHE_TTL_KEY) == NULL)
		ini.SetLongValue(INI_FILE_CONFIG_SECTION, INI_FILE_FFXIAH_CACHE_TTL_KEY, INI_FILE_FFXIAH_CACHE_TTL_VALUE ? 1L : 0L);

	if (ini.GetValue(INI_FILE_CONFIG_SECTION, INI_FILE_FINDALL_ENABLED_KEY) == NULL)
		ini.SetLongValue(INI_FILE_CONFIG_SECTION, INI_FILE_FINDALL_ENABLED_KEY, INI_FILE_FINDALL_ENABLED_VALUE ? 1L : 0L);

	if (ini.GetValue(INI_FILE_CONFIG_SECTION, INI_FILE_FINDALL_DATA_PATH_KEY) == NULL)
		ini.SetValue(INI_FILE_CONFIG_SECTION, INI_FILE_FINDALL_DATA_PATH_KEY, INI_FILE_FINDALL_DATA_PATH_VALUE);

	if (ini.GetValue(INI_FILE_CONFIG_SECTION, INI_FILE_FINDALL_KEYITEMS_PATH_KEY) == NULL)
		ini.SetValue(INI_FILE_CONFIG_SECTION, INI_FILE_FINDALL_KEYITEMS_PATH_KEY, INI_FILE_FINDALL_KEYITEMS_PATH_VALUE);
}

static void EnsureInventoryDefaults(CSimpleIni &ini)
{
	if (ini.GetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_INVENTORY_KEY) == NULL)
		ini.SetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_INVENTORY_KEY, INI_FILE_INVENTORY_VALUE);
	if (ini.GetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_SAFE_KEY) == NULL)
		ini.SetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_SAFE_KEY, INI_FILE_MOG_SAFE_VALUE);
	if (ini.GetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_SAFE_2_KEY) == NULL)
		ini.SetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_SAFE_2_KEY, INI_FILE_MOG_SAFE_2_VALUE);
	if (ini.GetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_STORAGE_KEY) == NULL)
		ini.SetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_STORAGE_KEY, INI_FILE_STORAGE_VALUE);
	if (ini.GetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_LOCKER_KEY) == NULL)
		ini.SetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_LOCKER_KEY, INI_FILE_MOG_LOCKER_VALUE);
	if (ini.GetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_SATCHEL_KEY) == NULL)
		ini.SetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_SATCHEL_KEY, INI_FILE_MOG_SATCHEL_VALUE);
	if (ini.GetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_SACK_KEY) == NULL)
		ini.SetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_SACK_KEY, INI_FILE_MOG_SACK_VALUE);
	if (ini.GetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_CASE_KEY) == NULL)
		ini.SetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_CASE_KEY, INI_FILE_MOG_CASE_VALUE);
	if (ini.GetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_WARDROBE_KEY) == NULL)
		ini.SetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_WARDROBE_KEY, INI_FILE_MOG_WARDROBE_VALUE);
	if (ini.GetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_WARDROBE_2_KEY) == NULL)
		ini.SetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_WARDROBE_2_KEY, INI_FILE_MOG_WARDROBE_2_VALUE);
	if (ini.GetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_WARDROBE_3_KEY) == NULL)
		ini.SetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_WARDROBE_3_KEY, INI_FILE_MOG_WARDROBE_3_VALUE);
	if (ini.GetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_WARDROBE_4_KEY) == NULL)
		ini.SetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_WARDROBE_4_KEY, INI_FILE_MOG_WARDROBE_4_VALUE);
	if (ini.GetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_WARDROBE_5_KEY) == NULL)
		ini.SetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_WARDROBE_5_KEY, INI_FILE_MOG_WARDROBE_5_VALUE);
	if (ini.GetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_WARDROBE_6_KEY) == NULL)
		ini.SetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_WARDROBE_6_KEY, INI_FILE_MOG_WARDROBE_6_VALUE);
	if (ini.GetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_WARDROBE_7_KEY) == NULL)
		ini.SetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_WARDROBE_7_KEY, INI_FILE_MOG_WARDROBE_7_VALUE);
	if (ini.GetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_WARDROBE_8_KEY) == NULL)
		ini.SetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_WARDROBE_8_KEY, INI_FILE_MOG_WARDROBE_8_VALUE);
}

static bool IsAbsolutePath(const std::wstring &path)
{
	if (path.empty())
		return false;

	if (path.size() >= 2 && path[1] == L':')
		return true;

	return path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\';
}

static std::wstring GetDirectoryPath(const std::wstring &path)
{
	size_t pos = path.find_last_of(L"\\/");
	if (pos == std::wstring::npos)
		return std::wstring();

	return path.substr(0, pos);
}

static std::wstring ResolvePath(const std::wstring &baseDir, const std::wstring &value)
{
	if (value.empty())
		return value;

	if (IsAbsolutePath(value))
		return value;

	if (baseDir.empty())
		return value;

	std::wstring combined = baseDir;
	if (combined.back() != L'\\' && combined.back() != L'/')
		combined += L'\\';
	combined += value;
	return combined;
}

static std::wstring TrimWhitespace(const std::wstring &value)
{
	if (value.empty())
		return value;

	size_t start = 0;
	while (start < value.size() && iswspace(value[start]))
		++start;

	size_t end = value.size();
	while (end > start && iswspace(value[end - 1]))
		--end;

	return value.substr(start, end - start);
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


static bool FileExists(const std::wstring &path)
{
	if (path.empty())
		return false;

	DWORD attrs = GetFileAttributes(path.c_str());
	return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

struct KeyItemInfo
{
	std::wstring Name;
	std::wstring Category;
};

static bool ReadFileText(const std::wstring &path, std::string &out)
{
	out.clear();
	CFile file;
	if (!file.Open(path.c_str(), CFile::modeRead | CFile::shareDenyNone))
		return false;

	ULONGLONG length = file.GetLength();
	if (length == 0)
		return false;

	out.resize((size_t)length);
	file.Read(&out[0], (UINT)length);
	return true;
}

static std::wstring ToWide(const std::string &value)
{
	if (value.empty())
		return std::wstring();

	int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), (int)value.size(), NULL, 0);
	if (size <= 0)
		return std::wstring();

	std::wstring result;
	result.resize(size);
	MultiByteToWideChar(CP_UTF8, 0, value.c_str(), (int)value.size(), &result[0], size);
	return result;
}

static std::wstring CapitalizeFirstLetter(const std::wstring &value)
{
	if (value.empty())
		return value;

	std::wstring result = value;
	for (size_t i = 0; i < result.size(); ++i)
	{
		if (iswalpha(result[i]))
		{
			result[i] = (wchar_t)towupper(result[i]);
			break;
		}
	}

	return result;
}

static bool ParseQuotedString(const std::string &data, size_t start, std::string &out, size_t &endPos)
{
	out.clear();
	endPos = std::string::npos;

	bool escape = false;
	for (size_t i = start; i < data.size(); ++i)
	{
		char ch = data[i];
		if (escape)
		{
			switch (ch)
			{
			case 'n':
				out.push_back('\n');
				break;
			case 'r':
				out.push_back('\r');
				break;
			case 't':
				out.push_back('\t');
				break;
			default:
				out.push_back(ch);
				break;
			}
			escape = false;
			continue;
		}

		if (ch == '\\')
		{
			escape = true;
			continue;
		}

		if (ch == '"')
		{
			endPos = i;
			return true;
		}

		out.push_back(ch);
	}

	return false;
}

static void ParseKeyItems(const std::string &data, std::unordered_map<int, KeyItemInfo> &items)
{
	size_t pos = 0;
	while ((pos = data.find("id=", pos)) != std::string::npos)
	{
		size_t idStart = pos + 3;
		size_t idEnd = idStart;
		while (idEnd < data.size() && isdigit((unsigned char)data[idEnd]))
			++idEnd;

		if (idEnd == idStart)
		{
			pos = idEnd;
			continue;
		}

		int id = atoi(data.substr(idStart, idEnd - idStart).c_str());
		size_t enPos = data.find("en=\"", idEnd);
		if (enPos == std::string::npos)
		{
			pos = idEnd;
			continue;
		}

		enPos += 4;
		std::string name;
		size_t enEnd = std::string::npos;
		if (!ParseQuotedString(data, enPos, name, enEnd))
		{
			pos = enPos;
			continue;
		}
		std::string category;

		size_t objEnd = data.find('}', enEnd);
		if (objEnd != std::string::npos)
		{
			size_t catPos = data.find("category=\"", enEnd);
			if (catPos != std::string::npos && catPos < objEnd)
			{
				catPos += 10;
				size_t catEnd = std::string::npos;
				if (!ParseQuotedString(data, catPos, category, catEnd) || catEnd >= objEnd)
					category.clear();
			}
		}

		KeyItemInfo info;
		info.Name = ToWide(name);
		info.Category = ToWide(category);
		items[id] = info;
		pos = enEnd;
	}
}

static bool ParseFindAllData(const std::string &data, long long &gil, std::vector<int> &keyItemIds)
{
	gil = 0;
	keyItemIds.clear();

	size_t gilPos = data.find("[\"gil\"]");
	if (gilPos != std::string::npos)
	{
		size_t eqPos = data.find('=', gilPos);
		if (eqPos != std::string::npos)
		{
			size_t numStart = eqPos + 1;
			while (numStart < data.size() && isspace((unsigned char)data[numStart]))
				++numStart;
			size_t numEnd = numStart;
			while (numEnd < data.size() && isdigit((unsigned char)data[numEnd]))
				++numEnd;
			if (numEnd > numStart)
				gil = _atoi64(data.substr(numStart, numEnd - numStart).c_str());
		}
	}

	size_t tablePos = data.find("[\"key items\"]");
	if (tablePos == std::string::npos)
		return false;

	size_t braceStart = data.find('{', tablePos);
	if (braceStart == std::string::npos)
		return false;

	size_t idx = braceStart + 1;
	int depth = 1;
	size_t braceEnd = std::string::npos;
	for (; idx < data.size(); ++idx)
	{
		if (data[idx] == '{')
			++depth;
		else if (data[idx] == '}')
		{
			--depth;
			if (depth == 0)
			{
				braceEnd = idx;
				break;
			}
		}
	}

	if (braceEnd == std::string::npos || braceEnd <= braceStart)
		return false;

	size_t pos = braceStart;
	while ((pos = data.find("[\"", pos)) != std::string::npos && pos < braceEnd)
	{
		size_t idStart = pos + 2;
		size_t idEnd = data.find("\"]", idStart);
		if (idEnd == std::string::npos || idEnd > braceEnd)
			break;

		std::string idText = data.substr(idStart, idEnd - idStart);
		int id = atoi(idText.c_str());
		if (id > 0)
			keyItemIds.push_back(id);

		pos = idEnd + 2;
	}

	return true;
}

static void AddKeyItem(const KeyItemInfo *info, int id, std::vector<CoreItem> &items)
{
	CoreItem item;
	item.Id = id;
	item.Count = 1;
	item.Name = info ? CapitalizeFirstLetter(info->Name) : std::wstring();
	item.Attr.clear();
	item.Description.clear();
	item.Slot = L"Key Item";
	item.Races.clear();
	item.Level.clear();
	item.Jobs.clear();
	item.Remarks = info ? info->Category : std::wstring();
	item.Median.clear();
	item.LastSale.clear();
	item.IconWidth = 0;
	item.IconHeight = 0;
	item.IconStride = 0;
	item.IconPixels.clear();

	if (item.Name.empty())
	{
		CString fallback;
		fallback.Format(_T("Key Item %d"), id);
		item.Name = ToWString(fallback);
	}

	items.push_back(item);
}

static void AddGilItem(long long gil, std::vector<CoreItem> &items)
{
	if (gil <= 0)
		return;

	CoreItem item;
	item.Id = 0;
	item.Count = gil > INT_MAX ? INT_MAX : (int)gil;
	item.Name = L"Gil";
	item.Attr.clear();
	item.Description = L"FindAll";
	item.Slot = L"Currency";
	item.Races.clear();
	item.Level.clear();
	item.Jobs.clear();
	item.Remarks = L"Gil";
	item.Median.clear();
	item.LastSale.clear();
	item.IconWidth = 0;
	item.IconHeight = 0;
	item.IconStride = 0;
	item.IconPixels.clear();
	items.push_back(item);
}

static std::wstring BuildFindAllDataFile(const std::wstring &dataDir, const std::wstring &characterName)
{
	std::wstring dataFile = dataDir;
	if (!dataFile.empty() && dataFile.back() != L'\\' && dataFile.back() != L'/')
		dataFile += L'\\';
	dataFile += characterName;
	dataFile += L".lua";
	return dataFile;
}

static void AddFindAllErrorItem(const std::wstring &dataFile, const std::wstring &keyItemsPath,
	const std::wstring &error,
	std::vector<CoreItem> &items)
{
	CoreItem item;
	item.Id = 0;
	item.Count = 0;
	item.Name = L"FindAll data missing";
	item.Attr.clear();
	item.Description = error.empty()
		? L"Check FindAll data/key_items paths in config.ini."
		: error;
	item.Slot = L"Key Item";
	item.Races.clear();
	item.Level.clear();
	item.Jobs.clear();
	item.Remarks = dataFile;
	item.Median.clear();
	item.LastSale.clear();
	item.IconWidth = 0;
	item.IconHeight = 0;
	item.IconStride = 0;
	item.IconPixels.clear();
	items.push_back(item);

	if (!keyItemsPath.empty())
	{
		CoreItem keyItem;
		keyItem.Id = 0;
		keyItem.Count = 0;
		keyItem.Name = L"Key items path";
		keyItem.Attr.clear();
		keyItem.Description = keyItemsPath;
		keyItem.Slot = L"Key Item";
		keyItem.Races.clear();
		keyItem.Level.clear();
		keyItem.Jobs.clear();
		keyItem.Remarks.clear();
		keyItem.Median.clear();
		keyItem.LastSale.clear();
		keyItem.IconWidth = 0;
		keyItem.IconHeight = 0;
		keyItem.IconStride = 0;
		keyItem.IconPixels.clear();
		items.push_back(keyItem);
	}
}

static bool LoadFindAllKeyItems(const std::wstring &dataDir,
	const std::wstring &keyItemsPath,
	const std::wstring &characterName,
	std::vector<CoreItem> &items,
	std::wstring &error)
{
	error.clear();


	if (dataDir.empty())
	{
		error = L"FindAllDataPath is empty in config.ini.";
		return false;
	}

	if (keyItemsPath.empty())
	{
		error = L"FindAllKeyItemsPath is empty in config.ini.";
		return false;
	}

	std::wstring trimmedName = TrimWhitespace(characterName);
	if (trimmedName.empty())
	{
		error = L"Character name is empty.";
		return false;
	}

	std::wstring dataFile = BuildFindAllDataFile(dataDir, trimmedName);

	std::string keyItemsText;
	if (!ReadFileText(keyItemsPath, keyItemsText))
	{
		error = L"Missing key items file: " + keyItemsPath;
		return false;
	}

	std::string dataText;
	if (!ReadFileText(dataFile, dataText))
	{
		error = L"Missing FindAll data file: " + dataFile;
		return false;
	}

	std::unordered_map<int, KeyItemInfo> keyItems;
	ParseKeyItems(keyItemsText, keyItems);

	long long gil = 0;
	std::vector<int> keyItemIds;
	if (!ParseFindAllData(dataText, gil, keyItemIds))
	{
		error = L"Unable to parse key items table in: " + dataFile;
		return false;
	}

	AddGilItem(gil, items);

	for (size_t i = 0; i < keyItemIds.size(); ++i)
	{
		int id = keyItemIds[i];
		std::unordered_map<int, KeyItemInfo>::const_iterator it = keyItems.find(id);
		const KeyItemInfo *info = it != keyItems.end() ? &it->second : NULL;
		AddKeyItem(info, id, items);
	}

	return true;
}

static void FillIconPixels(const FFXiIconInfo &iconInfo, CoreItem &item)
{
#ifdef GDIPLUS_IMAGE_RESIZING
	if (BuildLegacyIconPixels(iconInfo, item))
		return;
#endif
	const BITMAPINFOHEADER &header = iconInfo.ImageInfo.bmiHeader;
	int width = header.biWidth;
	int height = header.biHeight;
	if (width <= 0)
		width = 16;
	if (height == 0)
		height = 16;

	int absHeight = height < 0 ? -height : height;
	int bitsPerPixel = header.biBitCount;
	int bytesPerPixel = bitsPerPixel / 8;
	if (bytesPerPixel <= 0)
		bytesPerPixel = 4;

	item.IconWidth = width;
	item.IconHeight = absHeight;
	item.IconStride = width * 4;
	item.IconPixels.clear();
	item.IconPixels.resize((size_t)item.IconStride * absHeight);

	const unsigned char *raw = reinterpret_cast<const unsigned char*>(iconInfo.ImageInfo.ImageData);
	if (bitsPerPixel == 8)
	{
		for (int y = 0; y < absHeight; ++y)
		{
			int srcRow = height > 0 ? (absHeight - 1 - y) : y;
			for (int x = 0; x < width; ++x)
			{
				unsigned char idx = raw[srcRow * width + x];
				const RGBQUAD &color = iconInfo.ImageInfo.bmiColors[idx % 64];
				size_t dstIndex = (size_t)y * item.IconStride + x * 4;
				item.IconPixels[dstIndex + 0] = color.rgbBlue;
				item.IconPixels[dstIndex + 1] = color.rgbGreen;
				item.IconPixels[dstIndex + 2] = color.rgbRed;
				item.IconPixels[dstIndex + 3] = 0xFF;
			}
		}
		return;
	}

	if (bitsPerPixel == 32)
	{
		for (int y = 0; y < absHeight; ++y)
		{
			int srcRow = height > 0 ? (absHeight - 1 - y) : y;
			const unsigned char *src = raw + (size_t)srcRow * width * 4;
			unsigned char *dst = item.IconPixels.data() + (size_t)y * item.IconStride;
			for (int x = 0; x < width; ++x)
			{
				dst[x * 4 + 0] = src[x * 4 + 0];
				dst[x * 4 + 1] = src[x * 4 + 1];
				dst[x * 4 + 2] = src[x * 4 + 2];
				dst[x * 4 + 3] = 0xFF;
			}
		}
		return;
	}
}

bool CoreApi::LoadConfig(const std::wstring &configPath,
	CoreSettings &settings,
	std::vector<InventoryTabInfo> &tabs,
	std::vector<CharacterInfo> &characters)
{
	tabs.clear();
	characters.clear();

	CSimpleIni ini(true, false, false);
	CStringW iniPath = ToCString(configPath);

	if (GetFileAttributes(iniPath) == MAXDWORD)
	{
		FILE *iniFile = NULL;
		_tfopen_s(&iniFile, iniPath, _T("wb"));
		if (iniFile != NULL)
			fclose(iniFile);
	}

	if (ini.LoadFile(iniPath) < 0)
		return false;

	EnsureDefaultConfig(ini);
	EnsureInventoryDefaults(ini);

	settings.Region = (int)ini.GetLongValue(INI_FILE_CONFIG_SECTION, INI_FILE_GAME_REGION_KEY, INI_FILE_GAME_REGION_VALUE);
	settings.Language = (int)ini.GetLongValue(INI_FILE_CONFIG_SECTION, INI_FILE_LANGUAGE_KEY, INI_FILE_LANGUAGE_VALUE);
	settings.CompactList = ini.GetLongValue(INI_FILE_CONFIG_SECTION, INI_FILE_COMPACT_LIST_KEY, INI_FILE_COMPACT_LIST_VALUE ? 1L : 0L) != 0;

	const TCHAR *server = ini.GetValue(INI_FILE_CONFIG_SECTION, INI_FILE_FFXIAH_SERVER_KEY);
	settings.FfxiahServer = server ? std::wstring(server) : std::wstring(INI_FILE_FFXIAH_SERVER_VALUE);
	settings.FfxiahCacheTtlEnabled = ini.GetLongValue(INI_FILE_CONFIG_SECTION, INI_FILE_FFXIAH_CACHE_TTL_KEY, INI_FILE_FFXIAH_CACHE_TTL_VALUE ? 1L : 0L) != 0;
	settings.FindAllEnabled = ini.GetLongValue(INI_FILE_CONFIG_SECTION, INI_FILE_FINDALL_ENABLED_KEY, INI_FILE_FINDALL_ENABLED_VALUE ? 1L : 0L) != 0;

	CString findAllData(ini.GetValue(INI_FILE_CONFIG_SECTION, INI_FILE_FINDALL_DATA_PATH_KEY));
	if (findAllData.IsEmpty())
		findAllData = INI_FILE_FINDALL_DATA_PATH_VALUE;

	CString findAllKeys(ini.GetValue(INI_FILE_CONFIG_SECTION, INI_FILE_FINDALL_KEYITEMS_PATH_KEY));
	if (findAllKeys.IsEmpty())
		findAllKeys = INI_FILE_FINDALL_KEYITEMS_PATH_VALUE;

	std::wstring baseDir = GetDirectoryPath(configPath);
	settings.FindAllDataPath = ResolvePath(baseDir, ToWString(findAllData));
	settings.FindAllKeyItemsPath = ResolvePath(baseDir, ToWString(findAllKeys));

	CString ffxiPathValue(ini.GetValue(INI_FILE_CONFIG_SECTION, INI_FILE_GAME_DIRECTORY));
	if (ffxiPathValue.IsEmpty())
	{
		FFXiHelper helper(settings.Region);
		ffxiPathValue = helper.GetInstallPath(settings.Region);
	}
	settings.FfxiPath = ToWString(ffxiPathValue);

	CSimpleIni::TNamesDepend keys;
	if (ini.GetAllKeys(INI_FILE_INVENTORY_SECTION, keys))
	{
		keys.sort(CSimpleIni::Entry::LoadOrder());
		for (CSimpleIni::TNamesDepend::const_iterator it = keys.begin(); it != keys.end(); ++it)
		{
			const TCHAR *key = it->pItem;
			const TCHAR *value = ini.GetValue(INI_FILE_INVENTORY_SECTION, key);

			InventoryTabInfo info;
			info.FileName = key ? std::wstring(key) : std::wstring();
			info.DisplayName = value ? std::wstring(value) : std::wstring();
			tabs.push_back(info);
		}
	}

	if (settings.FindAllEnabled)
	{
		InventoryTabInfo info;
		info.FileName = L"__FINDALL_KEYITEMS__";
		info.DisplayName = L"Key Items";
		tabs.push_back(info);
	}

	if (settings.FfxiPath.empty())
		return true;

	CString userPath;
	userPath.Format(_T("%s\\%s\\*.*"), ffxiPathValue, FFXI_PATH_USER_DATA);

	CFileFind finder;
	BOOL hasFile = finder.FindFile(userPath);

	while (hasFile)
	{
		hasFile = finder.FindNextFile();
		if (!finder.IsDirectory() || finder.IsDots())
			continue;

		CString id = finder.GetFileName();
		const TCHAR *display = ini.GetValue(INI_FILE_CHARACTERS_SECTION, id);

		CharacterInfo character;
		character.Id = std::wstring(id.GetString());

		if (display == NULL || _tcslen(display) == 0)
			character.Name = character.Id;
		else
			character.Name = std::wstring(display);

		characters.push_back(character);
	}

	finder.Close();
	return true;
}

bool CoreApi::LoadInventoryForCharacter(const CoreSettings &settings,
	const CharacterInfo &character,
	const std::vector<InventoryTabInfo> &tabs,
	std::vector<InventoryTab> &outTabs)
{
	outTabs.clear();

	if (settings.FfxiPath.empty())
		return false;

	FFXiHelper helper(settings.Region);
	helper.SetInstallPath(ToCString(settings.FfxiPath));

	CString basePath = ToCString(settings.FfxiPath);
	basePath.TrimRight('\\');

	for (size_t i = 0; i < tabs.size(); ++i)
	{
		const InventoryTabInfo &tabInfo = tabs[i];
		InventoryTab tab;
		tab.Info = tabInfo;

		if (tabInfo.FileName == L"__FINDALL_KEYITEMS__")
		{
			std::vector<CoreItem> keyItems;
			std::wstring error;
			bool loaded = LoadFindAllKeyItems(settings.FindAllDataPath, settings.FindAllKeyItemsPath,
				character.Name, keyItems, error);

			if (!loaded)
			{
				std::wstring dataFile = BuildFindAllDataFile(settings.FindAllDataPath,
					TrimWhitespace(character.Name));
				AddFindAllErrorItem(dataFile, settings.FindAllKeyItemsPath, error, keyItems);
			}

			tab.Items = keyItems;
			outTabs.push_back(tab);
			continue;
		}

		CString invFile;
		invFile.Format(_T("%s\\%s\\%s\\%s"),
			basePath.GetString(),
			FFXI_PATH_USER_DATA,
			ToCString(character.Id).GetString(),
			ToCString(tabInfo.FileName).GetString());

		ItemArray itemMap;
		ItemLocationInfo location;
		location.InvTab = (int)i;
		location.Character = 0;
		location.ListIndex = 0;
		location.ImageIndex = 0;
		location.Location.Empty();

		if (helper.ParseInventoryFile(invFile, location, &itemMap, settings.Language, false))
		{
			POSITION pos = itemMap.GetStartPosition();
			InventoryItem *item = NULL;
			int itemId = 0;

			while (pos != NULL)
			{
				itemMap.GetNextAssoc(pos, itemId, item);
				if (item == NULL)
					continue;

				CoreItem coreItem;
				coreItem.Id = item->ItemHdr.ItemID;
				coreItem.Count = item->RefCount;
				coreItem.Name = ToWString(item->ItemName);
				coreItem.Attr = ToWString(item->Attr);
				coreItem.Description = ToWString(item->ItemDescription);
				coreItem.Slot = ToWString(item->Slot);
				coreItem.Races = ToWString(item->Races);
				coreItem.Level = ToWString(item->Level);
				coreItem.Jobs = ToWString(item->Jobs);
				coreItem.Remarks = ToWString(item->Remarks);
				coreItem.Median = ToWString(item->Median);
				coreItem.LastSale = std::wstring();
				coreItem.IconWidth = 0;
				coreItem.IconHeight = 0;
				coreItem.IconStride = 0;
				coreItem.IconPixels.clear();
				FillIconPixels(item->IconInfo, coreItem);
				tab.Items.push_back(coreItem);

				helper.ClearItemData(item);
				delete item;
			}
		}

		itemMap.RemoveAll();
		outTabs.push_back(tab);
	}

	return true;
}

bool CoreApi::SaveSettings(const std::wstring &configPath, const CoreSettings &settings)
{
	CSimpleIni ini(true, false, false);
	CStringW iniPath = ToCString(configPath);
	if (ini.LoadFile(iniPath) < 0)
		return false;

	ini.SetLongValue(INI_FILE_CONFIG_SECTION, INI_FILE_GAME_REGION_KEY, settings.Region);
	ini.SetLongValue(INI_FILE_CONFIG_SECTION, INI_FILE_LANGUAGE_KEY, settings.Language);
	ini.SetLongValue(INI_FILE_CONFIG_SECTION, INI_FILE_COMPACT_LIST_KEY, settings.CompactList ? 1L : 0L);
	ini.SetValue(INI_FILE_CONFIG_SECTION, INI_FILE_GAME_DIRECTORY, ToCString(settings.FfxiPath));
	ini.SetValue(INI_FILE_CONFIG_SECTION, INI_FILE_FFXIAH_SERVER_KEY, ToCString(settings.FfxiahServer));
	ini.SetLongValue(INI_FILE_CONFIG_SECTION, INI_FILE_FFXIAH_CACHE_TTL_KEY, settings.FfxiahCacheTtlEnabled ? 1L : 0L);
	ini.SetLongValue(INI_FILE_CONFIG_SECTION, INI_FILE_FINDALL_ENABLED_KEY, settings.FindAllEnabled ? 1L : 0L);
	ini.SetValue(INI_FILE_CONFIG_SECTION, INI_FILE_FINDALL_DATA_PATH_KEY, ToCString(settings.FindAllDataPath));
	ini.SetValue(INI_FILE_CONFIG_SECTION, INI_FILE_FINDALL_KEYITEMS_PATH_KEY, ToCString(settings.FindAllKeyItemsPath));

	return ini.SaveFile(iniPath) >= 0;
}

static CString FormatCacheSection(const std::wstring &server, const TCHAR *baseSection)
{
	CString section;
	if (server.empty())
		section = baseSection;
	else
		section.Format(_T("%s_%s"), baseSection, CString(server.c_str()));
	return section;
}

bool CoreApi::LoadFfxiahCache(const std::wstring &configPath,
	const std::wstring &server,
	std::vector<std::pair<int, std::wstring>> &outEntries)
{
	outEntries.clear();

	CSimpleIni ini(true, false, false);
	CStringW iniPath = ToCString(configPath);
	if (ini.LoadFile(iniPath) < 0)
		return false;

	CString section = FormatCacheSection(server, INI_FILE_FFXIAH_CACHE_SECTION);
	CSimpleIni::TNamesDepend keys;
	if (!ini.GetAllKeys(section, keys))
		return true;

	keys.sort(CSimpleIni::Entry::LoadOrder());
	for (CSimpleIni::TNamesDepend::const_iterator it = keys.begin(); it != keys.end(); ++it)
	{
		const TCHAR *key = it->pItem;
		const TCHAR *value = ini.GetValue(section, key);
		if (key == NULL || value == NULL)
			continue;

		int itemId = _ttoi(key);
		if (itemId <= 0)
			continue;

		outEntries.push_back(std::make_pair(itemId, std::wstring(value)));
	}

	return true;
}

bool CoreApi::SaveFfxiahCache(const std::wstring &configPath,
	const std::wstring &server,
	const std::vector<std::pair<int, std::wstring>> &entries)
{
	CSimpleIni ini(true, false, false);
	CStringW iniPath = ToCString(configPath);
	if (ini.LoadFile(iniPath) < 0)
		return false;

	CString section = FormatCacheSection(server, INI_FILE_FFXIAH_CACHE_SECTION);
	ini.Delete(section, NULL);

	for (size_t i = 0; i < entries.size(); ++i)
	{
		CString key;
		key.Format(_T("%d"), entries[i].first);
		ini.SetValue(section, key, CString(entries[i].second.c_str()));
	}

	return ini.SaveFile(iniPath) >= 0;
}

bool CoreApi::LoadFfxiahLastSaleCache(const std::wstring &configPath,
	const std::wstring &server,
	std::vector<std::pair<int, std::wstring>> &outEntries)
{
	outEntries.clear();

	CSimpleIni ini(true, false, false);
	CStringW iniPath = ToCString(configPath);
	if (ini.LoadFile(iniPath) < 0)
		return false;

	CString section = FormatCacheSection(server, INI_FILE_FFXIAH_LAST_CACHE_SECTION);
	CSimpleIni::TNamesDepend keys;
	if (!ini.GetAllKeys(section, keys))
		return true;

	keys.sort(CSimpleIni::Entry::LoadOrder());
	for (CSimpleIni::TNamesDepend::const_iterator it = keys.begin(); it != keys.end(); ++it)
	{
		const TCHAR *key = it->pItem;
		const TCHAR *value = ini.GetValue(section, key);
		if (key == NULL || value == NULL)
			continue;

		int itemId = _ttoi(key);
		if (itemId <= 0)
			continue;

		outEntries.push_back(std::make_pair(itemId, std::wstring(value)));
	}

	return true;
}

bool CoreApi::SaveFfxiahLastSaleCache(const std::wstring &configPath,
	const std::wstring &server,
	const std::vector<std::pair<int, std::wstring>> &entries)
{
	CSimpleIni ini(true, false, false);
	CStringW iniPath = ToCString(configPath);
	if (ini.LoadFile(iniPath) < 0)
		return false;

	CString section = FormatCacheSection(server, INI_FILE_FFXIAH_LAST_CACHE_SECTION);
	ini.Delete(section, NULL);

	for (size_t i = 0; i < entries.size(); ++i)
	{
		CString key;
		key.Format(_T("%d"), entries[i].first);
		ini.SetValue(section, key, CString(entries[i].second.c_str()));
	}

	return ini.SaveFile(iniPath) >= 0;
}

bool CoreApi::LoadFfxiahCacheTimes(const std::wstring &configPath,
	const std::wstring &server,
	std::vector<std::pair<int, long long>> &outEntries)
{
	outEntries.clear();

	CSimpleIni ini(true, false, false);
	CStringW iniPath = ToCString(configPath);
	if (ini.LoadFile(iniPath) < 0)
		return false;

	CString section = FormatCacheSection(server, INI_FILE_FFXIAH_CACHE_TIME_SECTION);
	CSimpleIni::TNamesDepend keys;
	if (!ini.GetAllKeys(section, keys))
		return true;

	keys.sort(CSimpleIni::Entry::LoadOrder());
	for (CSimpleIni::TNamesDepend::const_iterator it = keys.begin(); it != keys.end(); ++it)
	{
		const TCHAR *key = it->pItem;
		const TCHAR *value = ini.GetValue(section, key);
		if (key == NULL || value == NULL)
			continue;

		int itemId = _ttoi(key);
		if (itemId <= 0)
			continue;

		long long ts = _ttoi64(value);
		outEntries.push_back(std::make_pair(itemId, ts));
	}

	return true;
}

bool CoreApi::SaveFfxiahCacheTimes(const std::wstring &configPath,
	const std::wstring &server,
	const std::vector<std::pair<int, long long>> &entries)
{
	CSimpleIni ini(true, false, false);
	CStringW iniPath = ToCString(configPath);
	if (ini.LoadFile(iniPath) < 0)
		return false;

	CString section = FormatCacheSection(server, INI_FILE_FFXIAH_CACHE_TIME_SECTION);
	ini.Delete(section, NULL);

	for (size_t i = 0; i < entries.size(); ++i)
	{
		CString key;
		key.Format(_T("%d"), entries[i].first);
		CString value;
		value.Format(_T("%lld"), entries[i].second);
		ini.SetValue(section, key, value);
	}

	return ini.SaveFile(iniPath) >= 0;
}

bool CoreApi::SaveCharacterDisplayNames(const std::wstring &configPath,
	const std::vector<std::pair<std::wstring, std::wstring>> &entries)
{
	CSimpleIni ini(true, false, false);
	CStringW iniPath = ToCString(configPath);
	if (ini.LoadFile(iniPath) < 0)
		return false;

	for (size_t i = 0; i < entries.size(); ++i)
	{
		const std::wstring &id = entries[i].first;
		const std::wstring &name = entries[i].second;
		if (id.empty())
			continue;

		CString key(id.c_str());
		if (name.empty() || name == id)
			ini.Delete(INI_FILE_CHARACTERS_SECTION, key);
		else
			ini.SetValue(INI_FILE_CHARACTERS_SECTION, key, CString(name.c_str()));
	}

	return ini.SaveFile(iniPath) >= 0;
}
