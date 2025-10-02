#include <windows.h>

#include "CUser.h"

CUser::CUser()
{
}

CUser::~CUser()
{
	
}

void CUser::User_Init(UINT64 uniqID)
{
	m_UniqID = uniqID;
	m_ServerNo = INVALID_SERVER_NO;
	m_TimeOut = false;
	m_recvTime = timeGetTime();
}
