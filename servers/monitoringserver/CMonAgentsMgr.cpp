#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <timeapi.h>
#include <ws2tcpip.h>
#include <string>
#include <thread>
#include <unordered_map>

#include "LogClass.h"
#include "CAgent.h"
#include "CommonProtocol.h"
#include "MemoryPoolTLS.h"
#include "CMessage.h"
#include "CNetServer.h"
#include "CMonAgentsMgr.h"

CMonAgentsMgr::CMonAgentsMgr() : m_agentMaxCnt(0), m_endFlag(false), m_monitorSessionKey(nullptr),m_pAgentPool(nullptr)
{

}

CMonAgentsMgr::~CMonAgentsMgr()
{
}

void CMonAgentsMgr::RunAgentsManager(INT MAXAGENTCNT, WCHAR* SERVERIP, INT SERVERPORT, INT numberOfCreateThread, INT numberOfRunningThread, INT maxNumOfSession, INT SendSleep, INT SendTHFL, WORD packetCode, WORD fixedkey, BOOL OffNagle)
{
	// ��� �ʱ�ȭ
	m_monitorSessionKey = "ajfw@!cv980dSZ[fje#@fdj123948djf";
	m_agentMaxCnt = MAXAGENTCNT;
	m_pAgentPool = new CMemoryPool<CAgent>;
	m_endFlag = false;

	// ������ ����
	m_frame = std::thread(&CMonAgentsMgr::FrameThread, this);


	// ��Ʈ��ũ ���̺귯�� �۵�
	Start(SERVERIP, SERVERPORT, numberOfCreateThread, numberOfRunningThread, maxNumOfSession, SendSleep, SendTHFL, packetCode, fixedkey, OffNagle);

}

void CMonAgentsMgr::StopAgentsManager()
{
	Stop();

	// ������ �ı�
	m_endFlag = true;

	if (m_frame.joinable())
	{
		m_frame.join();
	}

	// ��ü �ı�
	delete m_pAgentPool;
	
}

void CMonAgentsMgr::SendServerData(BYTE serverNo, BYTE dataType, INT dataValue, INT timeStamp)
{
	CMessage* pPacket = CMessage::Alloc();
	pPacket->Clear();
	*pPacket << (WORD)en_PACKET_CS_MONITOR_TOOL_DATA_UPDATE;
	*pPacket << serverNo;
	*pPacket << dataType;
	*pPacket << dataValue;
	*pPacket << timeStamp;

	
	SendPacketAll(pPacket);

	CMessage::Free(pPacket);
}

size_t CMonAgentsMgr::GetAgentsSize()
{
	return m_agentMap.size();
}

size_t CMonAgentsMgr::GetNonAgentsSize()
{
	return m_nonAgentMap.size();
}

bool CMonAgentsMgr::OnConnectionRequest(WCHAR* InputIP, unsigned short InputPort)
{
	return true;
}

void CMonAgentsMgr::OnClientJoin(UINT64 SessionID)
{
	AcquireSRWLockExclusive(&m_nonAgentMapLock);
	m_nonAgentMap.insert(std::pair<UINT64, DWORD>(SessionID, timeGetTime()));
	ReleaseSRWLockExclusive(&m_nonAgentMapLock);
}

void CMonAgentsMgr::OnClientLeave(UINT64 SessionID)
{
	CAgent* pAgent = nullptr;

	AcquireSRWLockExclusive(&m_nonAgentMapLock);
	AcquireSRWLockExclusive(&m_agentMapLock);

	// ���� �ڷᱸ�� ���� ã�ƺ��� ����
	std::unordered_map<UINT64, CAgent*>::iterator iton;
	iton = m_agentMap.find(SessionID);
	if (iton == m_agentMap.end())
	{
		// ���� �ڷᱸ���� ������ NonUser �ڷᱸ�� ã��.
		std::unordered_map<UINT64, DWORD>::iterator itNon;
		itNon = m_nonAgentMap.find(SessionID);
		if (itNon == m_nonAgentMap.end())
		{
			__debugbreak();
		}

		m_nonAgentMap.erase(itNon);

		ReleaseSRWLockExclusive(&m_agentMapLock);
		ReleaseSRWLockExclusive(&m_nonAgentMapLock);
		return;
	}

	pAgent = iton->second;
	m_agentMap.erase(iton);

	ReleaseSRWLockExclusive(&m_agentMapLock);
	ReleaseSRWLockExclusive(&m_nonAgentMapLock);

	//Ǯ�� �ݳ�
	m_pAgentPool->Free(pAgent);

}

void CMonAgentsMgr::OnRecv(UINT64 SessionID, CMessage* pMessage)
{
	WORD type;
	*pMessage >> type;

	if (pMessage->GetLastError())
	{
		Disconnect(SessionID);
		LOG(L"CMonAgentsMgr", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"OnRecv::CMessage Flag Error... / UniqID : %lld", SessionID);
		return;
	}

	switch (type)
	{
	case en_PACKET_CS_MONITOR_TOOL_REQ_LOGIN:
		ToolLoginProc(SessionID, pMessage);
		break;

	default:
		Disconnect(SessionID);
		break;
	}
}

void CMonAgentsMgr::ToolLoginProc(UINT64 sessionID, CMessage* pMessage)
{
	CHAR sessionkey[MONITOR_SESSION_KEY_MAX];
	BYTE status;

	if (m_agentMap.size() >= m_agentMaxCnt)
	{
		Disconnect(sessionID);
		return;
	}

	pMessage->GetData(sessionkey, MONITOR_SESSION_KEY_MAX);
	if (pMessage->GetLastError())
	{
		LOG(L"CMonAgentsMgr", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"ToolLoginProc::CMessage Flag Error... / UniqID : %lld", sessionID);
		Disconnect(sessionID);
		return;
	}

	if (pMessage->GetDataSize() > 0)
	{
		LOG(L"CMonAgentsMgr", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"ToolLoginProc::CMessage Size Overflow Error... / UniqID : %lld", sessionID);
		Disconnect(sessionID);
		return;
	}


	// ����͸� ���� Key�� ��
	if (memcmp(sessionkey, m_monitorSessionKey, MONITOR_SESSION_KEY_MAX) == 0)
		status = dfMONITOR_TOOL_LOGIN_ERR_SESSIONKEY;
	else
		status = dfMONITOR_TOOL_LOGIN_OK;

	// Agent ����
	CAgent* pAgent = m_pAgentPool->Alloc();
	pAgent->Agent_Init(sessionID);

	// nonuser���� �����ϰ� user�� �ű��
	AgentInsert(sessionID, pAgent);

	// Res �޼��� ������
	CMessage* pPacket = CMessage::Alloc();
	pPacket->Clear();
	*pPacket << (WORD)en_PACKET_CS_MONITOR_TOOL_RES_LOGIN;
	*pPacket << status;

	SendPacket(sessionID, pPacket);

	CMessage::Free(pPacket);
}

void CMonAgentsMgr::AgentInsert(UINT64 sessionID, CAgent* pUser)
{
	AcquireSRWLockExclusive(&m_nonAgentMapLock);
	AcquireSRWLockExclusive(&m_agentMapLock);

	std::unordered_map<UINT64, DWORD>::iterator itNon;
	itNon = m_nonAgentMap.find(sessionID);
	if (itNon == m_nonAgentMap.end())
	{
		__debugbreak();
	}

	m_nonAgentMap.erase(itNon);
	m_agentMap.insert(std::pair<UINT64, CAgent*>(sessionID, pUser));

	ReleaseSRWLockExclusive(&m_agentMapLock);
	ReleaseSRWLockExclusive(&m_nonAgentMapLock);

}

void CMonAgentsMgr::FrameThread()
{

	DWORD OldTimeOutTick1; //2��  Ÿ�Ӿƿ�
	DWORD OldTimeOutTick2; //40�� Ÿ�Ӿƿ�
	DWORD curTick;

	OldTimeOutTick1 = timeGetTime();
	OldTimeOutTick2 = timeGetTime();

	while (!m_endFlag)
	{
		Sleep(100);

		curTick = timeGetTime();

		//if (curTick - OldTimeOutTick1 >= df_AGENTSMANAGER_NONAGENT_TIMEOUT)
		//{
		//	NonAgentTimeOut();

		//	OldTimeOutTick1 += df_AGENTSMANAGER_NONAGENT_TIMEOUT;
		//}

		//if (curTick - OldTimeOutTick2 >= df_AGENTSMANAGER_AGENT_TIMEOUT)
		//{
		//	AgentTimeOut();
		//	OldTimeOutTick2 += df_AGENTSMANAGER_AGENT_TIMEOUT;
		//}

	}

}

void CMonAgentsMgr::NonAgentTimeOut()
{
	DWORD tick;
	std::unordered_map<UINT64, DWORD>::iterator it;

	AcquireSRWLockShared(&m_nonAgentMapLock);
	tick = timeGetTime();
	it = m_nonAgentMap.begin();
	for (; it != m_nonAgentMap.end(); ++it)
	{
		if (tick - it->second >= df_AGENTSMANAGER_NONAGENT_TIMEOUT)
		{
			Disconnect(it->first);
			LOG(L"Login", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"FrameThread  NonAgentTimeout / SessionID : %lld / TimeOut : %d ", it->first, tick - it->second);
			continue;
		}
	}
	ReleaseSRWLockShared(&m_nonAgentMapLock);
}

void CMonAgentsMgr::AgentTimeOut()
{
	DWORD tick;
	CAgent* pAgent;
	std::unordered_map<UINT64, CAgent*>::iterator it;


	AcquireSRWLockShared(&m_agentMapLock);
	tick = timeGetTime();
	it = m_agentMap.begin();
	for (; it != m_agentMap.end(); ++it)
	{
		pAgent = it->second;
		if (tick - pAgent->m_recvTime >= df_AGENTSMANAGER_AGENT_TIMEOUT)
		{
			if (pAgent->m_timeOut == true)
				continue;

			Disconnect(it->first);
			pAgent->m_timeOut = true;

			LOG(L"LoginServer", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"FrameThread  AgentTimeout / SessionID : %lld / TimeOut : %lld ...", pAgent->m_uniqID, tick - pAgent->m_recvTime);
			continue;
		}
	}
	ReleaseSRWLockShared(&m_agentMapLock);
}
