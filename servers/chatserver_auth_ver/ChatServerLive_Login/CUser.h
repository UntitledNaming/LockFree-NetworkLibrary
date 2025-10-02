#pragma once
#define ID_MAX          20
#define NICK_MAX        20
#define SESSION_KEY_MAX 64
#define MESSAGE_LEN_MAX 256
class CUser
{
public:
	UINT64     s_UniqID;
	UCHAR      s_Sector;
	UCHAR      s_TimeOut;
	LONGLONG   s_RecvTime;
	INT64      s_AccountNo;
	SECTOR     s_Pos;
	WCHAR      s_ID[ID_MAX];
	WCHAR      s_NickName[NICK_MAX];
public:
	CUser();
	~CUser();

	void User_Init(UINT64 uniqID, INT64 accountno, WCHAR* id, WCHAR* nick);

};