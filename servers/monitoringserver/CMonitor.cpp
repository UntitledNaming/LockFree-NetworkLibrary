#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <timeapi.h>
#include <ws2tcpip.h>
#include <string>
#include <codecvt>
#include <unordered_map>
#include <thread>
#include <mysql.h>
#include <Pdh.h>
#include <ctime>
#include <cstdio>
#pragma comment(lib,"Pdh.lib")

#include "CPUUsage.h"
#include "ProcessMonitor.h"
#include "CommonProtocol.h"
#include "LibraryHeader.h"
#include "LFQSingleLive.h"
#include "LogClass.h"
#include "TextParser.h"
#include "MemoryPoolTLS.h"
#include "CMessage.h"
#include "LFQMultiLive.h"
#include "Ring_Buffer.h"
#include "CUser.h"
#include "CSession.h"
#include "DBTLS.h"

#include "CNetServer.h"
#include "CMonAgentsMgr.h"
#include "CLanServer.h"
#include "CMonitor.h"

#pragma warning(disable:4996)

CMonitor::CMonitor() : m_DBEvent{ INVALID_HANDLE_VALUE ,INVALID_HANDLE_VALUE ,INVALID_HANDLE_VALUE }, m_EndFlag(false), m_pAgentMgr(nullptr), m_pDBQueue(nullptr), m_pDBTLS(nullptr),
m_pMonitorPool(nullptr), m_pPDH(nullptr), m_pUserPool(nullptr)
{

}

CMonitor::~CMonitor()
{

}

BOOL CMonitor::RunServer()
{
	//Parsing 작업
	Parser parser;

	if (!parser.LoadFile("MServerConfig.txt"))
		return false;

	Parser::st_Msg bindexp;
	parser.GetValue("BIND_EXTIP", &bindexp);

	Parser::st_Msg bindint;
	parser.GetValue("BIND_INTIP", &bindint);

	//char형 문자열을 wchar로 변환
	std::wstring bindEXTstr;
	std::wstring bindINTstr;
	std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
	bindEXTstr = converter.from_bytes(bindexp.s_ptr);
	bindINTstr = converter.from_bytes(bindint.s_ptr);

	Parser::st_Msg DB;
	parser.GetValue("DB_IP", &DB);

	INT DBport;
	parser.GetValue("DB_PORT", &DBport);

	INT extport;
	parser.GetValue("BIND_EXTPORT", &extport);

	INT intport;
	parser.GetValue("BIND_INTPORT", &intport);

	INT maxSessions;
	parser.GetValue("SESSION_MAX", &maxSessions);

	INT createthread;
	parser.GetValue("IOCP_WORKER_THREAD", &createthread);

	INT runningthread;
	parser.GetValue("IOCP_ACTIVE_THREAD", &runningthread);

	INT PACKET_CODE;
	parser.GetValue("PACKET_CODE", (int*)&PACKET_CODE);

	INT PACKET_KEY;
	parser.GetValue("PACKET_KEY", (int*)&PACKET_KEY);

	INT Nagle;
	parser.GetValue("NAGLE", (int*)&Nagle);

	INT SendFrame;
	parser.GetValue("SEND_FRAME", (int*)&SendFrame);

	INT SendFlag;
	parser.GetValue("SEND_TH_FLAG", (int*)&SendFlag);

	INT Loglevel;
	parser.GetValue("LOG_LEVEL", (int*)&Loglevel);

	INT MaxAgent;
	parser.GetValue("MAX_AGENT", &MaxAgent);

#pragma endregion

	CLogClass::GetInstance()->Init(Loglevel);
	CMessage::Init(sizeof(LANHEADER), sizeof(NETHEADER));

	Mem_Init(DB.s_ptr, DBport);

	Thread_Create();

	if (!Start((WCHAR*)bindINTstr.c_str(), intport, createthread, runningthread, maxSessions, SendFrame, SendFlag, (bool)Nagle))
		return false;


	m_pAgentMgr->RunAgentsManager(MaxAgent,(WCHAR*)bindEXTstr.c_str(), extport, createthread, runningthread, maxSessions, SendFrame, SendFlag, PACKET_CODE, PACKET_KEY, Nagle);
	
	return true;
}

void CMonitor::StopServer()
{
	Thread_Destroy();

	m_pAgentMgr->StopAgentsManager();

	Stop();

	for (int i = 0; i < DB_HANDLE_COUNT; i++)
	{
		CloseHandle(m_DBEvent[i]);
	}

	delete m_pUserPool;
	delete m_pMonitorPool;
	delete m_pDBQueue;
	delete m_pDBTLS;
	delete m_pAgentMgr;
	delete m_pPDH;

	CMessage::PoolDestroy();
}

void CMonitor::Mem_Init(const CHAR* DBip, INT DBport)
{
	m_pUserPool = new CMemoryPool<CUser>;
	m_pMonitorPool = new CMemoryPool<MONITOR_DATA>;
	m_pDBQueue = new LFQueue<MONITOR_DATA*>;
	m_pDBTLS = new DBTLS(DBip, DBport);
	m_pAgentMgr = new CMonAgentsMgr;
	m_pPDH = new ProcessMonitor;
	m_EndFlag = false;
	InitializeSRWLock(&m_UserMapLock);
	
	for (int i = 0; i < DB_HANDLE_COUNT; i++)
	{
		m_DBEvent[i] = CreateEvent(NULL, FALSE, FALSE, NULL);
	}
}

void CMonitor::ServerLoginProc(UINT64 sessionID, CMessage* pMessage)
{
	INT serverno;
	CUser* pUser = nullptr;
	std::unordered_map<UINT64, CUser*>::iterator it;

	*pMessage >> serverno;

	AcquireSRWLockShared(&m_UserMapLock);
	it = m_UserMap.find(sessionID);
	pUser = it->second;
	ReleaseSRWLockShared(&m_UserMapLock);

	pUser->m_ServerNo = serverno;
}

void CMonitor::DataUpdateProc(UINT64 sessionID, CMessage* pMessage)
{
	CUser* pUser = nullptr;
	BYTE  dataType;
	INT   datavalue;
	INT   timestamp;

	std::unordered_map<UINT64, CUser*>::iterator it;

	*pMessage >> dataType;
	*pMessage >> datavalue;
	*pMessage >> timestamp;

	AcquireSRWLockShared(&m_UserMapLock);
	it = m_UserMap.find(sessionID);
	pUser = it->second;
	ReleaseSRWLockShared(&m_UserMapLock);


	// DB 저장 스레드에게 저장 요청 보내기
	DataEnqueue(timestamp, datavalue, pUser->m_ServerNo, dataType);

	// 모니터링 툴에게 데이터 보내기
	m_pAgentMgr->SendServerData((BYTE)pUser->m_ServerNo, dataType, datavalue, timestamp);
}

void CMonitor::Thread_Create()
{
	m_DB = std::thread(&CMonitor::DBUpdate, this);
	m_Monitor = std::thread(&CMonitor::Monitor, this);
	m_Frame = std::thread(&CMonitor::FrameThread, this);
}

void CMonitor::Thread_Destroy()
{
	m_EndFlag = true;
	SetEvent(m_DBEvent[2]);

	if (m_Frame.joinable())
	{
		m_Frame.join();
	}

	if (m_Monitor.joinable())
	{
		m_Monitor.join();
	}

	if (m_DB.joinable())
	{
		m_DB.join();
	}
}

void CMonitor::DataInsert(MONITOR_DATA* pData)
{
	std::unordered_map<BYTE, DB_DATA*>::iterator it;

	// datatype을 map에서 찾기
	it = m_DataMap.find(pData->s_dataType);
	if (it == m_DataMap.end())
	{
		DB_DATA* DBdata = new DB_DATA;
		memset(DBdata, 0, sizeof(DB_DATA));
		DBdata->s_count = 1;
		DBdata->s_dataMin = min(DBdata->s_dataMin,pData->s_dataValue);
		DBdata->s_dataMax = max(DBdata->s_dataMax,pData->s_dataValue);
		DBdata->s_dataTotal = pData->s_dataValue;
		DBdata->s_serverNo = pData->s_serverNo;
		DBdata->s_timeStamp = pData->s_timeStamp;

		m_DataMap.insert(std::pair<BYTE, DB_DATA*>(pData->s_dataType, DBdata));
		return;
	}

	it->second->s_dataMax = max(it->second->s_dataMax, pData->s_dataValue);
	it->second->s_dataMin = min(it->second->s_dataMin, pData->s_dataValue);
	it->second->s_dataTotal += pData->s_dataValue;
	it->second->s_timeStamp += pData->s_timeStamp;
	it->second->s_count++;

}

void CMonitor::DBInsert()
{
	// 10분동안 저장한 모든 데이터 타입 대해서 모은 데이터 Avg 계산 및 DB 저장
	std::unordered_map<BYTE, DB_DATA*>::iterator it;
	DB_DATA* pData;
	for (it = m_DataMap.begin(); it != m_DataMap.end(); ++it)
	{
		pData = it->second;

		char convertedDataTime[20];
		time_t timestamp = static_cast<time_t>(pData->s_timeStamp / pData->s_count);
		tm* timeInfo = localtime(&timestamp);

		strftime(convertedDataTime, sizeof(convertedDataTime), "%Y-%m-%d %H:%M:%S", timeInfo);

		if (!m_pDBTLS->DB_Post_Query("INSERT INTO logdb.monitorlog (logtime, serverno, type, avr, min, max) VALUES ('%s', %d , %d, %d, %d, %d)", convertedDataTime, pData->s_serverNo, it->first, pData->s_dataTotal / pData->s_count,pData->s_dataMin, pData->s_dataMax))
			__debugbreak();


		delete it->second;
	}

	m_DataMap.clear();
}

void CMonitor::DataEnqueue(INT timestamp, INT dataValue, BYTE serverno, BYTE datatype)
{
	MONITOR_DATA* pData = m_pMonitorPool->Alloc();
	pData->s_dataType = datatype;
	pData->s_dataValue = dataValue;
	pData->s_serverNo = serverno;
	pData->s_timeStamp = timestamp;

	m_pDBQueue->Enqueue(pData);
	SetEvent(m_DBEvent[0]);
}

void CMonitor::UserTimeOut()
{
	DWORD tick;
	CUser* pUser;
	std::unordered_map<UINT64, CUser*>::iterator it;


	AcquireSRWLockShared(&m_UserMapLock);
	tick = timeGetTime();
	it = m_UserMap.begin();
	for (; it != m_UserMap.end(); ++it)
	{
		pUser = it->second;
		if (tick - pUser->m_recvTime >= USER_TIMEOUT)
		{
			if (pUser->m_TimeOut == true)
				continue;

			Disconnect(it->first);
			pUser->m_TimeOut = true;

			LOG(L"CMonitor", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"FrameThread  UserTimeout / SessionID : %lld /  TimeOut : %lld ...", pUser->m_UniqID, tick - pUser->m_recvTime);
			continue;
		}
	}
	ReleaseSRWLockShared(&m_UserMapLock);
}

void CMonitor::FrameThread()
{
	DWORD oldTick;          // DB 저장 이벤트 처리
	DWORD OldTimeOutTick1;  // 40초 유저 타임아웃
	DWORD curTick;

	oldTick = timeGetTime();
	OldTimeOutTick1 = timeGetTime();

	while (!m_EndFlag)
	{
		Sleep(100);

		curTick = timeGetTime();

		//if (curTick - OldTimeOutTick1 >= USER_TIMEOUT)
		//{
		//	UserTimeOut();
		//	OldTimeOutTick1 += USER_TIMEOUT;
		//}

		if (timeGetTime() - oldTick >= DB_UPDATE_TIME)
		{
			SetEvent(m_DBEvent[1]);
			oldTick += DB_UPDATE_TIME;
		}
	}
}

void CMonitor::DBUpdate()
{
	BOOL esc = false;
	DWORD ret;
	DWORD idx;
	MONITOR_DATA* pdata;

	while (!esc)
	{
		ret = WaitForMultipleObjects(DB_HANDLE_COUNT, m_DBEvent, FALSE, INFINITE);
		idx = ret - WAIT_OBJECT_0;
		if (idx == 0)
		{
			while (m_pDBQueue->GetUseSize() > 0)
			{
				if (!m_pDBQueue->Dequeue(pdata))
					__debugbreak();

				DataInsert(pdata);
				m_pMonitorPool->Free(pdata);
			}
		}

		else if (idx == 1)
		{
			DBInsert();
		}

		else if (idx == 2)
		{
			esc = true;
			continue;
		}
	}
}

void CMonitor::Monitor()
{
	DWORD oldTick = timeGetTime();


	while (!m_EndFlag)
	{
		Sleep(1000);
		m_pPDH->UpdateCounter();
		m_pAgentMgr->SendServerData(MONITOR_SERVER_NO, dfMONITOR_DATA_TYPE_MONITOR_CPU_TOTAL, (INT)m_pPDH->ProcessorTotal(), static_cast<INT>(time(NULL)));
		m_pAgentMgr->SendServerData(MONITOR_SERVER_NO, dfMONITOR_DATA_TYPE_MONITOR_NONPAGED_MEMORY, (INT)(m_pPDH->m_NonPagedMemoryVal.doubleValue / (1024 * 1024)), static_cast<INT>(time(NULL)));
		m_pAgentMgr->SendServerData(MONITOR_SERVER_NO, dfMONITOR_DATA_TYPE_MONITOR_NETWORK_SEND, (INT)((m_pPDH->m_EtherNetSendVal1.doubleValue) / 1024), static_cast<INT>(time(NULL)));
		m_pAgentMgr->SendServerData(MONITOR_SERVER_NO, dfMONITOR_DATA_TYPE_MONITOR_NETWORK_RECV, (INT)((m_pPDH->m_EtherNetRecvVal1.doubleValue) / 1024), static_cast<INT>(time(NULL)));
		m_pAgentMgr->SendServerData(MONITOR_SERVER_NO, dfMONITOR_DATA_TYPE_MONITOR_AVAILABLE_MEMORY, (INT)m_pPDH->m_AvailableMemoryVal.doubleValue, static_cast<INT>(time(NULL)));

		DataEnqueue(static_cast<INT>(time(NULL)), (INT)m_pPDH->ProcessorTotal(), MONITOR_SERVER_NO, dfMONITOR_DATA_TYPE_MONITOR_CPU_TOTAL);
		DataEnqueue(static_cast<INT>(time(NULL)), (INT)(m_pPDH->m_NonPagedMemoryVal.doubleValue / (1024 * 1024)), MONITOR_SERVER_NO, dfMONITOR_DATA_TYPE_MONITOR_NONPAGED_MEMORY);
		DataEnqueue(static_cast<INT>(time(NULL)), (INT)((m_pPDH->m_EtherNetSendVal1.doubleValue) / 1024), MONITOR_SERVER_NO, dfMONITOR_DATA_TYPE_MONITOR_NETWORK_SEND);
		DataEnqueue(static_cast<INT>(time(NULL)), (INT)((m_pPDH->m_EtherNetRecvVal1.doubleValue) / 1024), MONITOR_SERVER_NO, dfMONITOR_DATA_TYPE_MONITOR_NETWORK_RECV);
		DataEnqueue(static_cast<INT>(time(NULL)), (INT)m_pPDH->m_AvailableMemoryVal.doubleValue, MONITOR_SERVER_NO, dfMONITOR_DATA_TYPE_MONITOR_AVAILABLE_MEMORY);


		wprintf(L"CMessagePool    UseCount : %lld \n", CMessage::m_pMessagePool->GetUseCnt());
		wprintf(L"MonitorDataPool UseCount : %d \n", m_pMonitorPool->GetUseCnt());
		wprintf(L"UserPool        UseCount : %lld \n", m_UserMap.size());
		wprintf(L"Agent              Count : %lld \n", m_pAgentMgr->GetAgentsSize());
		wprintf(L"DBQueue             Size : %d \n", m_pDBQueue->GetUseSize());

	}
}

bool CMonitor::OnConnectionRequest(WCHAR* InputIP, unsigned short InputPort)
{
	return true;
}

void CMonitor::OnClientJoin(UINT64 SessionID)
{
	CUser* pUser = m_pUserPool->Alloc();
	pUser->User_Init(SessionID);

	AcquireSRWLockExclusive(&m_UserMapLock);
	m_UserMap.insert(std::pair<UINT64,CUser*>(SessionID,pUser));
	ReleaseSRWLockExclusive(&m_UserMapLock);
}

void CMonitor::OnClientLeave(UINT64 SessionID)
{
	CUser* pUser;
	std::unordered_map<UINT64, CUser*>::iterator it;

	AcquireSRWLockExclusive(&m_UserMapLock);
	it = m_UserMap.find(SessionID);
	pUser = it->second;
	m_UserMap.erase(it);
	ReleaseSRWLockExclusive(&m_UserMapLock);

	m_pUserPool->Free(pUser);
}

void CMonitor::OnRecv(UINT64 SessionID, CMessage* pMessage)
{
	WORD type;
	*pMessage >> type;

	switch (type)
	{
	case en_PACKET_SS_MONITOR_LOGIN:
		ServerLoginProc(SessionID, pMessage);
		break;

	case en_PACKET_SS_MONITOR_DATA_UPDATE:
		DataUpdateProc(SessionID, pMessage);
		break;
	}
}
