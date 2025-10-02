#include <windows.h>
#include "CAgent.h"

CAgent::CAgent()
{
}

CAgent::~CAgent()
{
}

void CAgent::Agent_Init(UINT64 uniqid)
{
	m_uniqID = uniqid;
	m_recvTime = timeGetTime();
	m_timeOut = false;
}
