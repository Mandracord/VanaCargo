// Progress_Dlg.cpp : implementation file
//

#include "stdafx.h"
#include "resource.h"
#include "Progress_Dlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CProgress_Dlg dialog


CProgress_Dlg::CProgress_Dlg(CWnd* pParent /*=NULL*/)
	: CDialog(CProgress_Dlg::IDD, pParent)
	, m_Cancelled(false)
	, m_StartTick(0)
{
	//{{AFX_DATA_INIT(CProgress_Dlg)
		// NOTE: the ClassWizard will add member initialization here
	//}}AFX_DATA_INIT
}


void CProgress_Dlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CProgress_Dlg)
	DDX_Control(pDX, IDC_PROGRESS, m_Progress);
	DDX_Control(pDX, IDC_PROGRESS_LABEL, m_Label);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CProgress_Dlg, CDialog)
	//{{AFX_MSG_MAP(CProgress_Dlg)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CProgress_Dlg message handlers

void CProgress_Dlg::OnCancel(void)
{
	m_Cancelled = true;
	if (GetSafeHwnd() != NULL)
		DestroyWindow();
}

void CProgress_Dlg::SetStatusText(const CString &text)
{
	if (m_Label.GetSafeHwnd() != NULL)
		m_Label.SetWindowText(text);
}

void CProgress_Dlg::SetProgress(int current, int total)
{
	if (total <= 0)
	{
		total = 1;
	}

	if (m_Label.GetSafeHwnd() != NULL)
	{
		if (m_StartTick == 0 && current > 0)
		{
			m_StartTick = GetTickCount();
		}

		int percent = (current * 100) / total;
		CString status;
		if (current > 0 && m_StartTick != 0)
		{
			DWORD elapsedMs = GetTickCount() - m_StartTick;
			double avgMs = (double)elapsedMs / (double)current;
			DWORD remainingMs = (DWORD)(avgMs * (double)(total - current));
			DWORD remainingSec = remainingMs / 1000;
			DWORD hours = remainingSec / 3600;
			DWORD minutes = (remainingSec % 3600) / 60;
			DWORD seconds = remainingSec % 60;

			if (hours > 0)
			{
				status.Format(_T("Loading FFXIAH prices... %d/%d (%d%%) - Time remaining: %u:%02u:%02u"),
					current, total, percent, hours, minutes, seconds);
			}
			else
			{
				status.Format(_T("Loading FFXIAH prices... %d/%d (%d%%) - Time remaining: %u:%02u"),
					current, total, percent, minutes, seconds);
			}
		}
		else
		{
			status.Format(_T("Loading FFXIAH prices... %d/%d (%d%%) - Time remaining: --:--"),
				current, total, percent);
		}
		m_Label.SetWindowText(status);
	}

	if (m_Progress.GetSafeHwnd() != NULL)
	{
		m_Progress.SetRange(0, (short)total);
		m_Progress.SetPos(current);
	}
}
