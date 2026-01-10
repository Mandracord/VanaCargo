// VanaCargoDlg.cpp : implementation file
//

#include "stdafx.h"

#include "ACEdit.h"
#include "SimpleIni.h"
#include "ETSLayout.h"

#include "FFXIHelper.h"
#include "SearchHandler.h"
#include "DefaultConfig.h"

using namespace ETSLayout;

#include "VanaCargo.h"
#include "FFXiItemList.h"
#include "Progress_Dlg.h"
#include "SearchDialog.h"
#include "VanaCargoDlg.h"
#include "ServerSelect.h"
#include <winhttp.h>
#include <afxmt.h>
#pragma comment(lib, "winhttp.lib")

static void OpenBgWikiUrl(InventoryItem *pItem)
{
	if (pItem == NULL)
		return;

	CString Url;
	FFXiItemList::BuildBgWikiUrl(pItem->ItemName, Url);
	::ShellExecute(NULL, _T("open"), Url, NULL, NULL, SW_SHOWNORMAL);
}

class CAboutDlg : public CDialog
{
public:
	CAboutDlg() : CDialog(IDD_ABOUTBOX) {}

protected:
	virtual BOOL OnInitDialog()
	{
		CDialog::OnInitDialog();

		HICON hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
		if (hIcon != NULL)
			SendDlgItemMessage(IDC_ABOUT_ICON, STM_SETICON, (WPARAM)hIcon, 0);

		SYSTEMTIME st = {};
		GetLocalTime(&st);

		CString text;
		text.Format(_T("VanaCargo\r\nForked from LootBox by TeoTwawki (MIT License).\r\n")
			_T("Source: https://github.com/TeoTwawki/LootBox\r\n\r\n")
			_T("Copyright (c) 2015-2026 TeoTwawki\r\n")
			_T("Copyright (c) %u Mandracord"),
			st.wYear);

		SetDlgItemText(IDC_ABOUT_TEXT, text);
		return TRUE;
	}
};

struct ServerMenuEntry
{
	UINT CmdId;
	LPCTSTR Name;
};

static const ServerMenuEntry kServerMenuEntries[] =
{
	{ ID_SERVER_BAHAMUT, _T("Bahamut") },
	{ ID_SERVER_SHIVA, _T("Shiva") },
	{ ID_SERVER_PHOENIX, _T("Phoenix") },
	{ ID_SERVER_CARBUNCLE, _T("Carbuncle") },
	{ ID_SERVER_FENRIR, _T("Fenrir") },
	{ ID_SERVER_SYLPH, _T("Sylph") },
	{ ID_SERVER_VALEFOR, _T("Valefor") },
	{ ID_SERVER_LEVIATHAN, _T("Leviathan") },
	{ ID_SERVER_ODIN, _T("Odin") },
	{ ID_SERVER_QUETZALCOATL, _T("Quetzalcoatl") },
	{ ID_SERVER_SIREN, _T("Siren") },
	{ ID_SERVER_RAGNAROK, _T("Ragnarok") },
	{ ID_SERVER_CERBERUS, _T("Cerberus") },
	{ ID_SERVER_BISMARCK, _T("Bismarck") },
	{ ID_SERVER_LAKSHMI, _T("Lakshmi") },
	{ ID_SERVER_ASURA, _T("Asura") },
};

#define WM_APP_MEDIAN_READY (WM_APP + 1)
#define WM_APP_MEDIAN_PROGRESS (WM_APP + 2)

struct MedianFetchResult
{
	int ItemId;
	CString Median;
};

struct MedianProgressUpdate
{
	int Completed;
	int Total;
};

struct MedianFetchContext
{
	CLootBoxDlg *Dlg;
	CString Server;
	CArray<int, int> ItemIds;
	int NextIndex;
	volatile LONG ActiveWorkers;
};

struct ExportMedianFetchContext
{
	const CArray<int, int> *ItemIds;
	CString Server;
	CMap<int, int, CString, CString&> *Cache;
	CCriticalSection *CacheLock;
	volatile LONG *NextIndex;
	volatile LONG *Completed;
	volatile LONG *StopFlag;
};

static CString FormatNumberWithCommas(ULONGLONG value)
{
	CString result;
	while (value >= 1000)
	{
		UINT part = (UINT)(value % 1000);
		value /= 1000;
		CString chunk;
		chunk.Format(_T(",%03u"), part);
		result = chunk + result;
	}

	CString head;
	head.Format(_T("%llu"), value);
	result = head + result;
	return result;
}

static bool ExtractMedianForServer(const CStringA &html, const CString &serverName, CString &medianOut)
{
	int listStart = html.Find("Item.server_medians");
	if (listStart < 0)
		return false;

	listStart = html.Find('[', listStart);
	if (listStart < 0)
		return false;

	int listEnd = html.Find("];", listStart);
	if (listEnd < 0)
		return false;

	CStringA listData = html.Mid(listStart + 1, listEnd - listStart - 1);
	CStringA serverUtf8;
	int serverLen = WideCharToMultiByte(CP_UTF8, 0, serverName, -1, NULL, 0, NULL, NULL);
	if (serverLen > 0)
	{
		char *serverBuf = serverUtf8.GetBuffer(serverLen);
		WideCharToMultiByte(CP_UTF8, 0, serverName, -1, serverBuf, serverLen, NULL, NULL);
		serverUtf8.ReleaseBuffer();
	}

	int objStart = 0;
	while ((objStart = listData.Find('{', objStart)) >= 0)
	{
		int objEnd = listData.Find('}', objStart);
		if (objEnd < 0)
			break;

		CStringA obj = listData.Mid(objStart, objEnd - objStart + 1);
		int namePos = obj.Find("\"server_name\":\"");
		if (namePos >= 0)
		{
			namePos += 15;
			int nameEnd = obj.Find('\"', namePos);
			if (nameEnd > namePos)
			{
				CStringA name = obj.Mid(namePos, nameEnd - namePos);
				if (name.CompareNoCase(serverUtf8) == 0)
				{
					int medianPos = obj.Find("\"median\":");
					if (medianPos >= 0)
					{
						medianPos += 9;
						CStringA number;
						while (medianPos < obj.GetLength())
						{
							char c = obj[medianPos++];
							if (c < '0' || c > '9')
								break;
							number.AppendChar(c);
						}

						if (!number.IsEmpty())
						{
							ULONGLONG value = _strtoui64(number, NULL, 10);
							medianOut = FormatNumberWithCommas(value);
							return true;
						}
					}
				}
			}
		}

		objStart = objEnd + 1;
	}

	return false;
}

static bool FetchMedianFromFfxiah(int itemId, const CString &serverName, CString &medianOut)
{
	bool ok = false;
	CStringW host = L"www.ffxiah.com";
	CStringW path;
	path.Format(L"/item/%d/", itemId);

	HINTERNET session = WinHttpOpen(L"VanaCargo/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
		WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (session == NULL)
		return false;

	HINTERNET connect = WinHttpConnect(session, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
	if (connect == NULL)
	{
		WinHttpCloseHandle(session);
		return false;
	}

	HINTERNET request = WinHttpOpenRequest(connect, L"GET", path, NULL,
		WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
	if (request == NULL)
	{
		WinHttpCloseHandle(connect);
		WinHttpCloseHandle(session);
		return false;
	}

	BOOL sent = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
		WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
	if (sent && WinHttpReceiveResponse(request, NULL))
	{
		CStringA html;
		DWORD bytesAvailable = 0;
		do
		{
			if (!WinHttpQueryDataAvailable(request, &bytesAvailable))
				break;

			if (bytesAvailable == 0)
				break;

			char *buffer = new char[bytesAvailable + 1];
			DWORD bytesRead = 0;
			if (WinHttpReadData(request, buffer, bytesAvailable, &bytesRead))
			{
				buffer[bytesRead] = '\0';
				html.Append(buffer, bytesRead);
			}
			delete[] buffer;
		} while (bytesAvailable > 0);

		if (!html.IsEmpty())
			ok = ExtractMedianForServer(html, serverName, medianOut);
	}

	WinHttpCloseHandle(request);
	WinHttpCloseHandle(connect);
	WinHttpCloseHandle(session);
	return ok;
}

static UINT MedianFetchThread(LPVOID pParam)
{
	MedianFetchContext *ctx = (MedianFetchContext*)pParam;
	if (ctx == NULL || ctx->Dlg == NULL)
		return 0;

	while (true)
	{
		if (ctx->Dlg->IsMedianStopRequested())
			break;

		int index = (int)InterlockedIncrement(reinterpret_cast<volatile LONG*>(&ctx->NextIndex)) - 1;
		if (index < 0 || index >= ctx->ItemIds.GetCount())
			break;

		int itemId = ctx->ItemIds[index];
		CString median;
		if (FetchMedianFromFfxiah(itemId, ctx->Server, median))
		{
			MedianFetchResult *result = new MedianFetchResult();
			result->ItemId = itemId;
			result->Median = median;
			::PostMessage(ctx->Dlg->m_hWnd, WM_APP_MEDIAN_READY, 0, (LPARAM)result);
		}

		LONG completed = ctx->Dlg->IncrementMedianCompleted();
		MedianProgressUpdate *progress = new MedianProgressUpdate();
		progress->Completed = (int)completed;
		progress->Total = ctx->Dlg->GetMedianTotal();
		::PostMessage(ctx->Dlg->m_hWnd, WM_APP_MEDIAN_PROGRESS, 0, (LPARAM)progress);
	}

	if (InterlockedDecrement(&ctx->ActiveWorkers) <= 0)
	{
		ctx->Dlg->ClearMedianThread();
		delete ctx;
	}
	return 0;
}

static UINT ExportMedianFetchThread(LPVOID pParam)
{
	ExportMedianFetchContext *ctx = (ExportMedianFetchContext*)pParam;
	if (ctx == NULL || ctx->ItemIds == NULL || ctx->Cache == NULL)
		return 0;

	int total = (int)ctx->ItemIds->GetCount();
	while (true)
	{
		if (*ctx->StopFlag != 0)
			break;

		LONG index = InterlockedIncrement(ctx->NextIndex) - 1;
		if (index < 0 || index >= total)
			break;

		int itemId = (*ctx->ItemIds)[index];
		CString median;
		if (!FetchMedianFromFfxiah(itemId, ctx->Server, median))
			median = _T("0");

		{
			CSingleLock lock(ctx->CacheLock, TRUE);
			ctx->Cache->SetAt(itemId, median);
		}

		InterlockedIncrement(ctx->Completed);
	}

	return 0;
}

static bool TryGetServerNameForCommand(UINT cmdId, CString &serverOut)
{
	for (int i = 0; i < (int)(sizeof(kServerMenuEntries) / sizeof(kServerMenuEntries[0])); i++)
	{
		if (kServerMenuEntries[i].CmdId == cmdId)
		{
			serverOut = kServerMenuEntries[i].Name;
			return true;
		}
	}
	return false;
}

bool CLootBoxDlg::FetchMediansForExport(const CArray<bool, bool> &exportedChars)
{
	if (m_FfxiahServer.IsEmpty())
		return true;

	CArray<int, int> ids;
	CMap<int, int, BOOL, BOOL> seen;
	int FileCount = m_InventoryFiles.GetCount();
	int CharCount = m_CharacterNames.GetCount();

	for (int CharIndex = 0; CharIndex < CharCount; ++CharIndex)
	{
		if (exportedChars[CharIndex] == false)
			continue;

		InventoryMap *pInvMap = NULL;
		if (!m_GlobalMap.Lookup(CharIndex, pInvMap) || pInvMap == NULL)
			continue;

		for (int FileIndex = 0; FileIndex < FileCount; ++FileIndex)
		{
			ItemArray *pItemMap = NULL;
			if (!pInvMap->Lookup(FileIndex, pItemMap) || pItemMap == NULL)
				continue;

			POSITION ItemPos = pItemMap->GetStartPosition();
			InventoryItem *pItem = NULL;
			int ItemID = 0;

			while (ItemPos != NULL)
			{
				pItemMap->GetNextAssoc(ItemPos, ItemID, pItem);
				if (pItem == NULL)
					continue;

				CString cached;
				if (m_ItemMedianCache.Lookup(pItem->ItemHdr.ItemID, cached))
					continue;

				BOOL exists = FALSE;
				if (!seen.Lookup(pItem->ItemHdr.ItemID, exists))
				{
					BOOL value = TRUE;
					seen.SetAt(pItem->ItemHdr.ItemID, value);
					ids.Add(pItem->ItemHdr.ItemID);
				}
			}
		}
	}

	if (ids.GetCount() == 0)
		return true;

	CProgress_Dlg progress(this);
	progress.Create(IDD_PROGRESS, this);
	progress.SetWindowText(_T("Fetching FFXIAH Prices"));
	progress.SetProgress(0, (int)ids.GetCount());

	int total = (int)ids.GetCount();
	int maxThreads = 4;
	int threadCount = total < maxThreads ? total : maxThreads;
	if (threadCount <= 0)
		threadCount = 1;

	CCriticalSection cacheLock;
	volatile LONG nextIndex = 0;
	volatile LONG completed = 0;
	volatile LONG stopFlag = 0;

	ExportMedianFetchContext ctx;
	ctx.ItemIds = &ids;
	ctx.Server = m_FfxiahServer;
	ctx.Cache = &m_ItemMedianCache;
	ctx.CacheLock = &cacheLock;
	ctx.NextIndex = &nextIndex;
	ctx.Completed = &completed;
	ctx.StopFlag = &stopFlag;

	CArray<CWinThread*, CWinThread*> threads;
	for (int i = 0; i < threadCount; i++)
	{
		CWinThread *thread = AfxBeginThread(ExportMedianFetchThread, &ctx, THREAD_PRIORITY_BELOW_NORMAL);
		if (thread != NULL)
			threads.Add(thread);
	}

	if (threads.GetCount() == 0)
	{
		progress.DestroyWindow();
		return false;
	}

	bool cancelled = false;
	while (completed < total)
	{
		progress.SetProgress((int)completed, total);

		MSG msg;
		while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		if (m_hWnd == NULL || progress.IsCancelled())
		{
			cancelled = true;
			stopFlag = 1;
			break;
		}

		Sleep(10);
	}

	stopFlag = 1;
	for (int i = 0; i < threads.GetCount(); i++)
	{
		if (threads[i] != NULL && threads[i]->m_hThread != NULL)
			WaitForSingleObject(threads[i]->m_hThread, INFINITE);
	}

	progress.SetProgress(total, total);
	progress.DestroyWindow();

	if (cancelled)
		return false;

	SaveMedianCacheToIni(m_FfxiahServer);
	return true;
}

void CLootBoxDlg::LoadMedianCacheFromIni(const CString &server)
{
	m_ItemMedianCache.RemoveAll();

	if (m_pIni == NULL || server.IsEmpty())
		return;

	CString section = GetMedianCacheSection(server);
	CSimpleIni::TNamesDepend keys;
	if (!m_pIni->GetAllKeys(section, keys))
		return;

	for (CSimpleIni::TNamesDepend::const_iterator it = keys.begin(); it != keys.end(); ++it)
	{
		const TCHAR *key = it->pItem;
		const TCHAR *value = m_pIni->GetValue(section, key);
		if (key == NULL || value == NULL)
			continue;

		int itemId = _tstoi(key);
		if (itemId <= 0)
			continue;

		CString median(value);
		m_ItemMedianCache.SetAt(itemId, median);
	}
}

void CLootBoxDlg::SaveMedianCacheToIni(const CString &server)
{
	if (m_pIni == NULL || server.IsEmpty())
		return;

	CString section = GetMedianCacheSection(server);
	m_pIni->Delete(section, NULL);

	POSITION pos = m_ItemMedianCache.GetStartPosition();
	int itemId = 0;
	CString median;
	while (pos != NULL)
	{
		m_ItemMedianCache.GetNextAssoc(pos, itemId, median);
		if (itemId <= 0)
			continue;

		CString key;
		key.Format(_T("%d"), itemId);
		m_pIni->SetValue(section, key, median);
	}
}

CString CLootBoxDlg::GetMedianCacheSection(const CString &server) const
{
	CString section;
	if (server.IsEmpty())
	{
		section = INI_FILE_FFXIAH_CACHE_SECTION;
	}
	else
	{
		section.Format(_T("%s_%s"), INI_FILE_FFXIAH_CACHE_SECTION, server);
	}

	return section;
}

#include "RegionSelect.h"
#include "ExportDlg.h"

#include "CsvWriter.h"

#include <shlobj.h>

#ifdef GDIPLUS_IMAGE_RESIZING
int GetEncoderClsid(const WCHAR* format, CLSID* pClsid);
#endif

BEGIN_MESSAGE_MAP(CLootBoxDlg, ETSLayoutDialog)
	ON_WM_DESTROY()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()

	ON_MESSAGE(MSG_SEARCH_CLOSE, OnSearchClose)
	ON_COMMAND_RANGE(ID_LANGUAGE_JAPANESE, ID_VIEW_COMPACTLISTING, OnOptionsChange)
	ON_COMMAND_RANGE(ID_SERVER_BAHAMUT, ID_SERVER_ASURA, OnOptionsChange)
	ON_COMMAND(ID_FILE_EXPORT, OnExport)
	ON_COMMAND(ID_FILE_SEARCH, OnSearch)
	ON_COMMAND(ID_FILE_QUIT, OnFileQuit)
	ON_COMMAND(ID_HELP_ABOUT, OnHelpAbout)
	ON_BN_CLICKED(IDC_FETCH_FFXIAH, OnFetchFfxiah)
	ON_NOTIFY(HDN_ITEMCLICK, IDC_CHAR_LIST, &CLootBoxDlg::OnListItemClick)
	ON_NOTIFY(LVN_ENDLABELEDIT, IDC_CHAR_LIST, &CLootBoxDlg::OnEndItemEdit)
	ON_NOTIFY(LVN_KEYDOWN, IDC_CHAR_LIST, &CLootBoxDlg::OnKeyDownListItem)
	ON_NOTIFY(HDN_ENDTRACK, IDC_INVENTORY_LIST, &CLootBoxDlg::OnInventoryColumnResize)
	ON_NOTIFY(TCN_SELCHANGE, IDC_INVENTORY_TABS, &CLootBoxDlg::OnInventoryTabsChange)
	ON_NOTIFY(LVN_ITEMCHANGED, IDC_CHAR_LIST, &CLootBoxDlg::OnCharactersListChange)
	ON_NOTIFY(NM_RCLICK, IDC_INVENTORY_TABS, &CLootBoxDlg::OnRightClickInventoryTab)
	ON_NOTIFY(NM_RCLICK, IDC_INVENTORY_LIST, &CLootBoxDlg::OnInventoryListRightClick)
	ON_NOTIFY(HDN_DIVIDERDBLCLICK, IDC_INVENTORY_LIST, &CLootBoxDlg::OnInventoryColumnAutosize)
	ON_NOTIFY(NM_DBLCLK, IDC_INVENTORY_LIST, &CLootBoxDlg::OnItemDoubleClick)
	ON_MESSAGE(WM_APP_MEDIAN_READY, &CLootBoxDlg::OnMedianReady)
	ON_MESSAGE(WM_APP_MEDIAN_PROGRESS, &CLootBoxDlg::OnMedianProgress)
	ON_COMMAND(ID_REFRESH_CLOSE, &CLootBoxDlg::OnRefreshClose)
END_MESSAGE_MAP()

BEGIN_MESSAGE_MAP(FFXiItemList, CListCtrl)
	ON_WM_CREATE()
	ON_NOTIFY_EX_RANGE(TTN_NEEDTEXTW, 0, 0xFFFF, OnToolTipText)
	ON_NOTIFY_EX_RANGE(TTN_NEEDTEXTA, 0, 0xFFFF, OnToolTipText)
	ON_NOTIFY(HDN_ITEMCLICK, 0, OnColumnSort)
END_MESSAGE_MAP()

// CLootBoxDlg dialog
CLootBoxDlg::CLootBoxDlg(CWnd* pParent)
	: ETSLayoutDialog(CLootBoxDlg::IDD, pParent)
{
	m_ItemsCount = m_CharactersCount = m_SelectedChar = m_SelectedTab = 0;
	m_pFileData = (BYTE*)malloc(DATA_SIZE_INVENTORY + 1);
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
	m_pIni = new CSimpleIni(true, false, false);
	m_Language = INI_FILE_LANGUAGE_VALUE;
	m_pItemIconList = m_pIconList = NULL;
	m_pPopMenu = m_pMainMenu = NULL;
	m_hAcceleratorTable = NULL;
	m_pIni->SetSpaces(false);
	m_CompactList = false;
	m_pSearchDlg = NULL;
	m_InitDone = false;
	m_pHelper = NULL;
	m_MedianThread = NULL;
	m_StopMedianThread = false;
	m_MedianTotal = 0;
	m_MedianCompleted = 0;
	m_PromptForServer = false;
}

void CLootBoxDlg::DoDataExchange(CDataExchange* pDX)
{
	DDX_Control(pDX, IDC_INVENTORY_LIST, m_InventoryList);
	ETSLayoutDialog::DoDataExchange(pDX);
}

BOOL CLootBoxDlg::OnInitDialog()
{
	ETSLayoutDialog::OnInitDialog();
	SetIcon(m_hIcon, TRUE);
	SetIcon(m_hIcon, FALSE);

	m_pMainMenu = new CMenu();
	m_pMainMenu->LoadMenu(IDR_MAIN_MENU);
	SetMenu(m_pMainMenu);

	m_pPopMenu = new CMenu();
	m_pPopMenu->LoadMenu(IDR_POP_INVTAB);

	// set the dialog layout
	if (InitLayout())
	{
		// load the configuration file
		if (DefaultConfig())
		{
			m_hAcceleratorTable = LoadAccelerators(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDR_ACCELERATOR_SEARCH));
			InitDialog();

			m_InitDone = true;
		}
	}

	return FALSE;  // return TRUE  unless you set the focus to a control
}

BOOL CLootBoxDlg::InitDialog()
{
	CTabCtrl *pTabCtrl = (CTabCtrl*)GetDlgItem(IDC_INVENTORY_TABS);
	FFXiItemList *pList = (FFXiItemList*)GetDlgItem(IDC_CHAR_LIST);
	CLootBoxApp *pApp = (CLootBoxApp*)AfxGetApp();
	const TCHAR *pFFXiPath = NULL;
	const TBYTE *pValue, *pKey;

	// load the configuration file
	if (DefaultConfig())
	{
		m_pHelper = new FFXiHelper(m_Region);

	// retrieve the installation path from the configuration file
	pFFXiPath = m_pIni->GetValue(INI_FILE_CONFIG_SECTION, INI_FILE_GAME_DIRECTORY);
	SetLanguageMenu(m_Language);
	SetServerMenu(m_FfxiahServer);

	if (m_PromptForServer)
	{
		CArray<CString, LPCTSTR> servers;
		for (int i = 0; i < (int)(sizeof(kServerMenuEntries) / sizeof(kServerMenuEntries[0])); i++)
			servers.Add(kServerMenuEntries[i].Name);

		ServerSelect selectDlg(servers, m_FfxiahServer, this);
		if (selectDlg.DoModal() == IDOK)
		{
			CString selected = selectDlg.GetSelectedServer();
			if (!selected.IsEmpty())
			{
				m_FfxiahServer = selected;
				m_pIni->SetValue(INI_FILE_CONFIG_SECTION, INI_FILE_FFXIAH_SERVER_KEY, m_FfxiahServer);
				SetServerMenu(m_FfxiahServer);
			}
		}

		m_PromptForServer = false;
	}

		if (pFFXiPath != NULL)
		{
			m_FFXiInstallPath = pFFXiPath;
		}
		else
		{
			// retrieve the FFXI installation path from the registry
			m_FFXiInstallPath = m_pHelper->GetInstallPath(m_Region);
			// reading the registry failed: pick a directory manually
			if (m_FFXiInstallPath.IsEmpty())
			{
				LPCITEMIDLIST pSelectedPIDL;
				BROWSEINFO BrowseInfo;
				LPTSTR pPathBuffer;

				AfxMessageBox(_T("Failed to read the Final Fantasy XI installation folder from the registry.\nPlease choose the Final Fantasy XI path manually."), MB_ICONSTOP);
				SecureZeroMemory(&BrowseInfo, sizeof(BrowseInfo));

				BrowseInfo.hwndOwner = GetSafeHwnd();
				BrowseInfo.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NONEWFOLDERBUTTON | BIF_USENEWUI;
				BrowseInfo.lpszTitle = _T("Please select the Final Fantasy XI installation folder");

				// select the directory manually
				pSelectedPIDL = SHBrowseForFolder(&BrowseInfo);
				pPathBuffer = m_FFXiInstallPath.GetBuffer(MAX_PATH);
				SHGetPathFromIDList(pSelectedPIDL, pPathBuffer);
				m_FFXiInstallPath.ReleaseBuffer();
			}
		}

		if (m_FFXiInstallPath.IsEmpty() == false)
		{
			CSimpleIni::TNamesDepend::const_reverse_iterator iKey;
			CString UserDataPath, CurrentUserID;
			CSimpleIni::TNamesDepend Keys;
			LONG Width, Height, X, Y;
			HICON hPolIco, hLootBox;
			int ImageIndex = 0;
			CFileFind Finder;
			UINT ItemIndex;

			m_FFXiInstallPath.TrimRight('\\');

			m_pIconList = new CImageList();

			m_pIni->SetValue(INI_FILE_CONFIG_SECTION, INI_FILE_GAME_DIRECTORY, m_FFXiInstallPath);
			m_pHelper->SetInstallPath(m_FFXiInstallPath);

			Width = m_pIni->GetLongValue(INI_FILE_CONFIG_SECTION, INI_FILE_WINDOW_WIDTH_KEY, INI_FILE_WINDOW_WIDTH_VALUE);
			Height = m_pIni->GetLongValue(INI_FILE_CONFIG_SECTION, INI_FILE_WINDOW_HEIGHT_KEY, INI_FILE_WINDOW_HEIGHT_VALUE);

			if (Width < INI_FILE_WINDOW_WIDTH_VALUE)
				Width = INI_FILE_WINDOW_WIDTH_VALUE;

			if (Height < INI_FILE_WINDOW_HEIGHT_VALUE)
				Height = INI_FILE_WINDOW_HEIGHT_VALUE;

			// center the dialog
			X = (LONG)(GetSystemMetrics(SM_CXSCREEN) - Width) / 2;
			Y = (LONG)(GetSystemMetrics(SM_CYSCREEN) - Height) / 2;
			MoveWindow(X, Y, Width, Height);

			// search string to enumerate the user IDs
			UserDataPath.Format(_T("%s\\%s\\*.*"), m_FFXiInstallPath, FFXI_PATH_USER_DATA);

			// add the PlayOnline icon to the image list
			if (m_pIconList->Create(16, 16, ILC_COLOR32 | ILC_MASK, 4, 1))
			{
				m_pIconList->Add(m_hIcon);
				hPolIco = pApp->LoadIcon(IDI_PLAYONLINE);
				m_pIconList->Add(hPolIco);
				hLootBox = pApp->LoadIcon(IDI_LOOTBOX);
				m_pIconList->Add(hLootBox);
				hLootBox = pApp->LoadIcon(IDI_SEARCH);
				m_pIconList->Add(hLootBox);
				pList->SetImageList(m_pIconList, LVSIL_SMALL);
			}

			// enumerate the inventory tabs
			if (m_pIni->GetAllKeys(INI_FILE_INVENTORY_SECTION, Keys))
			{
				LPTSTR pBuffer;
				TCITEM TabItem;
				CSize Size;

				Keys.sort(CSimpleIni::Entry::LoadOrder());
				pTabCtrl->SetImageList(m_pIconList);

				for (iKey = Keys.rbegin(); iKey != Keys.rend(); ++iKey)
				{
					pKey = iKey->pItem;

					pValue = m_pIni->GetValue(INI_FILE_INVENTORY_SECTION, pKey);

					if (pValue != NULL)
					{
						// add the tab
						ItemIndex = pTabCtrl->InsertItem(0, pValue, 2);
						// save the filename
						m_InventoryFiles.InsertAt(ItemIndex, pKey);
						m_InventoryNames.InsertAt(ItemIndex, pValue);
						// retrieve the buffer
						pBuffer = m_InventoryFiles.GetAt(ItemIndex).GetBuffer();
						// set the pointer as the item data
						TabItem.mask = TCIF_PARAM;
						TabItem.lParam = (DWORD_PTR)pBuffer;
						pTabCtrl->SetItem(ItemIndex, &TabItem);
					}
				}
			}

			// enumerate the user IDs
			if (Finder.FindFile(UserDataPath))
			{
				BOOL MoreData = TRUE;

				while (MoreData)
				{
					MoreData = Finder.FindNextFile();
					// look only for folders
					if (Finder.IsDirectory() && Finder.IsDots() == FALSE)
					{
						LPTSTR pBuffer;

						CurrentUserID = Finder.GetFileName();
						pValue = m_pIni->GetValue(INI_FILE_CHARACTERS_SECTION, CurrentUserID);

						if (pValue == NULL || _tcslen(pValue) == 0)
						{
							m_pIni->SetValue(INI_FILE_CHARACTERS_SECTION, CurrentUserID, _T(""));
							pValue = CurrentUserID.GetBuffer();
						}
						else if (pValue[0] == '@')
							continue;

						// add the user ID to the list
						ItemIndex = pList->InsertItem(0, pValue, 1);
						// retrieve the buffer and set the pointer as the item data
						m_CharacterIDs.InsertAt(ItemIndex, CurrentUserID);
						m_CharacterNames.InsertAt(ItemIndex, pValue);
						pBuffer = m_CharacterIDs.GetAt(ItemIndex).GetBuffer();
						pList->SetItemData(ItemIndex, (DWORD_PTR)pBuffer);
					}
				}

				Finder.Close();
			}

			//LoadGlobalMap();

			m_CharactersCount = (UINT)m_CharacterIDs.GetSize();
			m_SelectedChar = m_pIni->GetLongValue(INI_FILE_CONFIG_SECTION, INI_FILE_LAST_CHARACTER_KEY, 0);

			if (m_SelectedChar < 0)
				m_SelectedChar = 0;
			else if (m_CharactersCount > 0 && m_SelectedChar >= m_CharactersCount)
				m_SelectedChar = m_CharactersCount - 1;

			pList->SetFocus();
			pList->SetItemState(m_SelectedChar, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);

			m_SelectedTab = m_pIni->GetLongValue(INI_FILE_CONFIG_SECTION, INI_FILE_LAST_TAB_KEY);
			pTabCtrl->SetCurSel(m_SelectedTab);

			pList = (FFXiItemList*)GetDlgItem(IDC_INVENTORY_LIST);

			m_pItemIconList = new CImageList();
			m_pItemIconList->Create(LIST_ICON_SIZE, LIST_ICON_SIZE, ILC_COLOR32 | ILC_MASK, 0, 1);
			pList->SetImageList(m_pItemIconList, LVSIL_SMALL);
			m_CompactList = (m_pIni->GetLongValue(INI_FILE_CONFIG_SECTION, INI_FILE_COMPACT_LIST_KEY, 0) != 0);
			pList->SetCompactList(m_CompactList);
			SetCompactListMenu(m_CompactList);

			// enumerate the columns
			if (pList && m_pIni->GetAllKeys(INI_FILE_COLUMNS_SECTION, Keys))
			{
				Keys.sort(CSimpleIni::Entry::LoadOrder());

				for (iKey = Keys.rbegin(); iKey != Keys.rend(); ++iKey)
				{
					pKey = iKey->pItem;

					Width = m_pIni->GetLongValue(INI_FILE_COLUMNS_SECTION, pKey);

					if (pValue != NULL)
						pList->InsertColumn(0, pKey, LVCFMT_LEFT, Width);
				}

				pList->InsertColumn(INVENTORY_LIST_COL_LOCATION, _T("Location"), LVCFMT_LEFT, 0);

			pList->BringWindowToTop();
		}

		UpdateStatus();

		ShowWindow(SW_SHOWMAXIMIZED);
	}

		return TRUE;
	}
	else
		AfxMessageBox(_T("Failed to load the configuration file."), MB_ICONSTOP);

	return FALSE;
}

void CLootBoxDlg::LoadGlobalMap(bool Update)
{
	int FileCount, CharCount, ProgressBarStep;
	ItemLocationInfo LocationInfo;
	InventoryMap *pInvMap;
	LPCTSTR pChar, pFile;
	ItemArray *pItemMap;
	CString InvFile;

	FileCount = (int)m_InventoryFiles.GetCount();
	CharCount = (int)m_CharacterIDs.GetCount();
	ProgressBarStep = CharCount * FileCount;

	// Init progress bar
	m_ProgressDlg.Create(IDD_PROGRESS, this);
	m_ProgressDlg.m_Progress.SetRange(0, ProgressBarStep);
	m_ProgressDlg.m_Progress.SetStep(1);

	for (int CharIndex = 0; CharIndex < CharCount; CharIndex++)
	{
		pInvMap = NULL;

		pChar = m_CharacterNames.GetAt(CharIndex);
		m_GlobalMap.Lookup(CharIndex, pInvMap);

		if (pInvMap == NULL)
		{
			pInvMap = new InventoryMap;
			// add the ID to the global map
			m_GlobalMap.SetAt(CharIndex, pInvMap);
		}

		// add the inventory files for the current character
		for (int FileIndex = 0; FileIndex < FileCount; FileIndex++)
		{
			pItemMap = NULL;

			pFile = m_InventoryNames.GetAt(FileIndex);
			pInvMap->Lookup(FileIndex, pItemMap);

			if (pItemMap == NULL || Update)
			{
				if (pItemMap == NULL)
					pItemMap = new ItemArray();

				pChar = m_CharacterIDs.GetAt(CharIndex);
				pFile = m_InventoryFiles.GetAt(FileIndex);
				InvFile.Format(_T("%s\\USER\\%s\\%s"), m_FFXiInstallPath, pChar, pFile);

				pChar = m_CharacterNames.GetAt(CharIndex);
				pFile = m_InventoryNames.GetAt(FileIndex);
				LocationInfo.Location.Format(_T("%s: %s"), pChar, pFile);
				LocationInfo.Character = CharIndex;
				LocationInfo.InvTab = FileIndex;
				LocationInfo.ListIndex = 0;
				LocationInfo.ImageIndex = 0;

				m_pHelper->ParseInventoryFile(InvFile, LocationInfo, pItemMap, m_Language, Update);

				pInvMap->SetAt(FileIndex, pItemMap);
			}

			m_ProgressDlg.m_Progress.StepIt();
		}
	}

	m_ProgressDlg.DestroyWindow();
}

BOOL CLootBoxDlg::DefaultConfig()
{
	const CSimpleIni::TKeyVal *pSectionData;
	const TBYTE *pValue;
	LONG Value = 0;

	// check if the configuration file doesn't exist
	if (GetFileAttributes(INI_FILE_FILENAME) == MAXDWORD)
	{
		// create an empty file if it doesn't
		FILE *IniFile;

		_tfopen_s(&IniFile, INI_FILE_FILENAME, _T("wb"));

		if (IniFile != NULL)
			fclose(IniFile);
		else
			return FALSE;
	}

	if (m_pIni->LoadFile(INI_FILE_FILENAME) >= 0)
	{
		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// check if the Export section exists
		pSectionData = m_pIni->GetSection(INI_FILE_EXPORT_SECTION);
		// create it if it doesn't
		if (pSectionData == NULL)
			m_pIni->SetValue(INI_FILE_EXPORT_SECTION, NULL, NULL);

		// check if the Name key exists
		pValue = m_pIni->GetValue(INI_FILE_EXPORT_SECTION, INI_FILE_COL_NAME_KEY);
		// create it if it doesn't
		if (pValue == NULL)
			m_pIni->SetLongValue(INI_FILE_EXPORT_SECTION, INI_FILE_COL_NAME_KEY, 1L);

		// check if the Attribute key exists
		pValue = m_pIni->GetValue(INI_FILE_EXPORT_SECTION, INI_FILE_COL_ATTR_KEY);
		// create it if it doesn't
		if (pValue == NULL)
			m_pIni->SetLongValue(INI_FILE_EXPORT_SECTION, INI_FILE_COL_ATTR_KEY, 1L);

		// check if the Description key exists
		pValue = m_pIni->GetValue(INI_FILE_EXPORT_SECTION, INI_FILE_COL_DESC_KEY);
		// create it if it doesn't
		if (pValue == NULL)
			m_pIni->SetLongValue(INI_FILE_EXPORT_SECTION, INI_FILE_COL_DESC_KEY, 1L);

		// check if the Type key exists
		pValue = m_pIni->GetValue(INI_FILE_EXPORT_SECTION, INI_FILE_COL_TYPE_KEY);
		// create it if it doesn't
		if (pValue == NULL)
			m_pIni->SetLongValue(INI_FILE_EXPORT_SECTION, INI_FILE_COL_TYPE_KEY, 1L);

		// check if the Races key exists
		pValue = m_pIni->GetValue(INI_FILE_EXPORT_SECTION, INI_FILE_COL_RACES_KEY);
		// create it if it doesn't
		if (pValue == NULL)
			m_pIni->SetLongValue(INI_FILE_EXPORT_SECTION, INI_FILE_COL_RACES_KEY, 1L);

		// check if the Level key exists
		pValue = m_pIni->GetValue(INI_FILE_EXPORT_SECTION, INI_FILE_COL_LEVEL_KEY);
		// create it if it doesn't
		if (pValue == NULL)
			m_pIni->SetLongValue(INI_FILE_EXPORT_SECTION, INI_FILE_COL_LEVEL_KEY, 1L);

		// check if the Jobs key exists
		pValue = m_pIni->GetValue(INI_FILE_EXPORT_SECTION, INI_FILE_COL_JOBS_KEY);
		// create it if it doesn't
		if (pValue == NULL)
			m_pIni->SetLongValue(INI_FILE_EXPORT_SECTION, INI_FILE_COL_JOBS_KEY, 1L);

		// check if the Remarks key exists
		pValue = m_pIni->GetValue(INI_FILE_EXPORT_SECTION, INI_FILE_COL_REMARKS_KEY);
		// create it if it doesn't
		if (pValue == NULL)
			m_pIni->SetLongValue(INI_FILE_EXPORT_SECTION, INI_FILE_COL_REMARKS_KEY, 1L);

		// check if the BG Wiki key exists
		pValue = m_pIni->GetValue(INI_FILE_EXPORT_SECTION, INI_FILE_EXPORT_BG_URL_KEY);
		// create it if it doesn't
		if (pValue == NULL)
			m_pIni->SetLongValue(INI_FILE_EXPORT_SECTION, INI_FILE_EXPORT_BG_URL_KEY, 1L);

		// check if the Avg FFXIAH Price key exists
		pValue = m_pIni->GetValue(INI_FILE_EXPORT_SECTION, INI_FILE_EXPORT_MEDIAN_KEY);
		// create it if it doesn't
		if (pValue == NULL)
			m_pIni->SetLongValue(INI_FILE_EXPORT_SECTION, INI_FILE_EXPORT_MEDIAN_KEY, 1L);

		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// check if the Characters section exists
		pSectionData = m_pIni->GetSection(INI_FILE_CHARACTERS_SECTION);
		// create it if it doesn't
		if (pSectionData == NULL)
			m_pIni->SetValue(INI_FILE_CHARACTERS_SECTION, NULL, NULL);

		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// check if the Inventory section exists
		pSectionData = m_pIni->GetSection(INI_FILE_INVENTORY_SECTION);
		// create it if it doesn't
		if (pSectionData == NULL)
			m_pIni->SetValue(INI_FILE_INVENTORY_SECTION, NULL, NULL);

		// check if the "Inventory" key exists
		pValue = m_pIni->GetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_INVENTORY_KEY);
		// create it if it doesn't
		if (pValue == NULL)
			m_pIni->SetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_INVENTORY_KEY, INI_FILE_INVENTORY_VALUE);

		// check if the "Mog Safe" key exists
		pValue = m_pIni->GetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_SAFE_KEY);
		// create it if it doesn't
		if (pValue == NULL)
			m_pIni->SetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_SAFE_KEY, INI_FILE_MOG_SAFE_VALUE);

		// check if the "Mog Safe 2" key exists
		pValue = m_pIni->GetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_SAFE_2_KEY);
		// create it if it doesn't
		if (pValue == NULL)
			m_pIni->SetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_SAFE_2_KEY, INI_FILE_MOG_SAFE_2_VALUE);

		// check if the "Storage" key exists
		pValue = m_pIni->GetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_STORAGE_KEY);
		// create it if it doesn't
		if (pValue == NULL)
			m_pIni->SetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_STORAGE_KEY, INI_FILE_STORAGE_VALUE);

		// check if the "Mog Locker" key exists
		pValue = m_pIni->GetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_LOCKER_KEY);
		// create it if it doesn't
		if (pValue == NULL)
			m_pIni->SetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_LOCKER_KEY, INI_FILE_MOG_LOCKER_VALUE);

		// check if the "Mog Satchel" key exists
		pValue = m_pIni->GetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_SATCHEL_KEY);
		// create it if it doesn't
		if (pValue == NULL)
			m_pIni->SetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_SATCHEL_KEY, INI_FILE_MOG_SATCHEL_VALUE);

		// check if the "Mog Sack" key exists
		pValue = m_pIni->GetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_SACK_KEY);
		// create it if it doesn't
		if (pValue == NULL)
			m_pIni->SetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_SACK_KEY, INI_FILE_MOG_SACK_VALUE);

		// check if the "Mog Case" key exists
		pValue = m_pIni->GetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_CASE_KEY);
		// create it if it doesn't
		if (pValue == NULL)
			m_pIni->SetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_CASE_KEY, INI_FILE_MOG_CASE_VALUE);

		// check if the "Mog Wardrobe" key exists
		pValue = m_pIni->GetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_WARDROBE_KEY);
		// create it if it doesn't
		if (pValue == NULL)
			m_pIni->SetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_WARDROBE_KEY, INI_FILE_MOG_WARDROBE_VALUE);

		// check if the "Mog Wardrobe 2" key exists
		pValue = m_pIni->GetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_WARDROBE_2_KEY);
		// create it if it doesn't
		if (pValue == NULL)
			m_pIni->SetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_WARDROBE_2_KEY, INI_FILE_MOG_WARDROBE_2_VALUE);

		// check if the "Mog Wardrobe 3" key exists
		pValue = m_pIni->GetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_WARDROBE_3_KEY);
		// create it if it doesn't
		if (pValue == NULL)
			m_pIni->SetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_WARDROBE_3_KEY, INI_FILE_MOG_WARDROBE_3_VALUE);

		// check if the "Mog Wardrobe 4" key exists
		pValue = m_pIni->GetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_WARDROBE_4_KEY);
		// create it if it doesn't
		if (pValue == NULL)
			m_pIni->SetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_WARDROBE_4_KEY, INI_FILE_MOG_WARDROBE_4_VALUE);

		// check if the "Mog Wardrobe 5" key exists
		pValue = m_pIni->GetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_WARDROBE_5_KEY);
		// create it if it doesn't
		if (pValue == NULL)
			m_pIni->SetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_WARDROBE_5_KEY, INI_FILE_MOG_WARDROBE_5_VALUE);

		// check if the "Mog Wardrobe 6" key exists
		pValue = m_pIni->GetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_WARDROBE_6_KEY);
		// create it if it doesn't
		if (pValue == NULL)
			m_pIni->SetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_WARDROBE_6_KEY, INI_FILE_MOG_WARDROBE_6_VALUE);

		// check if the "Mog Wardrobe 7" key exists
		pValue = m_pIni->GetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_WARDROBE_7_KEY);
		// create it if it doesn't
		if (pValue == NULL)
			m_pIni->SetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_WARDROBE_7_KEY, INI_FILE_MOG_WARDROBE_7_VALUE);

		// check if the "Mog Wardrobe 8" key exists
		pValue = m_pIni->GetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_WARDROBE_8_KEY);
		// create it if it doesn't
		if (pValue == NULL)
			m_pIni->SetValue(INI_FILE_INVENTORY_SECTION, INI_FILE_MOG_WARDROBE_8_KEY, INI_FILE_MOG_WARDROBE_8_VALUE);

		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// check if the Columns section exists
		pSectionData = m_pIni->GetSection(INI_FILE_COLUMNS_SECTION);
		// create it if it doesn't
		if (pSectionData == NULL)
			m_pIni->SetValue(INI_FILE_COLUMNS_SECTION, NULL, NULL);

		// check if the Name key exists
		pValue = m_pIni->GetValue(INI_FILE_COLUMNS_SECTION, INI_FILE_COL_NAME_KEY);
		// create it if it doesn't
		if (pValue == NULL)
			m_pIni->SetValue(INI_FILE_COLUMNS_SECTION, INI_FILE_COL_NAME_KEY, INI_FILE_COL_NAME_VALUE);

		// check if the Attr key exists
		pValue = m_pIni->GetValue(INI_FILE_COLUMNS_SECTION, INI_FILE_COL_ATTR_KEY);
		// create it if it doesn't
		if (pValue == NULL)
			m_pIni->SetValue(INI_FILE_COLUMNS_SECTION, INI_FILE_COL_ATTR_KEY, INI_FILE_COL_ATTR_VALUE);

		// check if the Description key exists
		pValue = m_pIni->GetValue(INI_FILE_COLUMNS_SECTION, INI_FILE_COL_DESC_KEY);
		// create it if it doesn't
		if (pValue == NULL)
			m_pIni->SetValue(INI_FILE_COLUMNS_SECTION, INI_FILE_COL_DESC_KEY, INI_FILE_COL_DESC_VALUE);

		// check if the Type key exists
		pValue = m_pIni->GetValue(INI_FILE_COLUMNS_SECTION, INI_FILE_COL_TYPE_KEY);
		// create it if it doesn't
		if (pValue == NULL)
			m_pIni->SetValue(INI_FILE_COLUMNS_SECTION, INI_FILE_COL_TYPE_KEY, INI_FILE_COL_TYPE_VALUE);

		// check if the Races key exists
		pValue = m_pIni->GetValue(INI_FILE_COLUMNS_SECTION, INI_FILE_COL_RACES_KEY);
		// create it if it doesn't
		if (pValue == NULL)
			m_pIni->SetValue(INI_FILE_COLUMNS_SECTION, INI_FILE_COL_RACES_KEY, INI_FILE_COL_RACES_VALUE);

		// check if the Level key exists
		pValue = m_pIni->GetValue(INI_FILE_COLUMNS_SECTION, INI_FILE_COL_LEVEL_KEY);
		// create it if it doesn't
		if (pValue == NULL)
			m_pIni->SetValue(INI_FILE_COLUMNS_SECTION, INI_FILE_COL_LEVEL_KEY, INI_FILE_COL_LEVEL_VALUE);

		// check if the Jobs key exists
		pValue = m_pIni->GetValue(INI_FILE_COLUMNS_SECTION, INI_FILE_COL_JOBS_KEY);
		// create it if it doesn't
		if (pValue == NULL)
			m_pIni->SetValue(INI_FILE_COLUMNS_SECTION, INI_FILE_COL_JOBS_KEY, INI_FILE_COL_JOBS_VALUE);

		// check if the Remarks key exists
		pValue = m_pIni->GetValue(INI_FILE_COLUMNS_SECTION, INI_FILE_COL_REMARKS_KEY);
		// create it if it doesn't
		if (pValue == NULL)
			m_pIni->SetValue(INI_FILE_COLUMNS_SECTION, INI_FILE_COL_REMARKS_KEY, INI_FILE_COL_REMARKS_VALUE);

		// check if the Median key exists
		pValue = m_pIni->GetValue(INI_FILE_COLUMNS_SECTION, INI_FILE_COL_MEDIAN_KEY);
		// create it if it doesn't
		if (pValue == NULL)
			m_pIni->SetValue(INI_FILE_COLUMNS_SECTION, INI_FILE_COL_MEDIAN_KEY, INI_FILE_COL_MEDIAN_VALUE);

		// migrate old Median column name to the new label
		const TCHAR *pOldMedian = m_pIni->GetValue(INI_FILE_COLUMNS_SECTION, _T("Median"));
		if (pOldMedian != NULL)
		{
			if (pValue == NULL)
				m_pIni->SetValue(INI_FILE_COLUMNS_SECTION, INI_FILE_COL_MEDIAN_KEY, pOldMedian);

			m_pIni->Delete(INI_FILE_COLUMNS_SECTION, _T("Median"));
		}

		// remove the legacy URL key if present
		pValue = m_pIni->GetValue(INI_FILE_COLUMNS_SECTION, _T("URL"));
		if (pValue != NULL)
			m_pIni->Delete(INI_FILE_COLUMNS_SECTION, _T("URL"));

		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// check if the Config section exists
		pSectionData = m_pIni->GetSection(INI_FILE_CONFIG_SECTION);
		// create it if it doesn't
		if (pSectionData == NULL)
			m_pIni->SetValue(INI_FILE_CONFIG_SECTION, NULL, NULL);

		// check if the Language key exists
		m_Region = m_pIni->GetLongValue(INI_FILE_CONFIG_SECTION, INI_FILE_GAME_REGION_KEY, -1);
		// create it if it doesn't
		if (m_Region == -1)
		{
			int RegionCount, Regions = 0;

			m_Region = INI_FILE_GAME_REGION_VALUE;
			RegionCount = m_pHelper->DetectGameRegion(Regions);

			if (RegionCount > 1)
			{
				RegionSelect RegionSelectDlg(Regions);

				if (RegionSelectDlg.DoModal() == IDOK)
					m_Region = RegionSelectDlg.GetSelectedRegion();
			}

			m_pIni->SetLongValue(INI_FILE_CONFIG_SECTION, INI_FILE_GAME_REGION_KEY, m_Region, INI_FILE_GAME_REGION_COMMENT);
		}

		// check if the Region key exists
		m_Language = m_pIni->GetLongValue(INI_FILE_CONFIG_SECTION, INI_FILE_LANGUAGE_KEY, -1);
		// create it if it doesn't
		if (m_Language == -1)
		{
			m_pIni->SetLongValue(INI_FILE_CONFIG_SECTION, INI_FILE_LANGUAGE_KEY, INI_FILE_LANGUAGE_VALUE, INI_FILE_LANGUAGE_COMMENT);
			m_Language = INI_FILE_LANGUAGE_VALUE;
		}

		// check if the FFXIAH server key exists
		pValue = m_pIni->GetValue(INI_FILE_CONFIG_SECTION, INI_FILE_FFXIAH_SERVER_KEY);
		if (pValue == NULL)
		{
			m_pIni->SetValue(INI_FILE_CONFIG_SECTION, INI_FILE_FFXIAH_SERVER_KEY, INI_FILE_FFXIAH_SERVER_VALUE);
			m_FfxiahServer = INI_FILE_FFXIAH_SERVER_VALUE;
			m_PromptForServer = true;
		}
		else
		{
			m_FfxiahServer = pValue;
		}

		LoadMedianCacheFromIni(m_FfxiahServer);

		// check if the Width key exists
		pValue = m_pIni->GetValue(INI_FILE_CONFIG_SECTION, INI_FILE_WINDOW_WIDTH_KEY);
		// create it if it doesn't
		if (pValue == NULL)
			m_pIni->SetLongValue(INI_FILE_CONFIG_SECTION, INI_FILE_WINDOW_WIDTH_KEY, INI_FILE_WINDOW_WIDTH_VALUE);

		// check if the Height key exists
		pValue = m_pIni->GetValue(INI_FILE_CONFIG_SECTION, INI_FILE_WINDOW_HEIGHT_KEY);
		// create it if it doesn't
		if (pValue == NULL)
			m_pIni->SetLongValue(INI_FILE_CONFIG_SECTION, INI_FILE_WINDOW_HEIGHT_KEY, INI_FILE_WINDOW_HEIGHT_VALUE);

		// check if the SelectedChar key exists
		pValue = m_pIni->GetValue(INI_FILE_CONFIG_SECTION, INI_FILE_LAST_CHARACTER_KEY);
		// create it if it doesn't
		if (pValue == NULL)
			m_pIni->SetLongValue(INI_FILE_CONFIG_SECTION, INI_FILE_LAST_CHARACTER_KEY, INI_FILE_LAST_CHARACTER_VALUE);

		// check if the SelectedTab key exists
		pValue = m_pIni->GetValue(INI_FILE_CONFIG_SECTION, INI_FILE_LAST_TAB_KEY);
		// create it if it doesn't
		if (pValue == NULL)
			m_pIni->SetLongValue(INI_FILE_CONFIG_SECTION, INI_FILE_LAST_TAB_KEY, INI_FILE_LAST_TAB_VALUE);
		// check if the SelectedTab key exists
		pValue = m_pIni->GetValue(INI_FILE_CONFIG_SECTION, INI_FILE_COMPACT_LIST_KEY);
		// create it if it doesn't
		if (pValue == NULL)
			m_pIni->SetLongValue(INI_FILE_CONFIG_SECTION, INI_FILE_COMPACT_LIST_KEY, INI_FILE_COMPACT_LIST_VALUE);

		m_pIni->SaveFile(INI_FILE_FILENAME);

		return TRUE;
	}

	return FALSE;
}

BOOL CLootBoxDlg::InitLayout()
{
	CTabCtrl *pTabCtrl = (CTabCtrl*)GetDlgItem(IDC_INVENTORY_TABS);
	CPane TopLeftPane, TopRightPane, BottomPane, TopPane, TopRightHeader, TopRightTabs;

	CreateRoot(VERTICAL, 2, 8);

	TopLeftPane = new Pane(this, VERTICAL);
	TopLeftPane->addItem(IDC_CHARACTERS_LABEL, NORESIZE | ALIGN_LEFT);
	TopLeftPane->addItem(IDC_CHAR_LIST, ABSOLUTE_HORZ);

	TopRightPane = new Pane(this, VERTICAL);
	TopRightHeader = new Pane(this, HORIZONTAL);
	TopRightHeader->addItem(IDC_FETCH_FFXIAH, NORESIZE | ALIGN_RIGHT);
	TopRightPane->addPane(TopRightHeader, NORESIZE, 0);

	TopRightTabs = new PaneTab(pTabCtrl, this, VERTICAL);
	TopRightTabs->addItem(IDC_INVENTORY_LIST);
	TopRightPane->addPane(TopRightTabs, GREEDY, 0);

	TopPane = new Pane(this, HORIZONTAL);

	TopPane->addPane(TopLeftPane, NORESIZE | ALIGN_LEFT, 0);
	TopPane->addPane(TopRightPane, GREEDY, 0);

	BottomPane = new Pane(this, HORIZONTAL);
	BottomPane->addItem(IDC_STATUS_BAR, ABSOLUTE_VERT | ALIGN_LEFT);
	BottomPane->addItem(IDC_ITEM_COUNT, GREEDY | ALIGN_RIGHT);

	m_RootPane->addPane(TopPane, GREEDY, 0);
	m_RootPane->addPane(BottomPane, ABSOLUTE_VERT, 0);

	UpdateLayout();

	return TRUE;
}

// prevent the dialog from closing when the user presses ESC
BOOL CLootBoxDlg::PreTranslateMessage(MSG* pMsg)
{
	if (m_hAcceleratorTable != NULL && TranslateAccelerator(m_hWnd, m_hAcceleratorTable, pMsg))
	{
		return TRUE;
	}

	switch (pMsg->message)
	{
		case WM_KEYDOWN:
		{
			switch (pMsg->wParam)
			{
				case VK_F1:
					OnHelpAbout();
					return TRUE;
				case VK_ESCAPE:
				case VK_CANCEL:
					::DispatchMessage(pMsg);

					return TRUE;
					break;
			}
		}
		break;

		case WM_MBUTTONDOWN:
		{
			CTabCtrl *pInvTab = (CTabCtrl*)GetDlgItem(IDC_INVENTORY_TABS);

			if (pInvTab)
			{
				CPoint MousePos = pMsg->pt;
				TCHITTESTINFO HitInfo;
				int TabIndex = -1;

				SecureZeroMemory(&HitInfo, sizeof(HitInfo));

				pInvTab->ScreenToClient(&MousePos);

				HitInfo.pt = MousePos;
				TabIndex = pInvTab->HitTest(&HitInfo);

				if (TabIndex >= m_InventoryFiles.GetCount())
				{
					CTabCtrl *pTabList = (CTabCtrl*)GetDlgItem(IDC_INVENTORY_TABS);
					SearchData *pData;

					pInvTab->DeleteItem(TabIndex);

					m_SearchTabs.Lookup(TabIndex, pData);

					if (pData)
					{
						if (pData->pParams != NULL)
						{
							if (pData->pParams->pSearchTerm != NULL)
							{
								delete pData->pParams->pSearchTerm;
								pData->pParams->pSearchTerm = NULL;
							}

							delete pData->pParams;
							pData->pParams = NULL;
						}

						delete pData;
						pData = NULL;
					}

					m_SearchTabs.RemoveKey(TabIndex);

					m_SelectedTab = TabIndex - 1;
					pTabList->SetCurSel(m_SelectedTab);

					UpdateStatus();
				}
			}
		}
		break;
	}

	return ETSLayoutDialog::PreTranslateMessage(pMsg);
}

// cleanup on dialog destruction
void CLootBoxDlg::OnDestroy()
{
	m_StopMedianThread = true;
	FFXiItemList* pInvList = (FFXiItemList*)GetDlgItem(IDC_INVENTORY_LIST);
	CTabCtrl *pTabCtrl = (CTabCtrl*)GetDlgItem(IDC_INVENTORY_TABS);
	CListCtrl* pList = (CListCtrl*)GetDlgItem(IDC_CHAR_LIST);
	LONG Width, Height, SelectedChar, SelectedTab;
	int CharCount, FileCount, TabID;
	SearchData *pData = NULL;
	POSITION MapPos;
	RECT WindowRect;

	GetWindowRect(&WindowRect);
	Width = WindowRect.right - WindowRect.left;
	Height = WindowRect.bottom - WindowRect.top;
	SelectedChar = pList->GetNextItem(-1, LVNI_SELECTED);
	SelectedTab = pTabCtrl->GetCurSel();
	CharCount = (int)m_CharacterIDs.GetCount();
	FileCount = (int)m_InventoryFiles.GetCount();

	if (SelectedChar < 0)
		SelectedChar = 0;
	if (SelectedChar >= CharCount)
		SelectedTab = CharCount - 1;

	if (SelectedTab < 0)
		SelectedTab = 0;
	if (SelectedTab >= FileCount && FileCount > 0)
		SelectedTab = FileCount - 1;


	m_pIni->SetLongValue(INI_FILE_CONFIG_SECTION, INI_FILE_WINDOW_WIDTH_KEY, Width);
	m_pIni->SetLongValue(INI_FILE_CONFIG_SECTION, INI_FILE_WINDOW_HEIGHT_KEY, Height);
	m_pIni->SetLongValue(INI_FILE_CONFIG_SECTION, INI_FILE_LAST_TAB_KEY, SelectedTab);
	m_pIni->SetLongValue(INI_FILE_CONFIG_SECTION, INI_FILE_LAST_CHARACTER_KEY, SelectedChar);

	pInvList->DeleteAllItems();
	DeleteGlobalMap();

	MapPos = m_SearchTabs.GetStartPosition();

	while (MapPos != NULL)
	{
		m_SearchTabs.GetNextAssoc(MapPos, TabID, pData);

		if (pData != NULL)
		{
			if (pData->pParams != NULL)
			{
				if (pData->pParams->pSearchTerm != NULL)
				{
					delete pData->pParams->pSearchTerm;
					pData->pParams->pSearchTerm = NULL;
				}

				delete pData->pParams;
				pData->pParams = NULL;
			}

			delete pData;
			pData = NULL;
		}
	}

	if (m_pFileData != NULL)
	{
		free(m_pFileData);
		m_pFileData = NULL;
	}

	if (m_pIconList != NULL)
	{
		delete(m_pIconList);
		m_pIconList = NULL;
	}

	if (m_pItemIconList != NULL)
	{
		delete(m_pItemIconList);
		m_pItemIconList = NULL;
	}

	if (m_pMainMenu != NULL)
	{
		delete m_pMainMenu;
		m_pMainMenu = NULL;
	}

	if (m_pPopMenu != NULL)
	{
		delete m_pPopMenu;
		m_pPopMenu = NULL;
	}

	if (m_pIni != NULL)
	{
		SaveMedianCacheToIni(m_FfxiahServer);
	m_pIni->SaveFile(INI_FILE_FILENAME);

		delete(m_pIni);
		m_pIni = NULL;
	}

	if (m_pSearchDlg != NULL)
	{
		delete m_pSearchDlg;
		m_pSearchDlg = NULL;
	}

	if (m_pHelper != NULL)
	{
		delete(m_pHelper);
		m_pHelper = NULL;
	}

	ETSLayoutDialog::OnDestroy();
}

int CLootBoxDlg::GlobalMapCount()
{
	int CharID, FileID, Result = 0;
	POSITION GlobalPos, InvPos;
	InventoryMap *pInvMap;
	ItemArray *pItemArr;

	GlobalPos = m_GlobalMap.GetStartPosition();
	pItemArr = NULL;
	pInvMap = NULL;

	while (GlobalPos != NULL)
	{
		m_GlobalMap.GetNextAssoc(GlobalPos, CharID, pInvMap);

		if (pInvMap != NULL)
		{
			InvPos = pInvMap->GetStartPosition();

			while (InvPos != NULL)
			{
				pInvMap->GetNextAssoc(InvPos, FileID, pItemArr);

				if (pItemArr != NULL)
					Result += (int)pItemArr->GetCount();
			}
		}
	}

	return Result;
}

void CLootBoxDlg::DeleteGlobalMap()
{
	POSITION GlobalPos, InvPos, ItemPos;
	int CharID, FileID, ItemID;
	CString InvKey, CharKey;
	InventoryMap *pInvMap;
	InventoryItem *pItem;
	ItemArray *pItemArr;

	GlobalPos = m_GlobalMap.GetStartPosition();
	pItemArr = NULL;
	pInvMap = NULL;

	while (GlobalPos != NULL)
	{
		m_GlobalMap.GetNextAssoc(GlobalPos, CharID, pInvMap);

		if (pInvMap != NULL)
		{
			InvPos = pInvMap->GetStartPosition();

			while (InvPos != NULL)
			{
				pInvMap->GetNextAssoc(InvPos, FileID, pItemArr);

				if (pItemArr != NULL)
				{
					ItemPos = pItemArr->GetStartPosition();

					while (ItemPos != NULL)
					{
						pItemArr->GetNextAssoc(ItemPos, ItemID, pItem);

						if (pItem != NULL)
						{
							if (pItem->hBitmap != NULL)
								DeleteObject(pItem->hBitmap);

							if (pItem != NULL)
							{
								delete(pItem);
								pItem = NULL;
							}
						}
					}

					delete pItemArr;
					pItemArr = NULL;
				}
			}

			delete pInvMap;
			pInvMap = NULL;
		}
	}

	m_GlobalMap.RemoveAll();
}

// If you add a minimize button to your dialog, you will need the code below
//  to draw the icon.  For MFC applications using the document/view model,
//  this is automatically done for you by the framework.

void CLootBoxDlg::OnPaint()
{
	if (IsIconic())
	{
		CPaintDC dc(this); // device context for painting

		SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

		// Center icon in client rectangle
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// Draw the icon
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		ETSLayoutDialog::OnPaint();
	}
}

// The system calls this function to obtain the cursor to display while the user drags
//  the minimized window.
HCURSOR CLootBoxDlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}

// rename a list item on left click
void CLootBoxDlg::OnListItemClick(NMHDR *pNMHDR, LRESULT *pResult)
{
	CListCtrl *pList = (CListCtrl*)GetDlgItem(IDC_CHAR_LIST);
	LPNMHEADER phdr = (LPNMHEADER)pNMHDR;

	// edit the item
	pList->EditLabel(phdr->iItem);

	*pResult = 0;
}

//void CLootBoxDlg::OnInvListItemClick(NMHDR *pNMHDR, LRESULT *pResult)
//{
//	FFXiItemList *pInvList = (FFXiItemList*)GetDlgItem(IDC_INVENTORY_LIST);
//	LPNMHEADER phdr = (LPNMHEADER)pNMHDR;
//
//	TRACE(_T("Item data %0x\n"), pInvList->GetItemData(phdr->iItem));
//
//	*pResult = 0;
//}

// process key presses on list items
void CLootBoxDlg::OnKeyDownListItem(NMHDR *pNMHDR, LRESULT *pResult)
{
	CListCtrl *pList = (CListCtrl*)GetDlgItem(IDC_CHAR_LIST);
	LPNMLVKEYDOWN pLVKeyDow = (LPNMLVKEYDOWN)pNMHDR;

	// rename the current item when F2 is pressed
	if (pLVKeyDow->wVKey == VK_F2)
	{
		// retrieve the selected item
		int ItemIndex = pList->GetSelectionMark();
		// edit the item
		pList->EditLabel(ItemIndex);
	}

	*pResult = 0;
}


// commit the changes when the edit mode ends
void CLootBoxDlg::OnEndItemEdit(NMHDR *pNMHDR, LRESULT *pResult)
{
	CListCtrl *pList = (CListCtrl*)GetDlgItem(IDC_CHAR_LIST);
	NMLVDISPINFO *pDispInfo = (NMLVDISPINFO*)pNMHDR;
	LPTSTR ItemData = (LPTSTR)pList->GetItemData(pDispInfo->item.iItem);

	// if the text is NULL, the user canceled: do nothing
	if (pDispInfo->item.pszText != NULL)
	{
		// if the text is empty
		if (_tcslen(pDispInfo->item.pszText) == 0)
		{
			// write an empty value in the INI file
			m_pIni->SetValue(INI_FILE_CHARACTERS_SECTION, ItemData, _T(""));
			// reset the item to the default value
			pList->SetItem(pDispInfo->item.iItem, pDispInfo->item.iSubItem, LVIF_TEXT, ItemData, 0, 0, 0, 0);
		}
		else
		{
			// update the INI file with the new value
			m_pIni->SetValue(INI_FILE_CHARACTERS_SECTION, ItemData, pDispInfo->item.pszText);
			// update the item
			pList->SetItem(pDispInfo->item.iItem, pDispInfo->item.iSubItem, LVIF_TEXT, pDispInfo->item.pszText, 0, 0, 0, 0);
		}
	}

	*pResult = 0;
}
void CLootBoxDlg::OnInventoryColumnResize(NMHDR *pNMHDR, LRESULT *pResult)
{
	FFXiItemList *pList = (FFXiItemList*)GetDlgItem(IDC_INVENTORY_LIST);
	NMHEADER* phdr = (NMHEADER*)pNMHDR;
	TCHAR pHeader[16];

	// if this item mask indicates a change in width, then cxy holds that changed value
	if (phdr->pitem->mask & HDI_WIDTH)
	{
		LVCOLUMN Column;

		Column.mask = LVCF_TEXT;
		Column.cchTextMax = 16;
		Column.pszText = pHeader;
		pList->GetColumn(phdr->iItem, &Column);

		// update the INI file with the new value
		m_pIni->SetLongValue(INI_FILE_COLUMNS_SECTION, Column.pszText, phdr->pitem->cxy);
	}

	*pResult = 0;
}

void CLootBoxDlg::OnInventoryColumnAutosize(NMHDR *pNMHDR, LRESULT *pResult)
{
	FFXiItemList *pList = (FFXiItemList*)GetDlgItem(IDC_INVENTORY_LIST);
	NMHEADER* phdr = (NMHEADER*)pNMHDR;
	TCHAR pHeader[16];

	if (phdr->iItem)
	{
		LVCOLUMN Column;

		Column.mask = LVCF_WIDTH | LVCF_TEXT;
		Column.cchTextMax = 16;
		Column.pszText = pHeader;
		pList->GetColumn(phdr->iItem, &Column);
		// update the INI file with the new value
		m_pIni->SetLongValue(INI_FILE_COLUMNS_SECTION, Column.pszText, Column.cx);
	}

	*pResult = 0;
}

void CLootBoxDlg::OnInventoryTabsChange(NMHDR *pNMHDR, LRESULT *pResult)
{
	FFXiItemList *pList = (FFXiItemList*)GetDlgItem(IDC_INVENTORY_LIST);
	CTabCtrl *pTabList = (CTabCtrl*)GetDlgItem(IDC_INVENTORY_TABS);

	if (pTabList)
	{
		m_SelectedTab = pTabList->GetCurSel();

		if (IsInventoryTab())
			pList->SetColumnWidth(INVENTORY_LIST_COL_LOCATION, 0);
		else
			pList->SetColumnWidth(INVENTORY_LIST_COL_LOCATION, 150);

		UpdateStatus();
	}

	*pResult = 0;
}

void CLootBoxDlg::OnItemDoubleClick(NMHDR *pNMHDR, LRESULT *pResult)
{
	FFXiItemList *pList = (FFXiItemList*)GetDlgItem(IDC_INVENTORY_LIST);
	CTabCtrl *pTabList = (CTabCtrl*)GetDlgItem(IDC_INVENTORY_TABS);
	CListCtrl *pCharList = (CListCtrl*)GetDlgItem(IDC_CHAR_LIST);
	NMITEMACTIVATE* pItemActivate = (NMITEMACTIVATE*)pNMHDR;
	InventoryItem *pItem;

	int ItemIndex = (pItemActivate != NULL) ? pItemActivate->iItem : -1;
	if (pList && ItemIndex < 0)
		ItemIndex = pList->GetNextItem(-1, LVNI_SELECTED);

	pItem = (pList && ItemIndex >= 0) ? (InventoryItem*)pList->GetItemData(ItemIndex) : NULL;

	if (pItem && pItemActivate != NULL && pItemActivate->iSubItem == INVENTORY_LIST_COL_NAME)
		OpenBgWikiUrl(pItem);

	if (pList && pTabList && pCharList && IsInventoryTab() == false && pItem)
	{
		int EnsureIndex, ListIndex = 0;

		m_SelectedTab = pItem->LocationInfo.InvTab;
		m_SelectedChar = pItem->LocationInfo.Character;
		ListIndex = pItem->LocationInfo.ListIndex;

		if (m_SelectedChar != m_CharacterIDs.GetCount() - 1)
			EnsureIndex = m_SelectedChar + 1;
		else
			EnsureIndex = m_SelectedChar;

		pList->SetColumnWidth(INVENTORY_LIST_COL_LOCATION, 0);

		pCharList->SetItemState(m_SelectedChar, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		pCharList->EnsureVisible(EnsureIndex, FALSE);
		pTabList->SetCurSel(m_SelectedTab);

		UpdateStatus();

		if (ListIndex != pList->GetItemCount() - 1)
			EnsureIndex = ListIndex + 1;
		else
			EnsureIndex = ListIndex;

		pList->SetItemState(ListIndex, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		pList->EnsureVisible(EnsureIndex, FALSE);
	}

	*pResult = 0;
}

void CLootBoxDlg::OnInventoryListRightClick(NMHDR *pNMHDR, LRESULT *pResult)
{
	FFXiItemList *pList = (FFXiItemList*)GetDlgItem(IDC_INVENTORY_LIST);
	NMITEMACTIVATE* pItemActivate = (NMITEMACTIVATE*)pNMHDR;

	if (pList)
	{
		int ItemIndex = (pItemActivate != NULL) ? pItemActivate->iItem : -1;
		if (ItemIndex < 0)
			ItemIndex = pList->GetNextItem(-1, LVNI_SELECTED);

		if (ItemIndex >= 0)
		{
			CPoint MousePos;
			CMenu Menu;
			UINT Cmd = 0;
			const UINT OpenWikiCmd = 1;

			pList->SetItemState(ItemIndex, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
			GetCursorPos(&MousePos);

			Menu.CreatePopupMenu();
			Menu.AppendMenu(MF_STRING, OpenWikiCmd, _T("Open BG Wiki"));

			Cmd = Menu.TrackPopupMenu(TPM_RETURNCMD | TPM_RIGHTBUTTON, MousePos.x, MousePos.y, this);
			if (Cmd == OpenWikiCmd)
			{
				InventoryItem *pItem = (InventoryItem*)pList->GetItemData(ItemIndex);
				OpenBgWikiUrl(pItem);
			}
		}
	}

	*pResult = 0;
}

LRESULT CLootBoxDlg::OnMedianReady(WPARAM wParam, LPARAM lParam)
{
	MedianFetchResult *result = (MedianFetchResult*)lParam;
	if (result == NULL)
		return 0;

	m_ItemMedianCache.SetAt(result->ItemId, result->Median);
	m_ItemMedianPending.RemoveKey(result->ItemId);

	int listCount = m_InventoryList.GetItemCount();
	for (int i = 0; i < listCount; i++)
	{
		InventoryItem *pItem = (InventoryItem*)m_InventoryList.GetItemData(i);
		if (pItem != NULL && pItem->ItemHdr.ItemID == (DWORD)result->ItemId)
		{
			pItem->Median = result->Median;
			m_InventoryList.UpdateItemText(result->Median, i, INVENTORY_LIST_COL_MEDIAN);
		}
	}

	delete result;
	return 0;
}

LRESULT CLootBoxDlg::OnMedianProgress(WPARAM wParam, LPARAM lParam)
{
	MedianProgressUpdate *progress = (MedianProgressUpdate*)lParam;
	if (progress == NULL)
		return 0;

	CStatic *pStatus = (CStatic*)GetDlgItem(IDC_STATUS_BAR);
	if (pStatus)
	{
		if (progress->Completed < progress->Total)
		{
			CString status;
			status.Format(_T("%s  (Prices %d/%d)"), m_CurrentFile, progress->Completed, progress->Total);
			pStatus->SetWindowText(status);
		}
		else
		{
			pStatus->SetWindowText(m_CurrentFile);
		}
	}

	delete progress;
	return 0;
}

void CLootBoxDlg::OnRightClickInventoryTab(NMHDR *pNMHDR, LRESULT *pResult)
{
	CMenu *pMenu = m_pPopMenu->GetSubMenu(0);
	CPoint MousePos;

	GetCursorPos(&MousePos);

	if (IsInventoryTab() == false)
		pMenu->TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, MousePos.x, MousePos.y, this);

	*pResult = 0;
}

void CLootBoxDlg::OnCharactersListChange(NMHDR *pNMHDR, LRESULT *pResult)
{
	NM_LISTVIEW* pNMListView = (NM_LISTVIEW*)pNMHDR;

	if (m_InitDone && (pNMListView->uChanged & LVIF_STATE) && (pNMListView->uNewState & LVNI_SELECTED))
	{
		m_SelectedChar = pNMListView->iItem;

		if (IsInventoryTab())
			UpdateStatus();
	}

	*pResult = 0;
}

void CLootBoxDlg::UpdateStatus()
{
	CStatic *pStatus = (CStatic*)GetDlgItem(IDC_STATUS_BAR);

	if (pStatus && IsValidChar())
	{
		ItemArray *pItemList = NULL;

		if (IsInventoryTab())
		{
			m_CurrentFile.Format(_T("%s\\USER\\%s\\%s"), m_FFXiInstallPath,
				m_CharacterIDs.GetAt(m_SelectedChar),
				m_InventoryFiles.GetAt(m_SelectedTab));
		}
		else
		{
			m_CurrentFile = _T("");
		}

		pStatus->SetWindowText(m_CurrentFile);

		pItemList = GetItemMap(m_SelectedChar, m_SelectedTab);

		if (pItemList == NULL)
		{
			ItemLocationInfo Location;

			pItemList = new ItemArray;

			Location.ImageIndex = Location.ListIndex = 0;
			Location.Location.Format(_T("%s: %s"), m_CharacterNames.GetAt(m_SelectedChar),
				m_InventoryNames.GetAt(m_SelectedTab));
			Location.Character = m_SelectedChar;
			Location.InvTab = m_SelectedTab;

			m_pHelper->ParseInventoryFile(m_CurrentFile, Location, pItemList, m_Language);

			SetItemMapAt(m_SelectedChar, m_SelectedTab, pItemList);
		}

		RefreshList(pItemList);
	}
}

void CLootBoxDlg::SetItemMapAt(int SelectedCharIndex, int SelectedTabIndex, ItemArray *pItemList)
{
	InventoryMap *pInvMap = NULL;

	if (IsInventoryTab(SelectedTabIndex) && IsValidChar(SelectedCharIndex))
	{
		m_GlobalMap.Lookup(SelectedCharIndex, pInvMap);

		if (pInvMap == NULL)
		{
			pInvMap = new InventoryMap;
			m_GlobalMap.SetAt(SelectedCharIndex, pInvMap);
		}

		pInvMap->SetAt(SelectedTabIndex, pItemList);
	}
}

ItemArray* CLootBoxDlg::GetItemMap(int SelectedCharIndex, int SelectedTabIndex)
{
	InventoryMap *pInvMap = NULL;
	ItemArray *pItemList = NULL;

	if (IsInventoryTab(SelectedTabIndex) && IsValidChar(SelectedCharIndex))
	{
		m_GlobalMap.Lookup(SelectedCharIndex, pInvMap);

		if (pInvMap != NULL)
		{
			pInvMap->Lookup(SelectedTabIndex, pItemList);

			return pItemList;
		}
	}
	else
	{
		SearchData *pData = NULL;

		m_SearchTabs.Lookup(m_SelectedTab, pData);

		if (pData)
		{
			if (pData->Done == false)
				GetSearchResults(pData);

			return &pData->Items;
		}
	}

	return NULL;
}

BOOL CLootBoxDlg::RefreshList(const ItemArray *pItemList)
{
	FFXiItemList* pList = (FFXiItemList*)GetDlgItem(IDC_INVENTORY_LIST);
	int ImageIndex = 0, ItemIndex = 0, ImageCount, IconIndex = 0;
	CString ItemCountStr;
	CBitmap Bitmap;
	CFile InvFile;
	int ItemID;

	if (pList)
	{
		pList->DeleteAllItems();
		pList->BlockRedraw();

		ImageCount = m_pItemIconList->GetImageCount();
		m_ItemsCount = 0;

		if (pItemList != NULL)
		{
			InventoryItem *pItem;
			POSITION ItemPos;

			ItemPos = pItemList->GetStartPosition();

			while (ItemPos != NULL)
			{
				pItemList->GetNextAssoc(ItemPos, ItemID, pItem);

				if (pItem != NULL)
				{
					CString CachedMedian;
					if (m_ItemMedianCache.Lookup(pItem->ItemHdr.ItemID, CachedMedian))
						pItem->Median = CachedMedian;
					else
						pItem->Median = _T("0");

					if (pItem->hBitmap == NULL)
					{
						pItem->hBitmap = GetItemIcon(&pItem->IconInfo, GetDC(), LIST_ICON_SIZE, LIST_ICON_SIZE,
							(int)pItem->ItemHdr.ItemID);
					}

					if (pItem->hBitmap != NULL)
					{
						BOOL attachOk = Bitmap.Attach(pItem->hBitmap);
						if (!attachOk)
						{
						}
						else
						{
						if (IconIndex < ImageCount)
						{
							BOOL replaceOk = m_pItemIconList->Replace(IconIndex, &Bitmap, RGB(0, 0, 0));
							ImageIndex = IconIndex;
						}
						else
						{
							ImageIndex = m_pItemIconList->Add(&Bitmap, RGB(0, 0, 0));
						}

						Bitmap.Detach();
					}
					}

					pItem->LocationInfo.ImageIndex = ImageIndex;
					m_ItemsCount++;
					IconIndex++;

					if (m_CompactList && pItem->RefCount > 1)
					{
						CString ItemText;

						pList->AddItem(pItem, ItemIndex);
						// update the name of the item
						ItemText.Format(_T("%s (%d)"), pItem->ItemName, pItem->RefCount);
						pList->UpdateItemText(ItemText, ItemIndex - 1, 0);
						pItem->ItemToolTip.Format(_T("%d %s"), pItem->RefCount, pItem->LogName2);
					}
					else
					{
						pItem->ItemToolTip = pItem->LogName;

						for (int i = 0; i < pItem->RefCount; i++)
							pList->AddItem(pItem, ItemIndex);
					}
				}

			}
		}

		for (int i = m_ItemsCount; i < m_pItemIconList->GetImageCount();)
			m_pItemIconList->Remove(i);

		ItemCountStr.Format(_T("%d item(s)"), m_ItemsCount);
		pList->BlockRedraw(false);

		GetDlgItem(IDC_ITEM_COUNT)->SetWindowText(ItemCountStr);

		return TRUE;
	}

	return FALSE;
}

void CLootBoxDlg::StartMedianFetch(const ItemArray *pItemList)
{
	if (pItemList == NULL || m_FfxiahServer.IsEmpty())
		return;

	if (m_MedianThread != NULL)
		return;

	CMap<int, int, BOOL, BOOL> seen;
	CArray<int, int> ids;
	POSITION ItemPos = pItemList->GetStartPosition();
	InventoryItem *pItem = NULL;
	int ItemID = 0;

	while (ItemPos != NULL)
	{
		pItemList->GetNextAssoc(ItemPos, ItemID, pItem);
		if (pItem == NULL)
			continue;

		if (m_ItemMedianCache.Lookup(pItem->ItemHdr.ItemID, pItem->Median))
			continue;

		BOOL exists = FALSE;
		if (!seen.Lookup(pItem->ItemHdr.ItemID, exists))
		{
			seen.SetAt(pItem->ItemHdr.ItemID, TRUE);
			ids.Add(pItem->ItemHdr.ItemID);
		}
	}

	if (ids.GetCount() == 0)
		return;

	MedianFetchContext *ctx = new MedianFetchContext();
	ctx->Dlg = this;
	ctx->Server = m_FfxiahServer;
	ctx->ItemIds.Copy(ids);
	ctx->NextIndex = 0;

	m_StopMedianThread = false;
	m_MedianTotal = (int)ids.GetCount();
	m_MedianCompleted = 0;
	m_ItemMedianPending.RemoveAll();

	for (INT_PTR i = 0; i < ids.GetCount(); i++)
	{
		BOOL value = TRUE;
		m_ItemMedianPending.SetAt(ids[(int)i], value);
	}

	const int workerCount = 4;
	ctx->ActiveWorkers = workerCount;
	m_MedianThread = (CWinThread*)1;
	for (int i = 0; i < workerCount; i++)
		AfxBeginThread(MedianFetchThread, ctx, THREAD_PRIORITY_BELOW_NORMAL);
}

void CLootBoxDlg::SetLanguageMenu(int Language, bool Check)
{
	MENUITEMINFO MenuInfo;
	CMenu *pLangMenu;

	pLangMenu = m_pMainMenu->GetSubMenu(1)->GetSubMenu(0);

	if (pLangMenu)
	{
		SecureZeroMemory(&MenuInfo, sizeof(MENUITEMINFO));
		MenuInfo.fMask = MIIM_STATE;
		MenuInfo.fState = Check ? MFS_CHECKED : MFS_UNCHECKED;
		MenuInfo.cbSize = sizeof(MENUITEMINFO);

		pLangMenu->SetMenuItemInfo(Language - 1, &MenuInfo, TRUE);
	}
}

void CLootBoxDlg::SetServerMenu(const CString &serverName, bool Check)
{
	CMenu *pServerMenu = m_pMainMenu->GetSubMenu(1)->GetSubMenu(1);
	if (!pServerMenu)
		return;

	for (int i = 0; i < (int)(sizeof(kServerMenuEntries) / sizeof(kServerMenuEntries[0])); i++)
	{
		const ServerMenuEntry &entry = kServerMenuEntries[i];
		UINT state = (Check && serverName.CompareNoCase(entry.Name) == 0) ? MFS_CHECKED : MFS_UNCHECKED;
		pServerMenu->CheckMenuItem(entry.CmdId, MF_BYCOMMAND | state);
	}
}

void CLootBoxDlg::SetCompactListMenu(LONG CompactList)
{
	MENUITEMINFO MenuInfo;
	CMenu *pCompactListMenu;

	pCompactListMenu = m_pMainMenu->GetSubMenu(2);

	if (pCompactListMenu)
	{
		SecureZeroMemory(&MenuInfo, sizeof(MENUITEMINFO));
		MenuInfo.fMask = MIIM_STATE;
		MenuInfo.fState = (CompactList != 0) ? MFS_CHECKED : MFS_UNCHECKED;
		MenuInfo.cbSize = sizeof(MENUITEMINFO);

		pCompactListMenu->SetMenuItemInfo(0, &MenuInfo, TRUE);
	}
}

afx_msg void CLootBoxDlg::OnSearch()
{
	if (m_pSearchDlg == NULL)
		m_pSearchDlg = new SearchDialog(m_pHelper, this);

	if (m_pSearchDlg->m_hWnd == NULL)
		m_pSearchDlg->Create(IDD_SEARCH_DIALOG, this);
	else
		m_pSearchDlg->ShowWindow(SW_SHOW);

	m_pSearchDlg->CenterWindow();
}

afx_msg void CLootBoxDlg::OnExport()
{
	ExportDialog Dialog(m_pHelper, m_CharacterNames, m_pIni, this);

	if (Dialog.DoModal() == IDOK)
	{
		int ExportChars = Dialog.GetExportedCharsCount();
		int ColumnCount = Dialog.GetColumnCount() + 2;
		DWORD_PTR BitMask = Dialog.GetBitMask();

		if (ColumnCount > 0 && ExportChars > 0 && BitMask != 0UL)
		{
			CFileDialog SaveDialog(FALSE, _T("*.csv"), _T("export.csv"),
				OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY | OFN_NOCHANGEDIR
				| OFN_PATHMUSTEXIST | OFN_ENABLESIZING,
				_T("(*.csv) Excel 2000/XP||"), this);

			if (SaveDialog.DoModal() == IDOK)
			{
				const CArray<bool, bool> &ExportedChars = Dialog.GetExportedChars();
				int FileCount = m_InventoryFiles.GetCount();
				int CharCount = m_CharacterNames.GetCount();

				CsvWriter<CFile> Exporter;
				InventoryMap *pInvMap;
				InventoryItem *pItem;
				ItemArray *pItemMap;
				CString Filename;
				POSITION ItemPos;
				int ItemID;

				LoadGlobalMap();

				Filename = SaveDialog.GetPathName();

				if ((BitMask & EXPORT_MEDIAN) == EXPORT_MEDIAN)
				{
					if (!FetchMediansForExport(ExportedChars))
					{
						AfxMessageBox(_T("Export cancelled."));
						return;
					}
				}

				Exporter.CreateFile(Filename, ColumnCount);

				Exporter.AddColumn(_T("Character"))
					.AddColumn(_T("Location"));

				if ((BitMask & EXPORT_NAME) == EXPORT_NAME)
					Exporter.AddColumn(INI_FILE_COL_NAME_KEY);

				if ((BitMask & EXPORT_ATTR) == EXPORT_ATTR)
					Exporter.AddColumn(INI_FILE_COL_ATTR_KEY);

				if ((BitMask & EXPORT_DESC) == EXPORT_DESC)
					Exporter.AddColumn(INI_FILE_COL_DESC_KEY);

				if ((BitMask & EXPORT_TYPE) == EXPORT_TYPE)
					Exporter.AddColumn(INI_FILE_COL_TYPE_KEY);

				if ((BitMask & EXPORT_RACES) == EXPORT_RACES)
					Exporter.AddColumn(INI_FILE_COL_RACES_KEY);

				if ((BitMask & EXPORT_LEVEL) == EXPORT_LEVEL)
					Exporter.AddColumn(INI_FILE_COL_LEVEL_KEY);

				if ((BitMask & EXPORT_JOBS) == EXPORT_JOBS)
					Exporter.AddColumn(INI_FILE_COL_JOBS_KEY);

				if ((BitMask & EXPORT_RMKS) == EXPORT_RMKS)
					Exporter.AddColumn(INI_FILE_COL_REMARKS_KEY);

				if ((BitMask & EXPORT_BG_URL) == EXPORT_BG_URL)
					Exporter.AddColumn(INI_FILE_EXPORT_BG_URL_KEY);

				if ((BitMask & EXPORT_MEDIAN) == EXPORT_MEDIAN)
					Exporter.AddColumn(INI_FILE_EXPORT_MEDIAN_KEY);

				for (int CharIndex = 0; CharIndex < CharCount; ++CharIndex)
				{
					if (ExportedChars[CharIndex] == false)
						continue;

					pInvMap = NULL;

					if (m_GlobalMap.Lookup(CharIndex, pInvMap) && pInvMap != NULL)
					{
						// add the inventory files for the current character
						for (int FileIndex = 0; FileIndex < FileCount; ++FileIndex)
						{
							pItemMap = NULL;

							if (pInvMap->Lookup(FileIndex, pItemMap) && pItemMap != NULL)
							{
								ItemPos = pItemMap->GetStartPosition();

								while (ItemPos != NULL)
								{
									pItemMap->GetNextAssoc(ItemPos, ItemID, pItem);

									Exporter.AddColumn(m_CharacterNames[CharIndex])
										.AddColumn(m_InventoryNames[FileIndex]);

									if ((BitMask & EXPORT_NAME) == EXPORT_NAME)
										Exporter.AddColumn(pItem->ItemName);

									if ((BitMask & EXPORT_ATTR) == EXPORT_ATTR)
										Exporter.AddColumn(pItem->Attr);

									if ((BitMask & EXPORT_DESC) == EXPORT_DESC)
										Exporter.AddColumn(pItem->ItemDescription);

									if ((BitMask & EXPORT_TYPE) == EXPORT_TYPE)
										Exporter.AddColumn(pItem->Slot);

									if ((BitMask & EXPORT_RACES) == EXPORT_RACES)
										Exporter.AddColumn(pItem->Races);

									if ((BitMask & EXPORT_LEVEL) == EXPORT_LEVEL)
										Exporter.AddColumn(pItem->Level);

									if ((BitMask & EXPORT_JOBS) == EXPORT_JOBS)
										Exporter.AddColumn(pItem->Jobs);

									if ((BitMask & EXPORT_RMKS) == EXPORT_RMKS)
										Exporter.AddColumn(pItem->Remarks);

									if ((BitMask & EXPORT_BG_URL) == EXPORT_BG_URL)
									{
										CString Url;
										FFXiItemList::BuildBgWikiUrl(pItem->ItemName, Url);
										Exporter.AddColumn(Url);
									}

									if ((BitMask & EXPORT_MEDIAN) == EXPORT_MEDIAN)
									{
										CString Median;
										if (!m_ItemMedianCache.Lookup(pItem->ItemHdr.ItemID, Median))
											Median = pItem->Median.IsEmpty() ? _T("0") : pItem->Median;

										Exporter.AddColumn(Median);
									}
								}
							}
						}
					}
				}

				Exporter.CloseFile();
				ShellExecute(m_hWnd, _T("open"), Filename, NULL, NULL, SW_SHOW);
			}
		}
		else
		{
			AfxMessageBox(_T("Nothing to export"));
		}
	}
}

afx_msg void CLootBoxDlg::OnFetchFfxiah()
{
	if (m_FfxiahServer.IsEmpty())
	{
		AfxMessageBox(_T("Please select an game server first."));
		return;
	}

	ItemArray *pItemList = GetItemMap(m_SelectedChar, m_SelectedTab);
	if (pItemList == NULL)
	{
		AfxMessageBox(_T("No items available to fetch."));
		return;
	}

	StartMedianFetch(pItemList);
}

afx_msg void CLootBoxDlg::OnFileQuit()
{
	PostMessage(WM_CLOSE);
}

afx_msg void CLootBoxDlg::OnHelpAbout()
{
	CAboutDlg dlg;
	dlg.DoModal();
}

void CLootBoxDlg::OnRefreshClose()
{
	CTabCtrl *pTabCtrl = (CTabCtrl*)GetDlgItem(IDC_INVENTORY_TABS);

	if (IsInventoryTab() == false)
	{
		pTabCtrl->DeleteItem(m_SelectedTab--);
		pTabCtrl->SetCurSel(m_SelectedTab);
	}
}

LRESULT CLootBoxDlg::OnSearchClose(WPARAM wParam, LPARAM lParam)
{
	FFXiItemList *pList = (FFXiItemList*)GetDlgItem(IDC_INVENTORY_LIST);
	CTabCtrl *pTabCtrl = (CTabCtrl*)GetDlgItem(IDC_INVENTORY_TABS);
	SearchData *pData;
	int CountTabs = 0;

	if (pTabCtrl && pList)
	{
		CString TabText;

		pData = new SearchData;
		pData->Done = false;

		LoadGlobalMap();

		pData->pParams = m_pSearchDlg->GetSearchParams();

		CountTabs = pTabCtrl->GetItemCount();

		TabText.Format(_T("Search %d"), CountTabs - m_InventoryFiles.GetCount() + 1);
		m_SelectedTab = pTabCtrl->InsertItem(CountTabs, TabText, 3);

		if (m_SelectedTab != -1)
		{
			pList->SetColumnWidth(INVENTORY_LIST_COL_LOCATION, 150);
			pTabCtrl->SetCurSel(m_SelectedTab);
			m_SearchTabs.SetAt(m_SelectedTab, pData);
		}

		UpdateStatus();
	}

	return 0L;
}

void CLootBoxDlg::GetSearchResults(SearchData *pData)
{
	FFXiItemList* pList = (FFXiItemList*)GetDlgItem(IDC_INVENTORY_LIST);
	CStatic *pLabel = (CStatic*)GetDlgItem(IDC_ITEM_COUNT);

	if (pList != NULL && pData != NULL)
	{
		if (pData->Done == false)
		{
			int CharID, FileID, ItemID, ItemCount, ListIndex = 0;
			POSITION GlobalPos, InvPos, ItemPos;
			SearchHandler Searcher(pData);
			InventoryMap *pInvMap;
			InventoryItem *pItem;
			ItemArray *pItemArr;

			GlobalPos = m_GlobalMap.GetStartPosition();
			pItemArr = NULL;
			pInvMap = NULL;

			ItemCount = GlobalMapCount();
			// Init progress bar
			m_ProgressDlg.Create(IDD_PROGRESS, this);
			m_ProgressDlg.m_Progress.SetRange(0, ItemCount);
			m_ProgressDlg.m_Progress.SetStep(1);

			while (GlobalPos != NULL)
			{
				m_GlobalMap.GetNextAssoc(GlobalPos, CharID, pInvMap);

				if (pInvMap != NULL)
				{
					InvPos = pInvMap->GetStartPosition();

					while (InvPos != NULL)
					{
						pInvMap->GetNextAssoc(InvPos, FileID, pItemArr);

						if (pItemArr != NULL)
						{
							ItemPos = pItemArr->GetStartPosition();

							while (ItemPos != NULL)
							{
								pItemArr->GetNextAssoc(ItemPos, ItemID, pItem);

								if (pItem != NULL)
									Searcher.ProcessAll(pItem);

								m_ProgressDlg.m_Progress.StepIt();
							}
						}
					}
				}
			}

			m_ProgressDlg.DestroyWindow();
			pData->Done = true;
		}
	}
}

afx_msg void CLootBoxDlg::OnOptionsChange(UINT CmdID)
{
	FFXiItemList *pInvList = (FFXiItemList*)GetDlgItem(IDC_INVENTORY_LIST);
	bool CompactListChange = false;
	ItemArray *pItemMap = NULL;
	int PrevLang = m_Language;
	CString PrevServer = m_FfxiahServer;

	pItemMap = GetItemMap(m_SelectedChar, m_SelectedTab);

	CString NewServer;
	if (TryGetServerNameForCommand(CmdID, NewServer))
	{
		if (m_FfxiahServer.CompareNoCase(NewServer) != 0)
			m_FfxiahServer = NewServer;
	}
	else
	{
		NewServer.Empty();
	}

	if (NewServer.IsEmpty())
	{
		switch (CmdID)
		{
			default:
			case ID_LANGUAGE_JAPANESE:
				m_Language = FFXI_LANG_JP;
				break;
			case ID_LANGUAGE_ENGLISH:
				m_Language = FFXI_LANG_US;
				break;
			case ID_LANGUAGE_FRENCH:
				m_Language = FFXI_LANG_FR;
				break;
			case ID_LANGUAGE_GERMAN:
				m_Language = FFXI_LANG_DE;
				break;
			case ID_VIEW_COMPACTLISTING:
				CompactListChange = true;
				m_CompactList = !pInvList->IsCompact();
				pInvList->SetCompactList(m_CompactList);
				break;
		}
	}

	if (!NewServer.IsEmpty() && m_FfxiahServer.CompareNoCase(PrevServer) != 0)
	{
		SaveMedianCacheToIni(PrevServer);
		m_pIni->SetValue(INI_FILE_CONFIG_SECTION, INI_FILE_FFXIAH_SERVER_KEY, m_FfxiahServer);
		SetServerMenu(PrevServer, false);
		SetServerMenu(m_FfxiahServer);

		m_StopMedianThread = true;
		m_MedianThread = NULL;
		m_StopMedianThread = false;
		m_ItemMedianCache.RemoveAll();
		LoadMedianCacheFromIni(m_FfxiahServer);

		if (pInvList)
		{
			int listCount = pInvList->GetItemCount();
			for (int i = 0; i < listCount; i++)
			{
				InventoryItem *pItem = (InventoryItem*)pInvList->GetItemData(i);
				if (pItem != NULL)
					pItem->Median = _T("0");

				pInvList->UpdateItemText(_T("0"), i, INVENTORY_LIST_COL_MEDIAN);
			}
		}
	}
	else if (m_Language != PrevLang)
	{
		m_pIni->SetLongValue(INI_FILE_CONFIG_SECTION, INI_FILE_LANGUAGE_KEY, m_Language);
		SetLanguageMenu(PrevLang, false);
		SetLanguageMenu(m_Language);

		LoadGlobalMap(true);
	}
	else if (CompactListChange)
	{
		m_pIni->SetLongValue(INI_FILE_CONFIG_SECTION, INI_FILE_COMPACT_LIST_KEY, m_CompactList ? 1 : 0);
		SetCompactListMenu(m_CompactList);
	}

	UpdateStatus();
}

void CLootBoxDlg::RemoveItemIcons()
{
	for (int i = 0; i < m_pItemIconList->GetImageCount(); i++)
		m_pItemIconList->Remove(i);
}

HBITMAP CLootBoxDlg::GetItemIcon(FFXiIconInfo *pIconInfo, CDC *pDC, int Width, int Height, int ItemID)
{
	if (pIconInfo != NULL)
	{
		HDC hDC = pDC->GetSafeHdc();
		PalettedBitmapInfo BmpInfo;
		HBITMAP hBitmap;
		int Index = 0;
		DWORD *pDst;

		BmpInfo = pIconInfo->ImageInfo;

		hBitmap = ::CreateDIBSection(hDC, (BITMAPINFO*)&BmpInfo, DIB_RGB_COLORS, (void**)&pDst, NULL, 0);

		if (hBitmap != NULL)
		{
			memcpy_s(pDst, 1024, &pIconInfo->ImageInfo.ImageData, 1024);

			if (BmpInfo.bmiHeader.biWidth != Width || BmpInfo.bmiHeader.biHeight != Height)
			{
#ifdef GDIPLUS_IMAGE_RESIZING
				Gdiplus::Bitmap *pResizedImage, *pSrcImage;

				pSrcImage = Gdiplus::Bitmap::FromHBITMAP(hBitmap, NULL);

				if (pSrcImage != NULL)
				{
					pResizedImage = (Gdiplus::Bitmap*)pSrcImage->GetThumbnailImage(Width, Height);

					if (pResizedImage != NULL)
					{
						DeleteObject(hBitmap);
						pResizedImage->GetHBITMAP(Gdiplus::Color::Transparent, &hBitmap);

						if (ItemID >= 0)
						{
							CLSID encoderClsid;
							CString Filename;

							GetEncoderClsid(_T("image/png"), &encoderClsid);

							Filename.Format(_T("img\\item%d.png"), ItemID);
							pResizedImage->Save(Filename, &encoderClsid, NULL);
						}

						delete pResizedImage;
					}

					delete pSrcImage;
				}
#else
				HBITMAP PrevSrcBmp, PrevDstBmp;
				CDC DstMemoryDC, SrcMemoryDC;
				BITMAPINFOHEADER CopyInfo;
				HBITMAP hResizedBitmap;

				SecureZeroMemory(&CopyInfo, sizeof(BITMAPINFOHEADER));
				CopyInfo.biSize = sizeof(BITMAPINFOHEADER);
				CopyInfo.biWidth = Width;
				CopyInfo.biHeight = Height;
				CopyInfo.biPlanes = 1;
				CopyInfo.biBitCount = 32;

				SrcMemoryDC.CreateCompatibleDC(pDC);
				PrevSrcBmp = (HBITMAP)SrcMemoryDC.SelectObject(hBitmap);

				DstMemoryDC.CreateCompatibleDC(pDC);
				hResizedBitmap = ::CreateDIBitmap(hDC, &CopyInfo, DIB_RGB_COLORS, NULL, NULL, 0);
				PrevDstBmp = (HBITMAP)DstMemoryDC.SelectObject(hResizedBitmap);

				DstMemoryDC.SetBkMode(TRANSPARENT);
				DstMemoryDC.SetStretchBltMode(COLORONCOLOR);
				DstMemoryDC.StretchBlt(0, 0, Width, Height, &SrcMemoryDC, 0, 0, BmpInfo.bmiHeader.biWidth, BmpInfo.bmiHeader.biHeight, SRCCOPY);

				DeleteObject(hBitmap);
				SrcMemoryDC.SelectObject(PrevSrcBmp);
				DstMemoryDC.SelectObject(PrevDstBmp);

				hBitmap = hResizedBitmap;
#endif
			}

			return hBitmap;
		}

		DeleteObject(hDC);
	}

	return NULL;
}

#ifdef GDIPLUS_IMAGE_RESIZING
int GetEncoderClsid(const WCHAR* format, CLSID* pClsid)
{
	UINT  num = 0;          // number of image encoders
	UINT  size = 0;         // size of the image encoder array in bytes

	Gdiplus::ImageCodecInfo* pImageCodecInfo = NULL;

	Gdiplus::GetImageEncodersSize(&num, &size);
	if (size == 0)
		return -1;  // Failure

	pImageCodecInfo = (Gdiplus::ImageCodecInfo*)(malloc(size));
	if (pImageCodecInfo == NULL)
		return -1;  // Failure

	Gdiplus::GetImageEncoders(num, size, pImageCodecInfo);

	for (UINT j = 0; j < num; ++j)
	{
		if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0)
		{
			*pClsid = pImageCodecInfo[j].Clsid;
			free(pImageCodecInfo);
			return j;  // Success
		}
	}

	free(pImageCodecInfo);
	return -1;  // Failure
}
#endif // GDIPLUS_IMAGE_RESIZING

#ifdef _DEBUG
void CLootBoxDlg::DumpGlobalMap()
{
	int CharID, FileID, ItemID, ItemCount = 0;
	POSITION GlobalPos, InvPos;
	InventoryItem *pItem;
	InventoryMap *pInvMap;
	ItemArray *pItemMap;

	GlobalPos = m_GlobalMap.GetStartPosition();
	pItemMap = NULL;
	pInvMap = NULL;

	TRACE(_T("=MAP==================================\n\tUSERS\n\t|\n"));

	while (GlobalPos != NULL)
	{
		m_GlobalMap.GetNextAssoc(GlobalPos, CharID, pInvMap);
		TRACE(_T("\t|_ %s (%s)\n"), m_CharacterNames.GetAt(CharID), m_CharacterIDs.GetAt(CharID));
		TRACE(_T("\t|\t|\n"));

		if (pInvMap != NULL)
		{
			InvPos = pInvMap->GetStartPosition();

			while (InvPos != NULL)
			{
				pInvMap->GetNextAssoc(InvPos, FileID, pItemMap);

				TRACE(_T("\t|\t|_ %s (%s)\n"), m_InventoryNames.GetAt(FileID), m_InventoryFiles.GetAt(FileID));
				TRACE(_T("\t|\t|\t|\n"));


				if (pItemMap != NULL)
				{
					POSITION ItemPos = pItemMap->GetStartPosition();

					while (ItemPos != NULL)
					{
						pItemMap->GetNextAssoc(ItemPos, ItemID, pItem);

						if (pItem != NULL)
							TRACE(_T("\t|\t|\t|_ %s (%d)\n"), pItem->ItemName, pItem->RefCount);
					}
				}
			}
		}
	}
}
#endif // _DEBUG
