#include <windows.h>
#include <unordered_map>
#include <string>
#include <thread>

#include "MemoryPoolTLS.h"
#include "CMessage.h"
#include "IUser.h"

#include "LFStack.h"
#include "LFQSingleLive.h"

#include "Ring_Buffer.h"
#include "LFQMultiLive.h"
#include "CSession.h"
#include "CGameLibrary.h"
#include "CGroup.h"


CGroup::CGroup()
{
	m_pGameLib = nullptr;
	m_GroupFrameTime = df_DEFAULT_FRAME;
	m_GroupID = df_INVALID_GROUPID;
	m_Shared = false;
	m_RecvTPS = 0;
	m_SendTPS = 0;
	m_FrameTPS = 0;
	m_OldTime = timeGetTime();
	InitializeSRWLock(&m_GroupLock);
}


CGroup::~CGroup()
{

}
