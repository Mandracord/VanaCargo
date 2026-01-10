#pragma once

using namespace System;

namespace VanaCargoBridge
{
	public ref class ManagedSettings
	{
	public:
		property int Region
		{
			int get() { return m_region; }
			void set(int value) { m_region = value; }
		}

		property int Language
		{
			int get() { return m_language; }
			void set(int value) { m_language = value; }
		}

		property bool CompactList
		{
			bool get() { return m_compactList; }
			void set(bool value) { m_compactList = value; }
		}

		property String^ FfxiPath
		{
			String^ get() { return m_ffxiPath; }
			void set(String^ value) { m_ffxiPath = value; }
		}

		property String^ FfxiahServer
		{
			String^ get() { return m_ffxiahServer; }
			void set(String^ value) { m_ffxiahServer = value; }
		}

		property bool FfxiahCacheTtlEnabled
		{
			bool get() { return m_ffxiahCacheTtlEnabled; }
			void set(bool value) { m_ffxiahCacheTtlEnabled = value; }
		}

		property bool FindAllEnabled
		{
			bool get() { return m_findAllEnabled; }
			void set(bool value) { m_findAllEnabled = value; }
		}

		property String^ FindAllDataPath
		{
			String^ get() { return m_findAllDataPath; }
			void set(String^ value) { m_findAllDataPath = value; }
		}

		property String^ FindAllKeyItemsPath
		{
			String^ get() { return m_findAllKeyItemsPath; }
			void set(String^ value) { m_findAllKeyItemsPath = value; }
		}

	private:
		int m_region = 0;
		int m_language = 0;
		bool m_compactList = false;
		String^ m_ffxiPath = nullptr;
		String^ m_ffxiahServer = nullptr;
		bool m_ffxiahCacheTtlEnabled = true;
		bool m_findAllEnabled = false;
		String^ m_findAllDataPath = nullptr;
		String^ m_findAllKeyItemsPath = nullptr;
	};

	public ref class ManagedTabInfo
	{
	public:
		property String^ FileName
		{
			String^ get() { return m_fileName; }
			void set(String^ value) { m_fileName = value; }
		}

		property String^ DisplayName
		{
			String^ get() { return m_displayName; }
			void set(String^ value) { m_displayName = value; }
		}

	private:
		String^ m_fileName = nullptr;
		String^ m_displayName = nullptr;
	};

	public ref class ManagedCharacter
	{
	public:
		property String^ Id
		{
			String^ get() { return m_id; }
			void set(String^ value) { m_id = value; }
		}

		property String^ Name
		{
			String^ get() { return m_name; }
			void set(String^ value) { m_name = value; }
		}

	private:
		String^ m_id = nullptr;
		String^ m_name = nullptr;
	};

	public ref class ManagedItem
	{
	public:
		property int Id
		{
			int get() { return m_id; }
			void set(int value) { m_id = value; }
		}

		property int Count
		{
			int get() { return m_count; }
			void set(int value) { m_count = value; }
		}

		property String^ Name
		{
			String^ get() { return m_name; }
			void set(String^ value) { m_name = value; }
		}

		property String^ Attr
		{
			String^ get() { return m_attr; }
			void set(String^ value) { m_attr = value; }
		}

		property String^ Description
		{
			String^ get() { return m_description; }
			void set(String^ value) { m_description = value; }
		}

		property String^ Slot
		{
			String^ get() { return m_slot; }
			void set(String^ value) { m_slot = value; }
		}

		property String^ Races
		{
			String^ get() { return m_races; }
			void set(String^ value) { m_races = value; }
		}

		property String^ Level
		{
			String^ get() { return m_level; }
			void set(String^ value) { m_level = value; }
		}

		property String^ Jobs
		{
			String^ get() { return m_jobs; }
			void set(String^ value) { m_jobs = value; }
		}

		property String^ Remarks
		{
			String^ get() { return m_remarks; }
			void set(String^ value) { m_remarks = value; }
		}

		property String^ Median
		{
			String^ get() { return m_median; }
			void set(String^ value) { m_median = value; }
		}

		property String^ LastSale
		{
			String^ get() { return m_lastSale; }
			void set(String^ value) { m_lastSale = value; }
		}

		property int IconWidth
		{
			int get() { return m_iconWidth; }
			void set(int value) { m_iconWidth = value; }
		}

		property int IconHeight
		{
			int get() { return m_iconHeight; }
			void set(int value) { m_iconHeight = value; }
		}

		property int IconStride
		{
			int get() { return m_iconStride; }
			void set(int value) { m_iconStride = value; }
		}

		property array<Byte>^ IconPixels
		{
			array<Byte>^ get() { return m_iconPixels; }
			void set(array<Byte>^ value) { m_iconPixels = value; }
		}

	private:
		int m_id = 0;
		int m_count = 0;
		String^ m_name = nullptr;
		String^ m_attr = nullptr;
		String^ m_description = nullptr;
		String^ m_slot = nullptr;
		String^ m_races = nullptr;
		String^ m_level = nullptr;
		String^ m_jobs = nullptr;
		String^ m_remarks = nullptr;
		String^ m_median = nullptr;
		String^ m_lastSale = nullptr;
		int m_iconWidth = 0;
		int m_iconHeight = 0;
		int m_iconStride = 0;
		array<Byte>^ m_iconPixels = nullptr;
	};

	public ref class ManagedTab
	{
	public:
		property ManagedTabInfo^ Info
		{
			ManagedTabInfo^ get() { return m_info; }
			void set(ManagedTabInfo^ value) { m_info = value; }
		}

		property array<ManagedItem^>^ Items
		{
			array<ManagedItem^>^ get() { return m_items; }
			void set(array<ManagedItem^>^ value) { m_items = value; }
		}

	private:
		ManagedTabInfo^ m_info = nullptr;
		array<ManagedItem^>^ m_items = nullptr;
	};

	public ref class ManagedMedianEntry
	{
	public:
		property int ItemId
		{
			int get() { return m_itemId; }
			void set(int value) { m_itemId = value; }
		}

		property String^ Median
		{
			String^ get() { return m_median; }
			void set(String^ value) { m_median = value; }
		}

	private:
		int m_itemId = 0;
		String^ m_median = nullptr;
	};

	public ref class ManagedLastSaleEntry
	{
	public:
		property int ItemId
		{
			int get() { return m_itemId; }
			void set(int value) { m_itemId = value; }
		}

		property String^ LastSale
		{
			String^ get() { return m_lastSale; }
			void set(String^ value) { m_lastSale = value; }
		}

	private:
		int m_itemId = 0;
		String^ m_lastSale = nullptr;
	};

	public ref class ManagedCacheTimeEntry
	{
	public:
		property int ItemId
		{
			int get() { return m_itemId; }
			void set(int value) { m_itemId = value; }
		}

		property long long Timestamp
		{
			long long get() { return m_timestamp; }
			void set(long long value) { m_timestamp = value; }
		}

	private:
		int m_itemId = 0;
		long long m_timestamp = 0;
	};

	public ref class LoadResult
	{
	public:
		property ManagedSettings^ Settings
		{
			ManagedSettings^ get() { return m_settings; }
			void set(ManagedSettings^ value) { m_settings = value; }
		}

		property array<ManagedTabInfo^>^ Tabs
		{
			array<ManagedTabInfo^>^ get() { return m_tabs; }
			void set(array<ManagedTabInfo^>^ value) { m_tabs = value; }
		}

		property array<ManagedCharacter^>^ Characters
		{
			array<ManagedCharacter^>^ get() { return m_characters; }
			void set(array<ManagedCharacter^>^ value) { m_characters = value; }
		}

	private:
		ManagedSettings^ m_settings = nullptr;
		array<ManagedTabInfo^>^ m_tabs = nullptr;
		array<ManagedCharacter^>^ m_characters = nullptr;
	};

	public ref class CoreBridge
	{
	public:
		CoreBridge();
		String^ Ping();
		LoadResult^ LoadConfigAndCharacters(String^ configPath);
		bool SaveSettings(String^ configPath, ManagedSettings^ settings);
		array<ManagedMedianEntry^>^ LoadFfxiahCache(String^ configPath, String^ server);
		bool SaveFfxiahCache(String^ configPath, String^ server, array<ManagedMedianEntry^>^ entries);
		array<ManagedLastSaleEntry^>^ LoadFfxiahLastSaleCache(String^ configPath, String^ server);
		bool SaveFfxiahLastSaleCache(String^ configPath, String^ server, array<ManagedLastSaleEntry^>^ entries);
		array<ManagedCacheTimeEntry^>^ LoadFfxiahCacheTimes(String^ configPath, String^ server);
		bool SaveFfxiahCacheTimes(String^ configPath, String^ server, array<ManagedCacheTimeEntry^>^ entries);
		bool SaveCharacterDisplayNames(String^ configPath, array<ManagedCharacter^>^ characters);
		array<ManagedTab^>^ LoadInventoryForCharacter(ManagedSettings^ settings,
			ManagedCharacter^ character,
			array<ManagedTabInfo^>^ tabs);
	};
}
