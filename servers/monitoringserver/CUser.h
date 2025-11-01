#pragma once
#define INVALID_SERVER_NO -1

class CUser
{
public:
	UINT64     m_UniqID;
	INT        m_ServerNo;
	DWORD      m_recvTime;
	bool       m_TimeOut;
public:
	CUser();
	~CUser();

	void User_Init(UINT64 uniqID);

};