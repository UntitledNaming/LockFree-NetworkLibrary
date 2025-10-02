#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <timeapi.h>
#include <unordered_map>
#include <unordered_set>
#include <list>
#include <thread>
#include <string.h>
#include <iostream>
#include <codecvt>
#include <Pdh.h>
#include <time.h>

#pragma comment(lib,"Pdh.lib")

#include "CommonProtocol.h"
#include "TextParser.h"
#include "LibraryHeader.h"
#include "LogClass.h"
#include "CPUUsage.h"
#include "ProcessMonitor.h"
#include "MemoryPoolTLS.h"
#include "CMessage.h"

#include "LFQSingleLive.h"

#include "LFStack.h"
#include "Ring_Buffer.h"
#include "LFQMultiLive.h"
#include "CSector.h"
#include "CUser.h"
#include "CLanClient.h"
#include "LockFreeMemoryPoolLive.h"


#include "CMonitorClient.h"
#include "CSession.h"
#include "CNetServer.h"
#include "ChatServer.h"

#pragma warning(disable : 4996) 


ChatServer::ChatServer()
{
}

ChatServer::~ChatServer()
{
}

BOOL ChatServer::RunServer()
{
	//Parsing 작업
	Parser parser;

	if (!parser.LoadFile("CServerConfig.txt"))
		return false;

	Parser::st_Msg bind;
	parser.GetValue("BIND_IP", &bind);

	Parser::st_Msg monitor;
	parser.GetValue("MORNITOR_IP", &monitor);

	std::wstring monitorstr;
	std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
	monitorstr = converter.from_bytes(monitor.s_ptr);


	//char형 문자열을 wchar로 변환
	std::wstring bindstr;
	bindstr = converter.from_bytes(bind.s_ptr);

	INT monitorport;
	parser.GetValue("MORNITOR_PORT", &monitorport);

	INT port;
	parser.GetValue("BIND_PORT", &port);

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

	INT Usermax;
	parser.GetValue("USER_MAX", (int*)&Usermax);

	INT Nagle;
	parser.GetValue("NAGLE", (int*)&Nagle);

	INT SendFrame;
	parser.GetValue("SEND_FRAME", (int*)&SendFrame);

	INT Sendflag;
	parser.GetValue("SEND_TH_FLAG", (int*)&Sendflag);

	INT Loglevel;
	parser.GetValue("LOG_LEVEL", (int*)&Loglevel);

#pragma endregion

	CLogClass::GetInstance()->Init(Loglevel);
	CMessage::Init(sizeof(LANHEADER), sizeof(NETHEADER));

	Mem_Init(Usermax, const_cast<WCHAR*>(bindstr.c_str()), const_cast<WCHAR*>(monitorstr.c_str()), monitorport);

	if (!Start((WCHAR*)bindstr.c_str(), port, createthread, runningthread, maxSessions, SendFrame, Sendflag, PACKET_CODE, PACKET_KEY, (bool)Nagle))
		return false;


}

void ChatServer::StopServer()
{
	Stop();

	Thread_Destroy();
	
	m_pMonitorClient->Destroy();

	delete m_pMonitorClient;
	delete m_pUserPool;
	delete m_pPDH;

	CMessage::PoolDestroy();
}

void ChatServer::Mem_Init(INT userMAX, WCHAR* bindip, WCHAR* serverip, INT serverport)
{
	m_UserMaxCnt = userMAX;
	m_pUserPool = new CMemoryPool<CUser>;
	m_pPDH = new ProcessMonitor;
	m_pMonitorClient = new CMonitorClient(CHAT_SERVER_NO);

	m_ReqTPS = 0;
	m_ReqLoginTPS = 0;
	m_ResLoginTPS = 0;
	m_ReqMoveTPS = 0;
	m_ResMoveTPS = 0;
	m_ReqChatMsgTPS = 0;
	m_ResChatMsgTPS = 0;
	m_EndFlag = false;

	InitializeSRWLock(&m_UserMapLock);
	InitializeSRWLock(&m_NonUserMapLock);
	for (int y = 0; y < SECTOR_Y_MAX; y++)
	{
		for (int x = 0; x < SECTOR_X_MAX; x++)
		{
			InitializeSRWLock(&m_Sector[y][x].s_lock);
		}
	}

	// 모니터링 서버에 연결
	//if (!m_pMonitorClient->Connect(bindip, serverip, serverport))
	//	__debugbreak();


	Thread_Create();
}

void ChatServer::Thread_Create()
{
	m_Frame = std::thread(&ChatServer::FrameThread, this);
	m_Monitor = std::thread(&ChatServer::MonitorThread, this);
}

void ChatServer::Thread_Destroy()
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

UINT64 ChatServer::SumResMsgTPS()
{
	return m_ResLoginTPS + m_ResChatMsgTPS + m_ResMoveTPS;
}

bool ChatServer::OnConnectionRequest(WCHAR* InputIP, USHORT InputPort)
{

	return true;
}

void ChatServer::OnClientJoin(UINT64 SessionID)
{
	AcquireSRWLockExclusive(&m_NonUserMapLock);
	m_NonUserMap.insert(std::pair<UINT64, DWORD>(SessionID, timeGetTime()));
	ReleaseSRWLockExclusive(&m_NonUserMapLock);
}

void ChatServer::OnClientLeave(UINT64 SessionID)
{
	std::unordered_map<UINT64, CUser*>::iterator iton;
	std::unordered_map<UINT64, DWORD>::iterator itNon;
	CUser* pUser = nullptr;

	//NonUser 제거
	AcquireSRWLockExclusive(&m_NonUserMapLock);
	itNon = m_NonUserMap.find(SessionID);
	if (itNon != m_NonUserMap.end())
	{
		// 있으면 제거하고 끝. 더 할 처리 없음.
		m_NonUserMap.erase(itNon);
		ReleaseSRWLockExclusive(&m_NonUserMapLock);
		return;
	}
	ReleaseSRWLockExclusive(&m_NonUserMapLock);

	// NonUser에 없으면 User에서 찾기(UserMap에도 없으면 서버 로직을 잘못짠것)
	AcquireSRWLockExclusive(&m_UserMapLock);
	iton = m_UserMap.find(SessionID);
	if (iton == m_UserMap.end())
		__debugbreak();

	pUser = iton->second;
	m_UserMap.erase(iton);
	ReleaseSRWLockExclusive(&m_UserMapLock);

	if (pUser->s_Sector == 1)
	{
		INT xpos = pUser->s_Pos.s_xpos;
		INT ypos = pUser->s_Pos.s_ypos;

		AcquireSRWLockExclusive(&m_Sector[ypos][xpos].s_lock);
		std::list<CUser*>::iterator it = m_Sector[ypos][xpos].s_list.begin();
		for (; it != m_Sector[ypos][xpos].s_list.end(); ++it)
		{
			//섹터에서 유저 찾았으면
			if (pUser == *it)
			{
				m_Sector[ypos][xpos].s_list.erase(it);
				break;
			}
		}

		ReleaseSRWLockExclusive(&m_Sector[ypos][xpos].s_lock);
	}

	//풀에 반납
	m_pUserPool->Free(pUser);
}

void ChatServer::OnRecv(UINT64 SessionID, CMessage* pMessage)
{
	WORD type;
	*pMessage >> type;

	if (pMessage->GetLastError())
	{
		Disconnect(SessionID);
		LOG(L"ChatServer", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"UserRecvMsg::CMessage Flag Error...  \ UniqID : %llu ", SessionID);
		return;
	}

	m_ReqTPS++;

	switch (type)
	{
	case en_PACKET_CS_CHAT_REQ_LOGIN:
		m_ReqLoginTPS++;
		LoginProc(pMessage, SessionID);
		break;

	case en_PACKET_CS_CHAT_REQ_SECTOR_MOVE:
		m_ReqMoveTPS++;
		SectorMoveProc(pMessage, SessionID);
		break;

	case en_PACKET_CS_CHAT_REQ_MESSAGE:
		m_ReqChatMsgTPS++;
		ChatMessageProc(pMessage, SessionID);
		break;

	case en_PACKET_CS_CHAT_REQ_HEARTBEAT:
		HeartBeatProc(SessionID);
		break;

	default:
		Disconnect(SessionID);
		break;

	}

	return;
}

void ChatServer::NonUserTimeOut()
{
	DWORD tick;
	std::unordered_map<UINT64, DWORD>::iterator it;

	AcquireSRWLockShared(&m_NonUserMapLock);
	it = m_NonUserMap.begin();
	for (; it != m_NonUserMap.end(); ++it)
	{
		tick = timeGetTime();
		if (tick - it->second >= df_TIMEOUT1)
		{
			CNetServer::Disconnect(it->first);
			LOG(L"ChatServer", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"UpdateThread  NonUserTimeout / SessionID : %llu / TimeOut : %d ", it->first, tick - it->second);
			continue;
		}
	}
	ReleaseSRWLockShared(&m_NonUserMapLock);
}

void ChatServer::UserTimeOut()
{
	CUser* pUser = nullptr;
	DWORD curTick;
	std::unordered_map<UINT64, CUser*>::iterator it;
	
	AcquireSRWLockShared(&m_UserMapLock);
	it = m_UserMap.begin();
	for (; it != m_UserMap.end(); ++it)
	{
		pUser = it->second;
		curTick = timeGetTime();
		if (curTick - pUser->s_RecvTime >= df_TIMEOUT2)
		{
			if (pUser->s_TimeOut == 1)
				continue;

			Disconnect(pUser->s_UniqID);
			pUser->s_TimeOut = 1;
			LOG(L"ChatServer", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"UpdateThread  UserTimeout / SessionID : %lld / AccountNo : %lld / TimeOut : %lld ...", it->first, it->second->s_AccountNo, curTick - it->second->s_RecvTime);
			continue;
		}

	}
	ReleaseSRWLockShared(&m_UserMapLock);
}

void ChatServer::AcquireSectorExclusiveLock(WORD eXpos, WORD eYpos, WORD iXpos, WORD iYpos)
{
	// 좌상단에서 우하단 순으로 Exclusive Lock걸기
    // (1 row) --->  이런 순으로 Shared가 Lock걸고 있음. 그래서 y좌표 부터 판단해서 먼저 어디 좌표 부터 Lock걸지 판단 가능함.
    // (2 row) --->
    // (3 row) --->
	if (eYpos - iYpos < 0)
	{
		AcquireSRWLockExclusive(&m_Sector[eYpos][eXpos].s_lock);
		AcquireSRWLockExclusive(&m_Sector[iYpos][iXpos].s_lock);
	}
	else if (eYpos - iYpos > 0)
	{
		AcquireSRWLockExclusive(&m_Sector[iYpos][iXpos].s_lock);
		AcquireSRWLockExclusive(&m_Sector[eYpos][eXpos].s_lock);
	}
	else
	{
		//x좌표까지 비교
		if (eXpos - iXpos < 0)
		{
			AcquireSRWLockExclusive(&m_Sector[eYpos][eXpos].s_lock);
			AcquireSRWLockExclusive(&m_Sector[iYpos][iXpos].s_lock);
		}
		else if (eXpos - iXpos > 0)
		{
			AcquireSRWLockExclusive(&m_Sector[iYpos][iXpos].s_lock);
			AcquireSRWLockExclusive(&m_Sector[eYpos][eXpos].s_lock);
		}
		else
			__debugbreak();
	}
}

void ChatServer::ReleaseSectorExclusiveLock(WORD eXpos, WORD eYpos, WORD iXpos, WORD iYpos)
{
	ReleaseSRWLockExclusive(&m_Sector[eYpos][eXpos].s_lock);
	ReleaseSRWLockExclusive(&m_Sector[iYpos][iXpos].s_lock);
}

void ChatServer::SendPacket_SectorOne(CMessage* pMessage, WORD xpos, WORD ypos, CUser* pUser)
{
	//섹터에 존재하는 pUser 제외하고 보냄.
	std::list<CUser*>::iterator it = m_Sector[ypos][xpos].s_list.begin();

	for (; it != m_Sector[ypos][xpos].s_list.end(); ++it)
	{
		SendPacket((*it)->s_UniqID, pMessage);
		m_ResChatMsgTPS++;
	}
}

void ChatServer::SendPakcet_SectorAround(CMessage* pMessage, CUser* pUser)
{
	//pUser 주위로 메세지 뿌리기
	SECTOR_AROUND around;

	//around의 섹터 배열 0번째는 반드시 자기 자신 섹터 좌표임.
	SectorFind(&around, pUser->s_Pos.s_xpos, pUser->s_Pos.s_ypos);

	//내 주위 섹터 Lock 걸기
	for (int i = 0; i < around.s_Cnt; i++)
	{
		AcquireSRWLockShared(&m_Sector[around.s_Around[i].s_ypos][around.s_Around[i].s_xpos].s_lock);
	}

	// 섹터에 보내고 바로 Lock 풀어도 다른 섹터에 같은 유저가 와서 메세지 2개 보내는 일 없음. 그 다른 섹터에 이미 Lock을 걸어놨기 때문임.
	for (int i = 0; i < around.s_Cnt; i++)
	{
		SendPacket_SectorOne(pMessage, around.s_Around[i].s_xpos, around.s_Around[i].s_ypos, pUser);
		ReleaseSRWLockShared(&m_Sector[around.s_Around[i].s_ypos][around.s_Around[i].s_xpos].s_lock);
	}

}

void ChatServer::SectorFind(SECTOR_AROUND* pAround, WORD xpos, WORD ypos)
{
	INT cnt = 0;

	if (SectorRangeCheck(xpos - 1, ypos - 1))
	{
		pAround->s_Around[cnt].s_xpos = xpos - 1;
		pAround->s_Around[cnt].s_ypos = ypos - 1;
		cnt++;
	}

	if (SectorRangeCheck(xpos, ypos - 1))
	{
		pAround->s_Around[cnt].s_xpos = xpos;
		pAround->s_Around[cnt].s_ypos = ypos - 1;
		cnt++;
	}

	if (SectorRangeCheck(xpos + 1, ypos - 1))
	{
		pAround->s_Around[cnt].s_xpos = xpos + 1;
		pAround->s_Around[cnt].s_ypos = ypos - 1;
		cnt++;
	}

	if (SectorRangeCheck(xpos - 1, ypos))
	{
		pAround->s_Around[cnt].s_xpos = xpos - 1;
		pAround->s_Around[cnt].s_ypos = ypos;
		cnt++;
	}

	//본인 자신 섹터
	pAround->s_Around[cnt].s_xpos = xpos;
	pAround->s_Around[cnt].s_ypos = ypos;
	cnt++;


	if (SectorRangeCheck(xpos + 1, ypos))
	{
		pAround->s_Around[cnt].s_xpos = xpos + 1;
		pAround->s_Around[cnt].s_ypos = ypos;
		cnt++;
	}

	if (SectorRangeCheck(xpos - 1, ypos + 1))
	{
		pAround->s_Around[cnt].s_xpos = xpos - 1;
		pAround->s_Around[cnt].s_ypos = ypos + 1;
		cnt++;
	}

	if (SectorRangeCheck(xpos, ypos + 1))
	{
		pAround->s_Around[cnt].s_xpos = xpos;
		pAround->s_Around[cnt].s_ypos = ypos + 1;
		cnt++;
	}

	if (SectorRangeCheck(xpos + 1, ypos + 1))
	{
		pAround->s_Around[cnt].s_xpos = xpos + 1;
		pAround->s_Around[cnt].s_ypos = ypos + 1;
		cnt++;
	}

	pAround->s_Cnt = cnt;
}

BOOL ChatServer::SectorRangeCheck(WORD xpos, WORD ypos)
{
	if (xpos < 0 || ypos < 0 || xpos >= SECTOR_X_MAX || ypos >= SECTOR_Y_MAX)
		return false;

	return true;
}

void ChatServer::LoginProc(CMessage* pMessage, UINT64 sessionid)
{
	BYTE  Status;
	INT64 accountno;
	WCHAR ID[ID_MAX];
	WCHAR NICK[NICK_MAX];
	CHAR  sessionkey[SESSION_KEY_MAX];

	CUser* pUser = nullptr;
	std::unordered_map<UINT64, DWORD>::iterator itNon;

	if (m_UserMap.size() >= m_UserMaxCnt)
	{
		LOG(L"ChatServer", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"LoginRequest::UserMax \ UniqID : %lld ", sessionid);
		Disconnect(sessionid);
		return;
	}

	*pMessage >> accountno;
	pMessage->GetData((char*)ID, ID_MAX * sizeof(WCHAR));
	pMessage->GetData((char*)NICK, NICK_MAX * sizeof(WCHAR));
	pMessage->GetData(sessionkey,SESSION_KEY_MAX);

	if (pMessage->GetLastError())
	{
		LOG(L"ChatServer", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"LoginRequest::CMessage Flag Error... \ UniqID : %lld", sessionid);

		//프로토콜 보다 보낸 데이터 크기가 적으면 플래그 켜짐.
		Disconnect(sessionid);


		return;
	}

	if (pMessage->GetDataSize() > 0)
	{
		LOG(L"ChatServer", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"LoginRequest::CMessage Size Overflow Error... \ UniqID : %lld ", sessionid);
		//프로토콜 보다 보낸 데이터 크기가 크면 끊기
		Disconnect(sessionid);

		return;
	}

	Status = 1;

	//유저객체 초기화
	pUser = m_pUserPool->Alloc();
	pUser->User_Init(sessionid, accountno, ID, NICK);

	//NonUser 구조에서 찾아서 User에 넣기
	AcquireSRWLockExclusive(&m_NonUserMapLock);
	AcquireSRWLockExclusive(&m_UserMapLock);

	itNon = m_NonUserMap.find(sessionid);
	if (itNon == m_NonUserMap.end())
		__debugbreak();

	//이제 진짜 유저니 NonUser에서 제거해야 함.
	m_NonUserMap.erase(itNon);
	m_UserMap.insert(std::pair<UINT64, CUser*>(sessionid, pUser));

	ReleaseSRWLockExclusive(&m_NonUserMapLock);
	ReleaseSRWLockExclusive(&m_UserMapLock);



	//채팅서버 로그인 응답 패킷 생성 및 전송
	CMessage* pPacket = CMessage::Alloc();
	pPacket->Clear();

	*pPacket << (WORD)en_PACKET_CS_CHAT_RES_LOGIN;
	*pPacket << Status;
	*pPacket << pUser->s_AccountNo;

	SendPacket(sessionid, pPacket);

	m_ResLoginTPS++;

	CMessage::Free(pPacket);

}

void ChatServer::SectorMoveProc(CMessage* pMessage, UINT64 sessionid)
{
	//유저 자료 구조에서 찾기
	std::unordered_map<UINT64, CUser*>::iterator itOn;
	CUser* pUser;
	INT64 AcntNo;
	SECTOR pos;


	// 유저 찾기
	AcquireSRWLockShared(&m_UserMapLock);
	itOn = m_UserMap.find(sessionid);
	if (itOn == m_UserMap.end())
	{
		std::unordered_map<UINT64, DWORD>::iterator itNon = m_NonUserMap.find(sessionid);
		if (itNon == m_NonUserMap.end())
			__debugbreak();
		ReleaseSRWLockShared(&m_UserMapLock);


		// LoginProc 처리 전에 메세지 먼저 온 경우는 상대방 끊기
		LOG(L"ChatServer", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"SectorMoveRequest::UserMap Not Exist NonUserMap Exist Error... \ UniqID : %lld ", sessionid);

		CNetServer::Disconnect(sessionid);
		return;
	}

	pUser = itOn->second;
	ReleaseSRWLockShared(&m_UserMapLock);


	//찾았으면
	*pMessage >> AcntNo;
	*pMessage >> pos.s_xpos;
	*pMessage >> pos.s_ypos;

	if (pMessage->GetLastError())
	{
		LOG(L"ChatServer", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"SectorMoveRequest::CMessage Flag Error... \ UniqID : %llu  / AccountNo : %lld ", sessionid, pUser->s_AccountNo);
		//프로토콜 보다 보낸 데이터 크기가 적으면 플래그 켜짐.
		Disconnect(sessionid);
		return;
	}

	if (pMessage->GetDataSize() > 0)
	{
		LOG(L"ChatServer", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"SectorMoveRequest::CMessage Size Overflow Error... \ UniqID : %llu  / AccountNo : %lld ", sessionid, pUser->s_AccountNo);
		//프로토콜 보다 보낸 데이터 크기가 크면 끊기
		Disconnect(sessionid);
		return;
	}



	if (!SectorRangeCheck(pos.s_xpos, pos.s_ypos))
	{
		LOG(L"ChatServer", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"SectorMoveRequest::Sector Arrang Error... \ UniqID : %llu  / AccountNo : %lld ", sessionid, pUser->s_AccountNo);
		Disconnect(sessionid);
		return;
	}

	//섹터 이동 메세지 처음 온 경우 그냥 변경 섹터에만 넣어주면 됨. 아니면 기존 섹터에서 삭제하고 변경 해야 함.
	if (pUser->s_Sector == 0)
	{
		AcquireSRWLockExclusive(&m_Sector[pos.s_ypos][pos.s_xpos].s_lock);
		m_Sector[pos.s_ypos][pos.s_xpos].s_list.push_back(pUser);
		ReleaseSRWLockExclusive(&m_Sector[pos.s_ypos][pos.s_xpos].s_lock);
	}
	else
	{
		//유저가 어떤 섹터에도 없는 상태 막기 위해 2개 Lock 다 얻기고 처리(클라이언트가 같은 좌표에 대해서 또 SectorMove 보낼 수 있어서 같은 좌표에 Lock 2번 걸면 데드락)
		if (pUser->s_Pos.s_xpos != pos.s_xpos || pUser->s_Pos.s_ypos != pos.s_ypos)
		{
			AcquireSectorExclusiveLock(pUser->s_Pos.s_xpos, pUser->s_Pos.s_ypos, pos.s_xpos, pos.s_ypos);

			std::list<CUser*>::iterator it = m_Sector[pUser->s_Pos.s_ypos][pUser->s_Pos.s_xpos].s_list.begin();
			for (; it != m_Sector[pUser->s_Pos.s_ypos][pUser->s_Pos.s_xpos].s_list.end(); ++it)
			{
				//섹터에서 유저 찾았으면
				if (pUser == *it)
				{
					m_Sector[pUser->s_Pos.s_ypos][pUser->s_Pos.s_xpos].s_list.erase(it);
					m_Sector[pos.s_ypos][pos.s_xpos].s_list.push_back(pUser);
					break;
				}
			}

			ReleaseSectorExclusiveLock(pUser->s_Pos.s_xpos, pUser->s_Pos.s_ypos, pos.s_xpos, pos.s_ypos);

		}
	}

	pUser->s_Sector = 1;
	pUser->s_Pos.s_xpos = pos.s_xpos;
	pUser->s_Pos.s_ypos = pos.s_ypos;
	pUser->s_RecvTime = timeGetTime();

	//섹터 이동 응답 패킷 뿌리기
	CMessage* pPacket = CMessage::Alloc();
	pPacket->Clear();
	*pPacket << (WORD)en_PACKET_CS_CHAT_RES_SECTOR_MOVE;
	*pPacket << AcntNo;
	*pPacket << pos.s_xpos;
	*pPacket << pos.s_ypos;

	SendPacket(sessionid, pPacket);

	m_ResMoveTPS++;

	CMessage::Free(pPacket);

}

void ChatServer::ChatMessageProc(CMessage* pMessage, UINT64 sessionid)
{
	std::unordered_map<UINT64, CUser*>::iterator itOn;
	CUser* pUser;
	WORD   MsgLen;
	WCHAR  Message[MESSAGE_LEN_MAX];

	//유저 자료 구조에서 찾기
	AcquireSRWLockShared(&m_UserMapLock);
	itOn = m_UserMap.find(sessionid);
	if (itOn == m_UserMap.end())
	{
		std::unordered_map<UINT64, DWORD>::iterator itNon = m_NonUserMap.find(sessionid);
		if (itNon == m_NonUserMap.end())
			__debugbreak();
		ReleaseSRWLockShared(&m_UserMapLock);


		// LoginProc 처리 전에 메세지 먼저 온 경우는 상대방 끊기
		LOG(L"ChatServer", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"ChatMessageRequest::UserMap Not Exist NonUserMap Exist Error... \ UniqID : %lld ", sessionid);

		Disconnect(sessionid);
		return;
	}

	//찾았으면
	pUser = itOn->second;
	ReleaseSRWLockShared(&m_UserMapLock);

	// 섹터 변경 메세지 보다 먼저 온 경우
	if (pUser->s_Sector != 1)
	{
		LOG(L"ChatServer", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"ChatMessageRequest::Not Recv SectorMove Message ... \ UniqID : %llu  / AccountNo : %lld ", sessionid, pUser->s_AccountNo);
		Disconnect(sessionid);
		return;
	}



	//요청 메세지 처리
	pMessage->MoveReadPos(sizeof(INT64)); //AccoutNo 옮기기

	//채팅 메세지를 뽑아서 어떤 금지어 체크(필터링 작업)
	*pMessage >> MsgLen;//메세지 바이트 단위 길이

	if (pMessage->GetData((char*)Message, MsgLen) == 0)
	{
		Disconnect(sessionid);
		return;
	}

	if (pMessage->GetLastError())
	{
		LOG(L"ChatServer", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"ChatMessageRequest::CMessage Flag Error... \ UniqID : %llu  / AccountNo : %lld ", sessionid, pUser->s_AccountNo);

		//프로토콜 보다 보낸 데이터 크기가 적으면 플래그 켜짐.
		Disconnect(sessionid);

		return;
	}

	if (pMessage->GetDataSize() > 0)
	{
		LOG(L"ChatServer", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"ChatMessageRequest::CMessage Size Overflow Error... \ UniqID : %llu  / AccountNo : %lld ", sessionid, pUser->s_AccountNo);
		//프로토콜 보다 보낸 데이터 크기가 크면 끊기
		Disconnect(sessionid);

		return;
	}

	pUser->s_RecvTime = timeGetTime();

	//채팅 보내기 응답 메세지 생성 및 전달
	CMessage* pPacket = CMessage::Alloc();
	pPacket->Clear();

	*pPacket << (WORD)en_PACKET_CS_CHAT_RES_MESSAGE;
	*pPacket << pUser->s_AccountNo;
	pPacket->PutData((char*)pUser->s_ID, ID_MAX * sizeof(WCHAR));
	pPacket->PutData((char*)pUser->s_NickName, NICK_MAX * sizeof(WCHAR));
	*pPacket << MsgLen;
	pPacket->PutData((char*)Message, MsgLen);

	//주위에 뿌리기
	SendPakcet_SectorAround(pPacket, pUser);

	CMessage::Free(pPacket);
}

void ChatServer::HeartBeatProc(UINT64 sessionid)
{
	//유저 자료 구조에서 찾기
	CUser* pUser = nullptr;

	AcquireSRWLockShared(&m_UserMapLock);
	std::unordered_map<UINT64, CUser*>::iterator itOn = m_UserMap.find(sessionid);
	if (itOn == m_UserMap.end())
	{
		std::unordered_map<UINT64, DWORD>::iterator itNon = m_NonUserMap.find(sessionid);
		if (itNon == m_NonUserMap.end())
			__debugbreak();
		ReleaseSRWLockShared(&m_UserMapLock);

		LOG(L"ChatServer", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"HeartBeat::UserMap Not Exist NonUserMap Exist Error... \ UniqID : %lld ", sessionid);
		Disconnect(sessionid);

		return;
	}


	//찾았으면
	pUser = itOn->second;
	ReleaseSRWLockShared(&m_UserMapLock);


	if (pUser->s_Sector != 1)
	{
		LOG(L"ChatServer", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"HeartBeat::Not Recv SectorMove Message ... \ UniqID : %llu  / AccountNo : %lld ", sessionid, pUser->s_AccountNo);

		//섹터 이동 보다 먼저 패킷온 경우
		Disconnect(sessionid);
		return;
	}

	pUser->s_RecvTime = timeGetTime();
}

void ChatServer::FrameThread()
{
	DWORD OldTimeOutTick1; //2초  타임아웃
	DWORD OldTimeOutTick2; //40초 타임아웃
	DWORD OldTimeOutTick;  //루프 이전 시간
	DWORD curTick;
	DWORD curLoopTick;

	LOG(L"ChatServer", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L"FrameThread  Start... : %d ", GetCurrentThreadId());

	OldTimeOutTick1 = timeGetTime();
	OldTimeOutTick2 = OldTimeOutTick1;
	OldTimeOutTick = OldTimeOutTick1;

	wprintf(L"Frame Thread Start : %d...\n", GetCurrentThreadId());


	while (!m_EndFlag)
	{
		curLoopTick = timeGetTime();

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
		
		Sleep(df_FRAME);

	}


	wprintf(L"Update Thread End : %d...\n", GetCurrentThreadId());

	return;
}

void ChatServer::MonitorThread()
{
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
	time_t start;
	tm* local_time;

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

		CPoolSum += CMessage::m_pMessagePool->GetUseCnt();
		AcptTPSSum += m_AcceptTPS;
		LoginResSum += m_ResLoginTPS;
		SectorMoveSum += m_ResMoveTPS;
		ChatMsgSum += m_ResChatMsgTPS;
		SendIOSum += m_SendIOTPS;
		RecvIOSum += m_RecvIOTPS;
		ReqMsgSum += m_ReqTPS;
		ResMsgSum += SumResMsgTPS();

		//TOTO : Start 시간 출력
		wprintf(L"Start Time : %04d / %02d / %02d, %02d:%02d:%02d\n",
			local_time->tm_year + 1900,
			local_time->tm_mon + 1,    
			local_time->tm_mday,
			local_time->tm_hour,
			local_time->tm_min,
			local_time->tm_sec);
		wprintf(L"======================= TPS 모니터링 ================================\n");
		wprintf(L"Accept                                        TPS    : (Avg %lld , %d) \n", AcptTPSSum / loopCnt, m_AcceptTPS);
		wprintf(L"SendIOComplete                                TPS    : (Avg %lld, %d) \n", SendIOSum / loopCnt, m_SendIOTPS);
		wprintf(L"RecvIOComplete                                TPS    : (Avg %lld, %d) \n", RecvIOSum / loopCnt, m_RecvIOTPS);
		wprintf(L"RequestMsg                                    TPS    : (Avg %lld, %d) \n\n", ReqMsgSum / loopCnt, m_ReqTPS);
		wprintf(L"ResponseMsg                                   TPS    : (Avg %lld, %lld) \n\n", ResMsgSum / loopCnt, SumResMsgTPS());


		wprintf(L"====================== 카운트 모니터링 ==============================\n");
		wprintf(L"UserMap / NonUserMap                   Count   : %lld / %lld \n", m_UserMap.size(), m_NonUserMap.size());
		wprintf(L"SessionTable                           Count   : %d \n", m_CurSessionCnt);
		wprintf(L"Accept  Total                          Count   : %lld \n", m_AcceptTotal);


		wprintf(L"====================== 사용량 모니터링 ==============================\n");
		wprintf(L" CMessagePool           Avg  UseCnt : %lld  / Count : %d \n", CPoolSum / loopCnt, CMessage::m_pMessagePool->GetUseCnt());
		wprintf(L"     UserPool                UseCnt : %d \n", m_pUserPool->GetUseCnt());


		wprintf(L"[ CPU Usage : T[%f%] U[%f%] K[%f%]]\n", processtotalsum / loopCnt, processusersum / loopCnt, processkernelsum / loopCnt);
		wprintf(L"[ Available        Memory Usage : %lf MByte ] [ NonPagedMemory Usage : %lf MByte ]\n", m_pPDH->m_AvailableMemoryVal.doubleValue, m_pPDH->m_NonPagedMemoryVal.doubleValue / (1024 * 1024));
		wprintf(L"[ Process User     Memory Usage : %lf MByte ]  [ Process NonPaged Memory Usage : %lf KByte ]\n", m_pPDH->m_processUserMemoryVal.doubleValue / (1024 * 1024), m_pPDH->m_processNonPagedMemoryVal.doubleValue / 1024);
		wprintf(L"[ TCP Retransmitted Avg   Count : %lf /sec  ]  [ TCP Segment Sent  Avg   Count : % lf / sec]\n", tcpretransmitsum / loopCnt, tcpsegmentsentsum / loopCnt);

		// 연결 되었을 때만 모니터링 서버로 데이터 보내기
		//if (m_pMonitorClient->ConnectAlive())
		//{
		//	m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_CHAT_SERVER_RUN, 1);
		//	m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_CHAT_SERVER_CPU, (int)m_pPDH->ProcessTotal());
		//	m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_CHAT_SERVER_MEM, (int)(m_pPDH->m_processUserMemoryVal.doubleValue / (1024 * 1024)));
		//	m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_CHAT_SESSION, m_CurSessionCnt);
		//	m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_CHAT_PLAYER, m_pUserPool->GetUseCnt());
		//	m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_CHAT_PACKET_POOL, CMessage::pMessagePool->GetUseCnt());
		//}
		//else
		//{
		//	if (m_pMonitorClient->ReConnect())
		//	{
		//		m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_CHAT_SERVER_RUN, 1);
		//		m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_CHAT_SERVER_CPU, (int)m_pPDH->ProcessTotal());
		//		m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_CHAT_SERVER_MEM, (int)(m_pPDH->m_processUserMemoryVal.doubleValue / (1024 * 1024)));
		//		m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_CHAT_SESSION, m_CurSessionCnt);
		//		m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_CHAT_PLAYER, m_pUserPool->GetUseCnt());
		//		m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_CHAT_PACKET_POOL, CMessage::pMessagePool->GetUseCnt());
		//	}
		//}


		tcpretranslog = m_pPDH->m_TCPReTransmitVal.doubleValue;
		if (tcpretranslog >= 2000)
		{
			LOG(L"TCP", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L" TCP Retransmitted : %lf ", tcpretranslog);
		}


		m_AcceptTPS = 0;
		m_RecvIOTPS = 0;
		m_SendIOTPS = 0;
		m_ReqTPS = 0;
		m_ResLoginTPS = 0;
		m_ResMoveTPS = 0;
		m_ResChatMsgTPS = 0;

		loopCnt++;
	}


}