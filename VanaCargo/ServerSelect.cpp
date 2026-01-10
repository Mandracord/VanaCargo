// ServerSelect.cpp : implementation file
//
#include "stdafx.h"
#include "resource.h"

#include "ServerSelect.h"

BEGIN_MESSAGE_MAP(ServerSelect, CDialog)
END_MESSAGE_MAP()

ServerSelect::ServerSelect(const CArray<CString, LPCTSTR> &Servers, const CString &DefaultServer, CWnd* pParent)
	: CDialog(ServerSelect::IDD, pParent)
	, m_Servers(Servers)
	, m_DefaultServer(DefaultServer)
{
}

BOOL ServerSelect::OnInitDialog()
{
	CComboBox *pCombo = (CComboBox*)GetDlgItem(IDC_SERVER_COMBO);
	int Selected = 0;

	for (int i = 0; i < m_Servers.GetCount(); i++)
	{
		int index = pCombo->AddString(m_Servers[i]);
		if (m_Servers[i].CompareNoCase(m_DefaultServer) == 0)
			Selected = index;
	}

	pCombo->SetCurSel(Selected);

	return TRUE;
}

void ServerSelect::OnOK()
{
	CComboBox *pCombo = (CComboBox*)GetDlgItem(IDC_SERVER_COMBO);
	int Selected = pCombo->GetCurSel();
	if (Selected >= 0)
		pCombo->GetLBText(Selected, m_SelectedServer);

	CDialog::OnOK();
}

CString ServerSelect::GetSelectedServer() const
{
	return m_SelectedServer;
}
