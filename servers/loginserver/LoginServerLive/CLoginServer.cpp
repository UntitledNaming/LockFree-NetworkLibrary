#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <timeapi.h>
#include <unordered_map>
#include <thread>
#include <string>
#include <mysql.h>
#include <Pdh.h>
#include <cpp_redis/cpp_redis>
#include <codecvt>

#pragma comment (lib, "cpp_redis.lib")
#pragma comment (lib, "tacopie.lib")

#include "CPUUsage.h"
#include "ProcessMonitor.h"
#include "LibraryHeader.h"
#include "TextParser.h"
#include "CommonProtocol.h"
#include "LogClass.h"
#include "MemoryPoolTLS.h"
#include "CMessage.h"
#include "DBTLS.h"
#include "CUser.h"
#include "LockFreeMemoryPoolLive.h"
#include "LFQMultiLive.h"
#include "Ring_Buffer.h"
#include "CLanClient.h"
#include "CMonitorClient.h"
#include "CNetServer.h"
#include "CLoginServer.h"

#pragma warning(disable : 4996) 

CLoginServer::CLoginServer()
{

}

CLoginServer::~CLoginServer()
{
}

BOOL CLoginServer::RunServer()
{
	std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;

	std::wstring loginipstr;
	std::wstring monitoripstr;

	Parser::st_Msg loginip;
	Parser::st_Msg dbip;
	Parser::st_Msg redisip;
	Parser::st_Msg monitorip;

	INT dbport;
	INT redisport;
	INT loginport;
	INT moitorport;
	INT maxSessions;
	INT createthread;
	INT runningthread;
	INT PACKET_CODE;
	INT PACKET_KEY;
	INT Usermax;
	INT Nagle;
	INT SendFrame;
	INT SendFlag;
	INT Loglevel;


	//Parsing �۾�
	Parser parser;

	if (!parser.LoadFile("LServerConfig.txt"))
	{
		__debugbreak();
		return false;
	}
	parser.GetValue("BIND_IP", &loginip);
	parser.GetValue("DB_IP", &dbip);
	parser.GetValue("REDIS_IP", &redisip);
	parser.GetValue("MORNITOR_IP", &monitorip);


	//char�� ���ڿ��� wchar�� ��ȯ
	loginipstr = converter.from_bytes(loginip.s_ptr);
	monitoripstr = converter.from_bytes(monitorip.s_ptr);


	parser.GetValue("BIND_PORT", &loginport);

	parser.GetValue("SESSION_MAX", &maxSessions);

	parser.GetValue("IOCP_WORKER_THREAD", &createthread);

	parser.GetValue("IOCP_ACTIVE_THREAD", &runningthread);

	parser.GetValue("PACKET_CODE", (int*)&PACKET_CODE);

	parser.GetValue("PACKET_KEY", (int*)&PACKET_KEY);

	parser.GetValue("USER_MAX", (int*)&Usermax);

	parser.GetValue("NAGLE", (int*)&Nagle);

	parser.GetValue("SEND_FRAME", (int*)&SendFrame);

	parser.GetValue("SEND_TH_FLAG", (int*)&SendFlag);

	parser.GetValue("MORNITOR_PORT", &moitorport);

	parser.GetValue("DB_PORT", &dbport);

	parser.GetValue("REDIS_PORT", &redisport);

	parser.GetValue("LOG_LEVEL", &Loglevel);

	CLogClass::GetInstance()->Init(Loglevel);
	CMessage::Init(sizeof(LANHEADER), sizeof(NETHEADER));

	Mem_Init(Usermax, dbip.s_ptr, dbport, redisip.s_ptr, redisport,(WCHAR*)monitoripstr.c_str(), moitorport);

	Thread_Create();

	if (!Start((WCHAR*)loginipstr.c_str(), loginport, createthread, runningthread, maxSessions, SendFrame, SendFlag, PACKET_CODE, PACKET_KEY, (bool)Nagle))
		return false;

	return 0;
}

void CLoginServer::StopServer()
{
	Stop();

	Thread_Destroy();

	m_pMonitorClient->Destroy();

	delete m_DBTLS;
	delete m_pUserPool;
	delete m_pMonitorClient;
	delete m_pPDH;
	delete m_pRedisClient;

	CMessage::PoolDestroy();
}


void CLoginServer::Mem_Init(INT maxUserCnt, const CHAR* DBIp, INT DBPort, const CHAR* RedisIp, INT RedisPort, WCHAR* MonitorIp, INT MonitorPort)
{
	m_MaxUserCnt = maxUserCnt;
	m_pUserPool = new CMemoryPool<CUser>;
	m_pPDH = new ProcessMonitor;
	m_LoginComTPS = 0;
	m_EndFlag = false;

	// DB Init
	m_DBTLS = new DBTLS(DBIp, DBPort);

	// Redis Init
	m_pRedisClient = new cpp_redis::client;
	m_pRedisClient->connect(RedisIp, RedisPort);

	// MonitorClient Init
	m_pMonitorClient = new CMonitorClient(LOGIN_SERVER_NO);

	if (!m_pMonitorClient->Connect(MonitorIp, MonitorPort))
		__debugbreak();

	ServerRoutInfo_Init();
	InitializeSRWLock(&m_UserMapLock);
	InitializeSRWLock(&m_NonUserMapLock);
	InitializeSRWLock(&m_ServerInfoMapLock);
}

void CLoginServer::Thread_Create()
{
	m_Frame = std::thread(&CLoginServer::FrameThread, this);
	m_Monitor = std::thread(&CLoginServer::MonitorThread, this);
}

void CLoginServer::Thread_Destroy()
{
	m_EndFlag = true;

	if (m_Frame.joinable())
	{
		m_Frame.join();
	}

	if (m_Monitor.joinable())
	{
		m_Monitor.join();
	}
}

void CLoginServer::ServerRoutInfo_Init()
{
	// ServerInfo ����
	SERVERINFO* pInfo1 = new SERVERINFO; // ���� 1 ��
	SERVERINFO* pInfo2 = new SERVERINFO; // ���� 2 ��
	INT         gameport;
	INT         chatport;
	Parser      parser;
	if (!parser.LoadFile("ServerRoutingConfig.txt"))
		__debugbreak();

	Parser::st_Msg dummy1;
	Parser::st_Msg dummy1_re;
	Parser::st_Msg dummy2;
	Parser::st_Msg dummy2_re;

	// ���� 1�� IP, Key�� ����
	parser.GetValue("DUMMY_IP1", &dummy1);

	//char�� ���ڿ��� wchar�� ��ȯ
	std::wstring wstr1;
	std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
	wstr1 = converter.from_bytes(dummy1.s_ptr);

	// ���� 2�� IP, Key�� ����
	parser.GetValue("DUMMY_IP2", &dummy2);

	//char�� ���ڿ��� wchar�� ��ȯ
	std::wstring wstr2;
	wstr2 = converter.from_bytes(dummy2.s_ptr);

	// ���� 1 IP���� �� IP ����
	parser.GetValue("DUMMY_IP1_RESULT", &dummy1_re);

	//char�� ���ڿ��� wchar�� ��ȯ
	std::wstring wstr1_re;
	wstr1_re = converter.from_bytes(dummy1_re.s_ptr);

	// ���� 2 IP���� �� IP ����
	parser.GetValue("DUMMY_IP2_RESULT", &dummy2_re);

	//char�� ���ڿ��� wchar�� ��ȯ
	std::wstring wstr2_re;
	wstr2_re = converter.from_bytes(dummy2_re.s_ptr);

	// ���� ���� Port ����
	parser.GetValue("GAME_PORT", &gameport);

	// ä�� ���� Port ����
	parser.GetValue("CHAT_PORT", &chatport);

	// ���� 1 Routing ���� ����
	pInfo1->s_ServerIP = wstr1_re;
	pInfo1->s_GamePort = gameport;
	pInfo1->s_ChatPort = chatport;

	// ���� 2 Routing ���� ����
	pInfo2->s_ServerIP = wstr2_re;
	pInfo2->s_GamePort = gameport;
	pInfo2->s_ChatPort = chatport;

	m_ServerInfoMap.insert(std::pair<std::wstring, SERVERINFO*>(wstr1, pInfo1));
	m_ServerInfoMap.insert(std::pair<std::wstring, SERVERINFO*>(wstr2, pInfo2));
}

bool CLoginServer::OnConnectionRequest(WCHAR* InputIP, unsigned short InputPort)
{
	return true;
}

void CLoginServer::OnClientJoin(UINT64 SessionID)
{
	AcquireSRWLockExclusive(&m_NonUserMapLock);
	m_NonUserMap.insert(std::pair<UINT64, DWORD>(SessionID, timeGetTime()));
	ReleaseSRWLockExclusive(&m_NonUserMapLock);
}

void CLoginServer::OnClientLeave(UINT64 SessionID)
{
	CUser* pUser = nullptr;

	AcquireSRWLockExclusive(&m_NonUserMapLock);
	AcquireSRWLockExclusive(&m_UserMapLock);


	// ���� �ڷᱸ�� ���� ã�ƺ��� ����
	std::unordered_map<UINT64, CUser*>::iterator iton;
	iton = m_UserMap.find(SessionID);
	if (iton == m_UserMap.end())
	{
		// ���� �ڷᱸ���� ������ NonUser �ڷᱸ�� ã��.
		std::unordered_map<UINT64, DWORD>::iterator itNon;
		itNon = m_NonUserMap.find(SessionID);
		if (itNon == m_NonUserMap.end())
		{
			__debugbreak();
		}

		m_NonUserMap.erase(itNon);

		ReleaseSRWLockExclusive(&m_UserMapLock);
		ReleaseSRWLockExclusive(&m_NonUserMapLock);
		return;
	}

	pUser = iton->second;
	m_UserMap.erase(iton);

	ReleaseSRWLockExclusive(&m_UserMapLock);
	ReleaseSRWLockExclusive(&m_NonUserMapLock);

	//Ǯ�� �ݳ�
	m_pUserPool->Free(pUser);
}

void CLoginServer::OnRecv(UINT64 SessionID, CMessage* pMessage)
{
	WORD type;
	*pMessage >> type;
	if (pMessage->GetLastError())
	{
		//�������� ���� ���� ������ ũ�Ⱑ ������ �÷��� ����.
		Disconnect(SessionID);
		LOG(L"LoginServer", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"OnRecv::CMessage Flag Error... \ UniqID : %lld ", SessionID);
		return;
	}

	switch (type)
	{
	case en_PACKET_CS_LOGIN_REQ_LOGIN:
		LoginRequest(pMessage, SessionID);
		break;

	default:
		Disconnect(SessionID);
		LOG(L"LoginServer", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"OnRecv::Message Type Error... \ UniqID : %lld ", SessionID);
		break;
	}

	return;
}

void CLoginServer::LoginRequest(CMessage* pMessage, UINT64 sessionid)
{
	CUser* pUser = nullptr;
	BYTE   Status;
	INT64  AccountNo;
	DWORD  startTime;
	DWORD  endTime;

	CHAR          SessionKey[SESSION_KEY_MAX];
	WCHAR         ID[ID_MAX];
	WCHAR         NICK[NICK_MAX];
	WCHAR         IP[IP_LEN ];
	SERVERINFO*   OutInfo;


	// ó�� �ð� start
	startTime = timeGetTime();

	// ����ȭ ���� ���� üũ
	*pMessage >> AccountNo;

	// ����ȭ ���ۿ� ��� �����Ͱ� ���� �� ����
	if (pMessage->GetData(SessionKey, SESSION_KEY_MAX) == 0)
	{
		Disconnect(sessionid);
		LOG(L"LoginServer", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"LoginRequest::GetData Error... \ UniqID : %lld ", sessionid);
		return;
	}

	if (pMessage->GetLastError())
	{
		//�������� ���� ���� ������ ũ�Ⱑ ������ �÷��� ����.
		Disconnect(sessionid);
		LOG(L"LoginServer", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"LoginRequest::CMessage Flag Error... \ UniqID : %lld ", sessionid);
		return;
	}

	if (pMessage->GetDataSize() > 0)
	{
		//�������� ���� ���� ������ ũ�Ⱑ ũ�� ����
		Disconnect(sessionid);
		LOG(L"LoginServer", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"LoginRequest::CMessage Size Overflow Error... \ UniqID : %lld / Account No : %lld", sessionid, AccountNo);
		return;
	}

	// ������ IP ��� ���� ��ü�� ����
	if (!FindIP(sessionid, IP))
	{
		Disconnect(sessionid);
		return;
	}

	FindServerInfo(IP, &OutInfo);

	// ����� ���̴� �缳ip �뿪�̶� ���� ������� �ʴ� �ٸ� �뿪���� ������ ip �뿪�� ���ͼ� FindServerINfo�Լ����� ��ã�� ���̸� �� ������ ����� ��.
	// ������ ���̿� ���� Ŭ�� �α��� ������ ���� ������ ȯ���̸� ������ ���µ� ���� �׽�Ʈ������ ���̸� ������ ���� ip �뿪�� �ƴ� �ٸ� ip �뿪�� ������ ����� ��.
	if (OutInfo == nullptr)
	{
		Disconnect(sessionid);
		return;
	}

	// DB ���
	GetDBData(ID, NICK, AccountNo);


	// ��ū ������ ��ū ����(�÷����� �߱��� ����key�� Ŭ���̾�Ʈ�� ���԰� �α��� ������ ���� �߱����� �ʰ� �̰� �״�� ����ϴ� ȯ����)
	std::string token(SessionKey, SESSION_KEY_MAX);
	SetRedisToken(AccountNo, token);


	// ���� ��ü �ʱ�ȭ
	pUser = m_pUserPool->Alloc();
	pUser->User_Init(sessionid, AccountNo);

	// ���� �ڷᱸ���� �ֱ�
	UserInsert(sessionid, pUser);

	Status = dfLOGIN_STATUS_OK;

	//�α��� ���� �α��� ���� ��Ŷ ���� �� ����
	WCHAR* OutIP;
	CMessage* pPacket = CMessage::Alloc();
	pPacket->Clear();

	*pPacket << (WORD)en_PACKET_CS_LOGIN_RES_LOGIN;
	*pPacket << AccountNo;
	*pPacket << Status;
	pPacket->PutData((CHAR*)ID, ID_MAX * sizeof(WCHAR));
	pPacket->PutData((CHAR*)NICK, NICK_MAX * sizeof(WCHAR));
	pPacket->PutData((CHAR*)OutInfo->s_ServerIP.c_str(), IP_LEN * sizeof(WCHAR));
	*pPacket << OutInfo->s_GamePort;
	pPacket->PutData((CHAR*)OutInfo->s_ServerIP.c_str(), IP_LEN * sizeof(WCHAR));
	*pPacket << OutInfo->s_ChatPort;

	SendPacket(sessionid, pPacket);

	CMessage::Free(pPacket);

	endTime = timeGetTime();

	// �α��� ��û ó�� �ð� üũ
	if (endTime - startTime >= 2000)
	{
		LOG(L"LoginServer", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L"LoginRequest::LoginRequest Proc Time Too Long \ UniqID : %lld / Account No : %lld / Time : %lld", sessionid, pUser->s_AccountNo, endTime - startTime);
	}

	InterlockedIncrement((long*)&m_LoginComTPS);
}

void CLoginServer::FindServerInfo(WCHAR* keyIp, SERVERINFO** OutIP)
{
	std::unordered_map<std::wstring, SERVERINFO*>::iterator it;
	std::wstring key(keyIp);

	AcquireSRWLockShared(&m_ServerInfoMapLock);
	it = m_ServerInfoMap.find(key);
	if (it == m_ServerInfoMap.end())
	{
		*OutIP = nullptr;
		ReleaseSRWLockShared(&m_ServerInfoMapLock);
		return;
	}

	*OutIP = it->second;
	ReleaseSRWLockShared(&m_ServerInfoMapLock);

}


void CLoginServer::GetDBData(WCHAR* id, WCHAR* nick, UINT64 accountNo)
{
	MYSQL_RES*    sql_result;
	MYSQL_ROW*    sql_row;
	INT           IDLen;
	INT           NICKLen;


	if (!m_DBTLS->DB_Post_Query("SELECT userid, usernick From accountdb.account WHERE accountno = %lld", accountNo))
		__debugbreak();

	sql_result = m_DBTLS->DB_GET_Result(DBTLS::en_Use);
	if (sql_result == nullptr)
		__debugbreak();

	sql_row = m_DBTLS->DB_Fetch_Row(sql_result);


	// sql_row[0]�� userid �÷� �����Ͱ� ���ڿ� ���·� ����Ǿ� �ִµ� �� �ּҰ��� �����.
	IDLen = strlen((*sql_row)[0]);
	NICKLen = strlen((*sql_row)[1]);


	for (int i = 0; i < IDLen; i++)
	{
		id[i] = (WCHAR)(*sql_row)[0][i];
	}
	id[IDLen] = NULL;

	for (int i = 0; i < NICKLen; i++)
	{
		nick[i] = (WCHAR)(*sql_row)[1][i];
	}
	nick[NICKLen] = NULL;


	m_DBTLS->DB_Free_Result();
}

void CLoginServer::SetRedisToken(UINT64 accountNo, const std::string& token)
{
	m_pRedisClient->set(std::to_string(accountNo), token);
	m_pRedisClient->sync_commit();
}

void CLoginServer::UserInsert(UINT64 sessionID, CUser* pUser)
{
	AcquireSRWLockExclusive(&m_NonUserMapLock);
	AcquireSRWLockExclusive(&m_UserMapLock);

	std::unordered_map<UINT64, DWORD>::iterator itNon;
	itNon = m_NonUserMap.find(sessionID);
	if (itNon == m_NonUserMap.end())
	{
		__debugbreak();
	}

	m_NonUserMap.erase(itNon);
	m_UserMap.insert(std::pair<UINT64, CUser*>(sessionID, pUser));

	ReleaseSRWLockExclusive(&m_UserMapLock);
	ReleaseSRWLockExclusive(&m_NonUserMapLock);

}


void CLoginServer::NonUserTimeOut()
{
	DWORD tick;
	std::unordered_map<UINT64, DWORD>::iterator it;

	AcquireSRWLockShared(&m_NonUserMapLock);
	tick = timeGetTime();
	it = m_NonUserMap.begin();
	for (; it != m_NonUserMap.end(); ++it)
	{
		if (tick - it->second >= TIMEOUT1)
		{
			Disconnect(it->first);
			LOG(L"Login", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"FrameThread  NonUserTimeout / SessionID : %lld / TimeOut : %d ", it->first, tick - it->second);
			continue;
		}
	}
	ReleaseSRWLockShared(&m_NonUserMapLock);
}

void CLoginServer::UserTimeOut()
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
		if (tick - pUser->s_RecvTime >= TIMEOUT2)
		{
			if (pUser->s_TimeOut == 1)
				continue;

			Disconnect(it->first);
			pUser->s_TimeOut = 1;

			LOG(L"LoginServer", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"FrameThread  UserTimeout / SessionID : %lld / AccountNo : %lld / TimeOut : %lld ...", pUser->s_UniqID, pUser->s_AccountNo, tick - pUser->s_RecvTime);
			continue;
		}
	}
	ReleaseSRWLockShared(&m_UserMapLock);
}

void CLoginServer::FrameThread()
{
	DWORD OldTimeOutTick1; //2��  Ÿ�Ӿƿ�
	DWORD OldTimeOutTick2; //40�� Ÿ�Ӿƿ�
	DWORD curTick;

	OldTimeOutTick1 = timeGetTime();
	OldTimeOutTick2 = timeGetTime();

	while (!m_EndFlag)
	{
		Sleep(100);

		curTick = timeGetTime();

		//if (curTick - OldTimeOutTick1 >= TIMEOUT1)
		//{
		//	NonUserTimeOut();

		//	OldTimeOutTick1 += TIMEOUT1;
		//}

		//if (curTick - OldTimeOutTick2 >= TIMEOUT2)
		//{
		//	UserTimeOut();
		//	OldTimeOutTick2 += TIMEOUT2;
		//}

	}
}

void CLoginServer::MonitorThread()
{
	time_t start;
	tm* local_time;
	INT64 loopCnt = 1;
	INT64 SendIOSum = 0;
	INT64 RecvIOSum = 0;
	INT64 ReqMsgSum = 0;
	INT64 ResMsgSum = 0;
	INT64 UpdateSum = 0;
	INT64 LoginResSum = 0;
	INT64 SectorMoveSum = 0;
	INT64 ChatMsgSum = 0;
	INT64 AcptTPSSum = 0;
	INT64 MsgQSizeSum = 0;
	INT64 CPoolSum = 0;
	INT64 CPoolSendSum = 0;
	INT64 SessionSendMsgCntSum = 0;

	float processtotalsum = 0;
	float processusersum = 0;
	float processkernelsum = 0;
	double tcpretransmitsum = 0;
	double tcpsegmentsentsum = 0;
	double ethernet1sendsum = 0;
	double ethernet2sendsum = 0;

	double tcpretranslog = 0;

	start = time(NULL);
	local_time = localtime(&start);

	while (!m_EndFlag)
	{
		Sleep(1000);

		m_pPDH->UpdateCounter();

		processtotalsum += m_pPDH->ProcessTotal();
		processusersum += m_pPDH->ProcessUser();
		processkernelsum += m_pPDH->ProcessKernel();
		tcpretransmitsum += m_pPDH->m_TCPReTransmitVal.doubleValue;
		tcpsegmentsentsum += m_pPDH->m_TCPSegmentSentVal.doubleValue;
		ethernet1sendsum += m_pPDH->m_EtherNetSendVal1.doubleValue;
		ethernet2sendsum += m_pPDH->m_EtherNetSendVal2.doubleValue;

		wprintf(L"Start Time : %04d / %02d / %02d, %02d:%02d:%02d\n",
			local_time->tm_year + 1900,
			local_time->tm_mon + 1,
			local_time->tm_mday,
			local_time->tm_hour,
			local_time->tm_min,
			local_time->tm_sec);
		wprintf(L"/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////\n");
		wprintf(L"[ User      / NonUser     Count :  %lld / %lld ]\n", m_UserMap.size(), m_NonUserMap.size());
		wprintf(L"[ MessagePool             Count :  %d ]\n", CMessage::m_pMessagePool->GetUseCnt());
		wprintf(L"[ Login Complte            TPS  :  %d]\n\n", m_LoginComTPS);
		wprintf(L"[ Accept Total             TPS  :  %lld]\n\n", m_AcceptTotal);
		wprintf(L"[ CPU Usage : T[%f%] U[%f%] K[%f%]]\n", processtotalsum / loopCnt, processusersum / loopCnt, processkernelsum / loopCnt);
		wprintf(L"[ Available        Memory Usage : %lf MByte ] [ NonPagedMemory Usage : %lf MByte ]\n", m_pPDH->m_AvailableMemoryVal.doubleValue / (1024 * 1024), m_pPDH->m_NonPagedMemoryVal.doubleValue / (1024 * 1024));
		wprintf(L"[ Process User     Memory Usage : %lf MByte ]  [ Process NonPaged Memory Usage : %lf KByte ]\n", m_pPDH->m_processUserMemoryVal.doubleValue / (1024 * 1024), m_pPDH->m_processNonPagedMemoryVal.doubleValue / 1024);
		wprintf(L"[ TCP Retransmitted Avg   Count : %lf /sec  ]  [ TCP Segment Sent  Avg   Count : % lf / sec]\n", tcpretransmitsum / loopCnt, tcpsegmentsentsum / loopCnt);
		wprintf(L"[ Ethernet Send     Avg   Byte  : %lf KByte/sec]\n", ((ethernet1sendsum + ethernet1sendsum) / (1024)) / loopCnt);
		wprintf(L"/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////\n");

		if (m_pMonitorClient->ConnectAlive())
		{
			m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_LOGIN_SERVER_RUN, 1);
			m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_LOGIN_SERVER_CPU, (int)m_pPDH->ProcessTotal());
			m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_LOGIN_SERVER_MEM, (int)(m_pPDH->m_processUserMemoryVal.doubleValue / (1024 * 1024)));
			m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_LOGIN_SESSION, m_CurSessionCnt);
			m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_LOGIN_AUTH_TPS, m_LoginComTPS);
			m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_LOGIN_PACKET_POOL, CMessage::m_pMessagePool->GetUseCnt());
		}
		else
		{
			if (m_pMonitorClient->ReConnect())
			{
				m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_LOGIN_SERVER_RUN, 1);
				m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_LOGIN_SERVER_CPU, (int)m_pPDH->ProcessTotal());
				m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_LOGIN_SERVER_MEM, (int)(m_pPDH->m_processUserMemoryVal.doubleValue / (1024 * 1024)));
				m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_LOGIN_SESSION, m_CurSessionCnt);
				m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_LOGIN_AUTH_TPS, m_LoginComTPS);
				m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_LOGIN_PACKET_POOL, CMessage::m_pMessagePool->GetUseCnt());
			}
		}


		m_LoginComTPS = 0;

		loopCnt++;
	}
}

