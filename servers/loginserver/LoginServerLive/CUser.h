#pragma once
class CUser
{
public:
	UINT64     s_UniqID;
	UCHAR      s_TimeOut;
	DWORD   s_RecvTime;
	INT64      s_AccountNo;

public:
	CUser();
	~CUser();

	void User_Init(UINT64 uniqID, INT64 accountno);

};