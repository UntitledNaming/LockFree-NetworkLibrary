#include <windows.h>

#include "CSector.h"
#include "CUser.h"

CUser::CUser()
{
}

CUser::~CUser()
{
	
}

void CUser::User_Init(UINT64 uniqID, INT64 accountno, WCHAR* id, WCHAR* nick)
{
	s_UniqID = uniqID;
	s_Sector = 0;
	s_TimeOut = 0;
	s_RecvTime = timeGetTime();
	s_AccountNo = accountno;
	s_Pos.s_xpos = INVALID_SECTOR_XPOS;
	s_Pos.s_ypos = INVALID_SECTOR_YPOS;
	memcpy_s(s_ID, ID_MAX, id, ID_MAX);
	memcpy_s(s_NickName, NICK_MAX, nick, NICK_MAX);
}
