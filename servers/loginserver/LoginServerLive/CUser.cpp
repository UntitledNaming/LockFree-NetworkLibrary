#include <windows.h>
#include "CUser.h"

CUser::CUser()
{
}

CUser::~CUser()
{
	
}

void CUser::User_Init(UINT64 uniqID, INT64 accountno)
{
	s_UniqID = uniqID;
	s_TimeOut = 0;
	s_RecvTime = timeGetTime();
	s_AccountNo = accountno;
}
