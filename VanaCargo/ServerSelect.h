#pragma once

class ServerSelect : public CDialog
{
	enum
	{
		IDD = IDD_SERVER_SELECT
	};

public:
	ServerSelect(const CArray<CString, LPCTSTR> &Servers, const CString &DefaultServer, CWnd* pParent = NULL);
	CString GetSelectedServer() const;

protected:
	const CArray<CString, LPCTSTR> &m_Servers;
	CString m_DefaultServer;
	CString m_SelectedServer;

	virtual BOOL OnInitDialog();
	virtual void OnOK();

	DECLARE_MESSAGE_MAP()
};
