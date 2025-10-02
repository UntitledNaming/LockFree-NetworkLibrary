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

	// WORD 형으로 지역변수 선언하면 스택 오염임. 2바이트 지역변수인데 GetValue할때 넘겨주는 변수의 주소는 4바이트 크기로 파서가 인지하고 있음. 그래서 파서가 4바이트를 copy해서 줘버림.
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

	INT UpdateFrame;
	parser.GetValue("UPDATE_FRAME", (int*)&UpdateFrame);

	INT Loglevel;
	parser.GetValue("LOG_LEVEL", (int*)&Loglevel);

#pragma endregion

	CLogClass::GetInstance()->Init(Loglevel);
	CMessage::Init(sizeof(LANHEADER), sizeof(NETHEADER));

	// todo :  mem_inint에서 모니터링 서버 연결까지 하기
	Mem_Init(Usermax, UpdateFrame, const_cast<WCHAR*>(monitorstr.c_str()), monitorport);

	if (!Start((WCHAR*)bindstr.c_str(), port, createthread, runningthread, maxSessions, SendFrame, Sendflag, PACKET_CODE, PACKET_KEY, (bool)Nagle))
		return false;


	return true;
}

void ChatServer::StopServer()
{
	Stop();

	Thread_Destroy();
	
	m_pMonitorClient->Destroy();

	delete m_pMonitorClient;
	delete m_pUserPool;
	delete m_pUpdateJobQ;
	delete m_pPDH;

	CMessage::PoolDestroy();
}

void ChatServer::Mem_Init(INT userMAX, INT updateFrame, WCHAR* monitorserverip, INT monitorserverport)
{
	m_UserMaxCnt = userMAX;
	m_UpdateFrame = updateFrame;

	m_pUserPool = new CMemoryPool<CUser>;
	m_pJobPool = new CMemoryPool<JOB>;
	m_pUpdateJobQ = new LFQueue<JOB*>;
	m_pPDH = new ProcessMonitor;
	m_pMonitorClient = new CMonitorClient(CHAT_SERVER_NO);

	m_UpdateLoopTime = 0;
	m_UpdateTPS = 0;
	m_ReqTPS = 0;
	m_ResTPS = 0;
	m_ReqLoginTPS = 0;
	m_ResLoginTPS = 0;
	m_ReqMoveTPS = 0;
	m_ResMoveTPS = 0;
	m_ReqChatMsgTPS = 0;
	m_ResChatMsgTPS = 0;
	m_MonitorFlag = false;

	if (!m_pMonitorClient->Connect(monitorserverip, monitorserverport))
		__debugbreak();

	Thread_Create();

}

void ChatServer::Thread_Create()
{
	m_Update = std::thread(&ChatServer::UpdateThread, this);
	m_Monitor = std::thread(&ChatServer::MonitorThread, this);
}

void ChatServer::Thread_Destroy()
{
	JOB* job = m_pJobPool->Alloc();
	job->s_Id = NULL;
	job->s_Type = en_End;
	m_pUpdateJobQ->Enqueue(job);

	m_MonitorFlag = true;

	if (m_Update.joinable())
	{
		m_Update.join();
	}

	if (m_Monitor.joinable())
	{
		m_Monitor.join();
	}
}


bool ChatServer::OnConnectionRequest(WCHAR* InputIP, USHORT InputPort)
{

	return true;
}

void ChatServer::OnClientJoin(UINT64 SessionID)
{
	JOB* job = m_pJobPool->Alloc();
	job->s_Id = SessionID;
	job->s_Type = en_Join;

	m_pUpdateJobQ->Enqueue(job);

}

void ChatServer::OnClientLeave(UINT64 SessionID)
{
	JOB* job = m_pJobPool->Alloc();
	job->s_Id = SessionID;
	job->s_Type = en_Delete;

	m_pUpdateJobQ->Enqueue(job);
}

void ChatServer::OnRecv(UINT64 SessionID, CMessage* pMessage)
{
	JOB* job = m_pJobPool->Alloc();
	job->s_Id = SessionID;
	job->s_Type = en_Recv;
	job->s_ptr = pMessage;
	pMessage->AddRef();


	m_pUpdateJobQ->Enqueue(job);
}

void ChatServer::NonUserTimeOut()
{
	DWORD tick;
	std::unordered_map<UINT64, DWORD>::iterator it = m_NonUserMap.begin();
	for (; it != m_NonUserMap.end(); ++it)
	{
		tick = timeGetTime();
		if (tick - it->second >= TIMEOUT1)
		{
			CNetServer::Disconnect(it->first);
			LOG(L"ChatServer", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"UpdateThread  NonUserTimeout / SessionID : %llu / TimeOut : %d ", it->first, tick - it->second);
			continue;
		}
	}
}

void ChatServer::UserTimeOut()
{
	CUser* pUser = nullptr;
	DWORD curTick;
	std::unordered_map<UINT64, CUser*>::iterator it = m_UserMap.begin();
	for (; it != m_UserMap.end(); ++it)
	{
		pUser = it->second;
		curTick = timeGetTime();
		if (curTick - pUser->s_RecvTime >= TIMEOUT2)
		{
			if (pUser->s_TimeOut == 1)
				continue;

			Disconnect(pUser->s_UniqID);
			pUser->s_TimeOut = 1;
			LOG(L"ChatServer", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"UpdateThread  UserTimeout / SessionID : %lld / AccountNo : %lld / TimeOut : %lld ...", it->first, it->second->s_AccountNo, curTick - it->second->s_RecvTime);
			continue;
		}

	}
}

void ChatServer::SendPacket_SectorOne(CMessage* pMessage, WORD xpos, WORD ypos, CUser* pUser)
{
	//섹터에 존재하는 pUser 제외하고 보냄.
	std::list<CUser*>::iterator it = m_Sector[ypos][xpos].begin();

	for (; it != m_Sector[ypos][xpos].end(); ++it)
	{
		if (*it == pUser)
			continue;

		SendPacket((*it)->s_UniqID, pMessage);
		m_ResChatMsgTPS++;
		m_ResTPS++;
	}
}

void ChatServer::SendPakcet_SectorAround(CMessage* pMessage, CUser* pUser)
{
	//pUser 주위로 메세지 뿌리기
	SECTOR_AROUND around;

	//around의 섹터 배열 0번째는 반드시 자기 자신 섹터 좌표임.
	SectorFind(&around, pUser->s_Pos.s_xpos, pUser->s_Pos.s_ypos);

	//내 주위 섹터에게 보내기
	for (int i = 0; i < around.s_Cnt; i++)
	{
		SendPacket_SectorOne(pMessage, around.s_Around[i].s_xpos, around.s_Around[i].s_ypos, nullptr);
	}
}

void ChatServer::SectorFind(SECTOR_AROUND* pAround, WORD xpos, WORD ypos)
{
	INT cnt = 0;

	//본인 자신 섹터
	pAround->s_Around[cnt].s_xpos = xpos;
	pAround->s_Around[cnt].s_ypos = ypos;
	cnt++;

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

BOOL ChatServer::SectorRemove(CUser* pUser)
{
	std::list<CUser*>::iterator it = m_Sector[pUser->s_Pos.s_ypos][pUser->s_Pos.s_xpos].begin();
	for (; it != m_Sector[pUser->s_Pos.s_ypos][pUser->s_Pos.s_xpos].end(); ++it)
	{
		//섹터에서 유저 찾았으면
		if (pUser == *it)
		{
			m_Sector[pUser->s_Pos.s_ypos][pUser->s_Pos.s_xpos].erase(it);
			return true;
		}
	}


	return false;
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
		LOG(L"ChatServer", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"LoginRequest::UserMax \ UniqID : %llu ", sessionid);
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

	//NonUser 구조에서 찾기
	itNon = m_NonUserMap.find(sessionid);
	if (itNon == m_NonUserMap.end())
		__debugbreak();

	//이제 진짜 유저니 NonUser에서 제거해야 함.
	m_NonUserMap.erase(itNon);

	//유저객체 초기화
	pUser = m_pUserPool->Alloc();
	pUser->User_Init(sessionid, accountno, ID, NICK);

	m_UserMap.insert(std::pair<UINT64, CUser*>(sessionid, pUser));

	//채팅서버 로그인 응답 패킷 생성 및 전송
	CMessage* pPacket = CMessage::Alloc();
	pPacket->Clear();

	*pPacket << (WORD)en_PACKET_CS_CHAT_RES_LOGIN;
	*pPacket << Status;
	*pPacket << pUser->s_AccountNo;

	SendPacket(sessionid, pPacket);

	m_ResLoginTPS++;
	m_ResTPS++;

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
	itOn = m_UserMap.find(sessionid);
	if (itOn == m_UserMap.end())
	{
		std::unordered_map<UINT64, DWORD>::iterator itNon = m_NonUserMap.find(sessionid);
		if (itNon == m_NonUserMap.end())
			__debugbreak();

		// LoginProc 처리 전에 메세지 먼저 온 경우는 상대방 끊기
		LOG(L"ChatServer", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"SectorMoveRequest::UserMap Not Exist NonUserMap Exist Error... \ UniqID : %llu ", sessionid);
		CNetServer::Disconnect(sessionid);
		return;
	}
	pUser = itOn->second;

	//찾았으면
	*pMessage >> AcntNo;
	*pMessage >> pos.s_xpos;
	*pMessage >> pos.s_ypos;

	if (pMessage->GetLastError())
	{
		LOG(L"ChatServer", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"SectorMoveRequest::CMessage Flag Error... \ UniqID : %llu  / AccountNo : %llu ", sessionid, pUser->s_AccountNo);
		//프로토콜 보다 보낸 데이터 크기가 적으면 플래그 켜짐.
		Disconnect(sessionid);
		return;
	}
	if (pMessage->GetDataSize() > 0)
	{
		LOG(L"ChatServer", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"SectorMoveRequest::CMessage Size Overflow Error... \ UniqID : %llu  / AccountNo : %llu ", sessionid, pUser->s_AccountNo);
		//프로토콜 보다 보낸 데이터 크기가 크면 끊기
		Disconnect(sessionid);
		return;
	}
	if (!SectorRangeCheck(pos.s_xpos, pos.s_ypos))
	{
		LOG(L"ChatServer", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"SectorMoveRequest::Sector Arrang Error... \ UniqID : %llu  / AccountNo : %llu ", sessionid, pUser->s_AccountNo);
		Disconnect(sessionid);
		return;
	}

	//섹터 이동 메세지 처음 온 경우 그냥 변경 섹터에만 넣어주면 됨. 아니면 기존 섹터에서 삭제하고 변경 해야 함.
	if (pUser->s_Sector == 0)
	{
		m_Sector[pos.s_ypos][pos.s_xpos].push_back(pUser);
	}
	else
	{
		//기존 섹터에서 제거
		if (!SectorRemove(pUser))
			__debugbreak();

		//새로운 섹터에 넣기
		m_Sector[pos.s_ypos][pos.s_xpos].push_back(pUser);
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
	m_ResTPS++;

	CMessage::Free(pPacket);

}

void ChatServer::ChatMessageProc(CMessage* pMessage, UINT64 sessionid)
{
	std::unordered_map<UINT64, CUser*>::iterator itOn;
	CUser* pUser;
	WORD   MsgLen;
	WCHAR  Message[MESSAGE_LEN_MAX];

	//유저 자료 구조에서 찾기
	itOn = m_UserMap.find(sessionid);
	if (itOn == m_UserMap.end())
	{
		std::unordered_map<UINT64, DWORD>::iterator itNon = m_NonUserMap.find(sessionid);
		if (itNon == m_NonUserMap.end())
			__debugbreak();

		// LoginProc 처리 전에 메세지 먼저 온 경우는 상대방 끊기
		LOG(L"ChatServer", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"ChatMessageRequest::UserMap Not Exist NonUserMap Exist Error... \ UniqID : %llu ", sessionid);
		Disconnect(sessionid);

		return;
	}


	//찾았으면
	pUser = itOn->second;


	// 섹터 변경 메세지 보다 먼저 온 경우
	if (pUser->s_Sector != 1)
	{
		LOG(L"ChatServer", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"ChatMessageRequest::Not Recv SectorMove Message ... \ UniqID : %llu  / AccountNo : %llu ", sessionid, pUser->s_AccountNo);
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
		LOG(L"ChatServer", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"ChatMessageRequest::CMessage Flag Error... \ UniqID : %llu  / AccountNo : %llu ", sessionid, pUser->s_AccountNo);

		//프로토콜 보다 보낸 데이터 크기가 적으면 플래그 켜짐.
		CNetServer::Disconnect(sessionid);

		return;
	}

	if (pMessage->GetDataSize() > 0)
	{
		LOG(L"ChatServer", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"ChatMessageRequest::CMessage Size Overflow Error... \ UniqID : %llu  / AccountNo : %llu ", sessionid, pUser->s_AccountNo);
		//프로토콜 보다 보낸 데이터 크기가 크면 끊기
		CNetServer::Disconnect(sessionid);

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

void ChatServer::HeartBeatProc(CMessage* pMessage, UINT64 sessionid)
{

	if (pMessage->GetDataSize() > 0)
	{
		LOG(L"ChatServer", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"HeartBeat::CMessage Size Overflow Error... \ UniqID : %llu  ", sessionid);
		//프로토콜 보다 보낸 데이터 크기가 크면 끊기
		CNetServer::Disconnect(sessionid);

		return;
	}

	//유저 자료 구조에서 찾기
	std::unordered_map<UINT64, CUser*>::iterator itOn = m_UserMap.find(sessionid);
	if (itOn == m_UserMap.end())
	{
		std::unordered_map<UINT64, DWORD>::iterator itNon = m_NonUserMap.find(sessionid);
		if (itNon == m_NonUserMap.end())
			__debugbreak();

		LOG(L"ChatServer", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"HeartBeat::UserMap Not Exist NonUserMap Exist Error... \ UniqID : %llu ", sessionid);

		CNetServer::Disconnect(sessionid);

		return;
	}


	//찾았으면
	CUser* pUser = itOn->second;
	if (pUser->s_Sector != 1)
	{
		LOG(L"ChatServer", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"HeartBeat::Not Recv SectorMove Message ... \ UniqID : %llu  / AccountNo : %llu ", sessionid, pUser->s_AccountNo);

		//섹터 이동 보다 먼저 패킷온 경우
		CNetServer::Disconnect(sessionid);
		return;
	}

	pUser->s_RecvTime = timeGetTime();
	m_ResTPS++;
}

void ChatServer::JoinProc(UINT64 sessionID)
{
	//유저 껍데기 자료 구조에 현재 시간 측정해서 insert
	m_NonUserMap.insert(std::pair<UINT64, DWORD>(sessionID, timeGetTime()));
}

void ChatServer::RecvProc(UINT64 sessionID, CMessage* pMessage)
{
	WORD type;
	*pMessage >> type;

	if (pMessage->GetLastError())
	{
		Disconnect(sessionID);
		LOG(L"ChatServer", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"UserRecvMsg::CMessage Flag Error...  \ UniqID : %llu ", sessionID);
		CMessage::Free(pMessage);
		return;
	}

	m_ReqTPS++;

	switch (type)
	{
	case en_PACKET_CS_CHAT_REQ_LOGIN:
		m_ReqLoginTPS++;
		LoginProc(pMessage, sessionID);
		break;

	case en_PACKET_CS_CHAT_REQ_SECTOR_MOVE:
		m_ReqMoveTPS++;
		SectorMoveProc(pMessage, sessionID);
		break;

	case en_PACKET_CS_CHAT_REQ_MESSAGE:
		m_ReqChatMsgTPS++;
		ChatMessageProc(pMessage, sessionID);
		break;

	case en_PACKET_CS_CHAT_REQ_HEARTBEAT:
		HeartBeatProc(pMessage,sessionID);
		break;

	default:
		Disconnect(sessionID);
		break;

	}

	CMessage::Free(pMessage);
	return;
}

void ChatServer::DeleteProc(UINT64 sessionID)
{
	//해당 ID가 NonUser인지 User인지 모름.
	std::unordered_map<UINT64, DWORD>::iterator itNon;
	std::unordered_map<UINT64, CUser*>::iterator itOn;

	//Non 유저 자료 구조에서 먼저 찾기
	itNon = m_NonUserMap.find(sessionID);
	if (itNon != m_NonUserMap.end())
	{
		//찾았으면 제거하고 끝
		m_NonUserMap.erase(itNon);

		return;
	}


	// 논유저에 없으면 유저에서 찾기
	itOn = m_UserMap.find(sessionID);
	if (itOn == m_UserMap.end())
		__debugbreak();

	CUser* pUser = itOn->second;

	//유저 자료구조에서 제거
	m_UserMap.erase(itOn);


	//섹터에서 제거할때 SectorMove 메세지 오기전에 제거가 되는 경우 SectorRemove에서 에러남. 그래서 이때는 SectorRemove 호출 안함.
	if (pUser->s_Sector == 0)
	{
		m_pUserPool->Free(pUser);
		return;
	}

	if (!SectorRemove(pUser))
		__debugbreak();

	m_pUserPool->Free(pUser);
	return;
}

void ChatServer::UpdateThread()
{
	DWORD OldTimeOutTick1; //2초  타임아웃
	DWORD OldTimeOutTick2; //40초 타임아웃
	DWORD OldTimeOutTick;  //루프 이전 시간
	DWORD curTick;
	DWORD curLoopTick;
	JOB*  job;
	BOOL  esc = false;

	LOG(L"ChatServer", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L"UpdateThread  Start... : %d ", GetCurrentThreadId());

	OldTimeOutTick1 = timeGetTime();
	OldTimeOutTick2 = OldTimeOutTick1;
	OldTimeOutTick = OldTimeOutTick1;

	wprintf(L"Update Thread Start : %d...\n", GetCurrentThreadId());


	while (!esc)
	{
		curLoopTick = timeGetTime();

		curTick = timeGetTime();

		//if (curTick - OldTimeOutTick1 >= TIMEOUT1)
		//{
		//	NonUserTimeOut(curTick);

		//	OldTimeOutTick1 += TIMEOUT1;
		//}

		//if (curTick - OldTimeOutTick2 >= TIMEOUT2)
		//{
		//	UserTimeOut();

		//	OldTimeOutTick2 += TIMEOUT2;
		//}


		while (m_pUpdateJobQ->GetUseSize() > 0)
		{
			//Deq하는 스레드가 1개라서 size가 0보다 크면 Enq 스레드에서 큐에 노드를 넣었는데 없을 수 없음.
			if (!m_pUpdateJobQ->Dequeue(job))
				__debugbreak();


			switch (job->s_Type)
			{
			case en_Join:
				JoinProc(job->s_Id);
				break;

			case en_Recv:
				RecvProc(job->s_Id, job->s_ptr);
				break;

			case en_Delete:
				DeleteProc(job->s_Id);
				break;

			case en_End:
				esc = true;
				break;

			}

			//Job 풀에 반환
			m_pJobPool->Free(job);
			m_UpdateTPS++;
		}


		OldTimeOutTick = timeGetTime();

		m_UpdateLoopTime = OldTimeOutTick - curLoopTick;
		if (m_UpdateLoopTime >= 200)
		{
			LOG(L"ChatServer", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L"UpdateThread 1Frame Proc Time : %d ", m_UpdateLoopTime);
		}

		
		Sleep(m_UpdateFrame);

	}

	LOG(L"ChatServer", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L"UpdateThread  End... : %d ", GetCurrentThreadId());


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

	while (!m_MonitorFlag)
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

		MsgQSizeSum += m_pUpdateJobQ->GetUseSize();
		CPoolSum += CMessage::m_pMessagePool->GetUseCnt();
		AcptTPSSum += m_AcceptTPS;
		LoginResSum += m_ResLoginTPS;
		SectorMoveSum += m_ResMoveTPS;
		ChatMsgSum += m_ResChatMsgTPS;
		UpdateSum += m_UpdateTPS;
		SendIOSum += m_SendIOTPS;
		RecvIOSum += m_RecvIOTPS;
		ReqMsgSum += m_ReqTPS;
		ResMsgSum += m_ResTPS;

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
		wprintf(L"Update                                        TPS    : (Avg %lld, %d) \n", UpdateSum / loopCnt, m_UpdateTPS);
		wprintf(L"SendIOComplete                                TPS    : (Avg %lld, %d) \n", SendIOSum / loopCnt, m_SendIOTPS);
		wprintf(L"RecvIOComplete                                TPS    : (Avg %lld, %d) \n", RecvIOSum / loopCnt, m_RecvIOTPS);
		wprintf(L"RequestMsg                                    TPS    : (Avg %lld, %d) \n\n", ReqMsgSum / loopCnt, m_ReqTPS);
		wprintf(L"ResponseMsg                                   TPS    : (Avg %lld, %lld) \n\n", ResMsgSum / loopCnt, m_ResTPS);


		wprintf(L"====================== 카운트 모니터링 ==============================\n");
		wprintf(L"UserMap / NonUserMap                   Count   : %lld / %lld \n", m_UserMap.size(), m_NonUserMap.size());
		wprintf(L"SessionTable                           Count   : %d \n", m_CurSessionCnt);
		wprintf(L"Accept  Total                          Count   : %lld \n", m_AcceptTotal);


		wprintf(L"=====================================================================\n");
		wprintf(L"Update          Loop Time                       Time    : %d ms \n", m_UpdateLoopTime);

		wprintf(L"====================== 사용량 모니터링 ==============================\n");
		wprintf(L"   UpdateJobQ           Avg    Size : %lld  / Size  : %d \n", MsgQSizeSum / loopCnt, m_pUpdateJobQ->GetUseSize());
		wprintf(L" CMessagePool           Avg  UseCnt : %lld  / Count : %d \n", CPoolSum / loopCnt, CMessage::m_pMessagePool->GetUseCnt());
		wprintf(L"     UserPool                UseCnt : %d \n", m_pUserPool->GetUseCnt());


		wprintf(L"[ CPU Usage : T[%f%] U[%f%] K[%f%]]\n", processtotalsum / loopCnt, processusersum / loopCnt, processkernelsum / loopCnt);
		wprintf(L"[ Available        Memory Usage : %lf MByte ] [ NonPagedMemory Usage : %lf MByte ]\n", m_pPDH->m_AvailableMemoryVal.doubleValue / (1024 * 1024), m_pPDH->m_NonPagedMemoryVal.doubleValue / (1024 * 1024));
		wprintf(L"[ Process User     Memory Usage : %lf MByte ]  [ Process NonPaged Memory Usage : %lf KByte ]\n", m_pPDH->m_processUserMemoryVal.doubleValue / (1024 * 1024), m_pPDH->m_processNonPagedMemoryVal.doubleValue / 1024);
		wprintf(L"[ TCP Retransmitted Avg   Count : %lf /sec  ]  [ TCP Segment Sent  Avg   Count : % lf / sec]\n", tcpretransmitsum / loopCnt, tcpsegmentsentsum / loopCnt);

		// 연결 되었을 때만 모니터링 서버로 데이터 보내기
		if (m_pMonitorClient->ConnectAlive())
		{
			m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_CHAT_SERVER_RUN, 1);
			m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_CHAT_SERVER_CPU, (int)m_pPDH->ProcessTotal());
			m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_CHAT_SERVER_MEM, (int)(m_pPDH->m_processUserMemoryVal.doubleValue / (1024 * 1024)));
			m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_CHAT_SESSION, m_CurSessionCnt);
			m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_CHAT_PLAYER, m_pUserPool->GetUseCnt());
			m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_CHAT_UPDATE_TPS, m_UpdateTPS);
			m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_CHAT_PACKET_POOL, CMessage::m_pMessagePool->GetUseCnt());
			m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_CHAT_UPDATEMSG_POOL, m_pJobPool->GetUseCnt());
		}
		else
		{
			if (m_pMonitorClient->ReConnect())
			{
				m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_CHAT_SERVER_RUN, 1);
				m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_CHAT_SERVER_CPU, (int)m_pPDH->ProcessTotal());
				m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_CHAT_SERVER_MEM, (int)(m_pPDH->m_processUserMemoryVal.doubleValue / (1024 * 1024)));
				m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_CHAT_SESSION, m_CurSessionCnt);
				m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_CHAT_PLAYER, m_pUserPool->GetUseCnt());
				m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_CHAT_UPDATE_TPS, m_UpdateTPS);
				m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_CHAT_PACKET_POOL, CMessage::m_pMessagePool->GetUseCnt());
				m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_CHAT_UPDATEMSG_POOL, m_pJobPool->GetUseCnt());
			}
		}


		tcpretranslog = m_pPDH->m_TCPReTransmitVal.doubleValue;
		if (tcpretranslog >= 2000)
		{
			LOG(L"TCP", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L" TCP Retransmitted : %lf ", tcpretranslog);
		}


		m_AcceptTPS = 0;
		m_RecvIOTPS = 0;
		m_SendIOTPS = 0;
		m_UpdateTPS = 0;
		m_ReqTPS = 0;
		m_ResTPS = 0;
		m_ResLoginTPS = 0;
		m_ResMoveTPS = 0;
		m_ResChatMsgTPS = 0;

		loopCnt++;
	}


}
