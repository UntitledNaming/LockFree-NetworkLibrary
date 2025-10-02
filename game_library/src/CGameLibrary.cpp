#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <timeapi.h>
#include <ws2tcpip.h>
#include <string>
#include <unordered_map>
#include <thread>
#include <array>
#include <vector>
#include <codecvt>


#include "LibraryHeader.h"
#include "TextParser.h"
#include "LogClass.h"

#include "LFQSingleLive.h"
#include "IUser.h"

#include "MemoryPoolTLS.h"
#include "CMessage.h"
#include "LFStack.h"

#include "LFQMultiLive.h"
#include "Ring_Buffer.h"
#include "CSession.h"
#include "CGameLibrary.h"
#include "CGroup.h"



CGameLibrary::CGameLibrary()
{
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
	{
		wprintf(L"WSAStartUp Failed....! \n");
		LOG(L"GameLibrary", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"CGameLibrary::Net_Init WSAStartup Error : %d ", WSAGetLastError());
		__debugbreak();
	}
}

CGameLibrary::~CGameLibrary()
{
}

bool CGameLibrary::Run()
{
	// 파싱 작업
	Parser parser;

	if (!parser.LoadFile("GameLibraryConfig.txt"))
		return false;

	Parser::st_Msg msg;
	parser.GetValue("BIND_IP", &msg);

	//char형 문자열을 wchar로 변환
	wstring wstr;
	std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
	wstr = converter.from_bytes(msg.s_ptr);

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

	INT Nagle;
	parser.GetValue("NAGLE", (int*)&Nagle);

	INT SendFrame;
	parser.GetValue("SEND_FRAME", (int*)&SendFrame);

	INT Loglevel;
	parser.GetValue("LOG_LEVEL", (int*)&Loglevel);

	INT SendFlag;
	parser.GetValue("SEND_TH_FLAG", (int*)&SendFlag);

	CMessage::Init(sizeof(LANHEADER), sizeof(NETHEADER));
	CLogClass::GetInstance()->Init(Loglevel);

	// 멤버 변수 초기화(스레드 생성 포함)
	Mem_Init(maxSessions, createthread, runningthread, PACKET_CODE, PACKET_KEY, SendFrame, SendFlag, Nagle);

	// 네트워크 초기화 및 연결
	Net_Init((WCHAR*)wstr.c_str(), port);

	return true;
}

void CGameLibrary::Stop()
{
	// 네트워크 닫기
	closesocket(m_Listen);

	// 전체 세션 Release 작업
	while (1)
	{
		if (m_CurSessionCnt == 0)
			break;

		for (int i = 0; i < m_MaxSessionCnt; i++)
		{
			if (m_SessionTable[i].m_SessionID == df_INVALID_SESSIONID)
				continue;

			Disconnect(m_SessionTable[i].m_SessionID);
		}

	}

	CloseHandle(m_IOCP);

	m_Endflag = true;


	//Accept 스레드 종료 확인
	if (m_AcceptThread.joinable())
	{
		m_AcceptThread.join();
	}

	//생성된 워커 스레드 종료 확인
	for (int i = 0; i < m_CreateWorkerCnt; i++)
	{
		if (m_IOCPWorkerThread[i].joinable())
		{
			m_IOCPWorkerThread[i].join();
		}
	}


	// send 플래그 켜져 있으면 
	if (m_SendThFL == 1)
	{

		if (m_SendThread.joinable())
		{
			m_SendThread.join();
		}
	}

	if (m_FrameThread.joinable())
	{
		m_FrameThread.join();
	}


	delete[] m_SessionTable;
	delete   m_pSessionIdxStack;
	m_SessionTable = nullptr;
	m_pSessionIdxStack = nullptr;
	

}

bool CGameLibrary::Attach(CGroup* pContents, wstring contentsType, UINT64 groupframe, BOOL shared)
{
	m_GroupArray.push_back(pContents);
	pContents->SetGroupFrame(groupframe);
	pContents->SetGroupID(m_GroupID);
	pContents->SetGameLib(this);
	pContents->SetSharedFlag(shared);
	m_GroupMap.insert(std::pair<std::wstring, UINT16>(contentsType, m_GroupID));
	m_GroupID++;

	return true;
}

void CGameLibrary::Mem_Init(INT sessionmax, INT createiothread, INT activethread, WORD packet_code, WORD packet_key, INT sendframe, INT sendflag, INT nagle)
{
	// Config 정보 초기화
	m_MaxSessionCnt = sessionmax;
	m_CreateWorkerCnt = createiothread;
	m_ConcurrentCnt = activethread;
	m_PacketCode = packet_code;
	m_FixedKey = packet_key;
	m_SendFrame = sendframe;
	m_SendThFL = sendflag;
	m_Nagle = nagle;
	m_Endflag = false;

	m_GroupArray.reserve(df_GAMELIB_DEFAULT_GROUP_COUNT);

	// IOCP 객체 생성 및 초기화
	m_IOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, m_ConcurrentCnt); // 러닝 스레드 갯수로 컨커런트 스레드 설정
	if (m_IOCP == NULL)
	{
		wprintf(L"CreateIoCompletionPort Failed...\n");
		LOG(L"GameLibrary", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"CreateIoCompletionPort Failed...");

		return;
	}
	wprintf(L"Create IOCP Resoure Success! \n");
	LOG(L"GameLibrary", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L"CreateIoCompletionPort Complete...");


	// 세션 관련 초기화 작업
	Session_Init();

	// 그룹 관련 초기화 작업
	Group_Init();

	// 모니터링 변수 초기화
	Monitoring_Init();

	// 게임 라이브러리 스레드 생성
	Thread_Create(m_SendThFL);

}

void CGameLibrary::Net_Init(WCHAR* serverIp, INT serverport)
{
	int ret;

	// 리슨 소켓 생성
	m_Listen = socket(AF_INET, SOCK_STREAM, 0);
	if (m_Listen == INVALID_SOCKET)
	{
		wprintf(L"socket() error \n");
		LOG(L"GameLibrary", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"CGameLibrary::Net_Init socket() Error : %d ", WSAGetLastError());
		__debugbreak();
	}
	wprintf(L"socket() Complete... \n");

	//bind() 처리
	SOCKADDR_IN serveraddr;
	ZeroMemory(&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port = htons(serverport);
	InetPtonW(AF_INET, serverIp, &serveraddr.sin_addr);

	ret = bind(m_Listen, (SOCKADDR*)&serveraddr, sizeof(serveraddr));
	if (ret == SOCKET_ERROR)
	{
		LOG(L"GameLibrary", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"CGameLibrary::Net_Init bind() Error... \ Error Code :", WSAGetLastError());
		__debugbreak();
	}


	ret = listen(m_Listen, SOMAXCONN_HINT(5000));
	if (ret == SOCKET_ERROR)
	{
		LOG(L"GameLibrary", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"CGameLibrary::Net_Init listen() Error... \ Error Code :", WSAGetLastError());
		__debugbreak();
	}

	wprintf(L"listen() Complete... \n");
	LOG(L"GameLibrary", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L"CGameLibrary::Net_Init listen() Complete... ");

}

void CGameLibrary::Session_Init()
{
	m_SessionTable = new CSession[m_MaxSessionCnt];
	m_pSessionIdxStack = new LFStack<UINT16>;
	m_AllocID = 0;

	for (int i = 0; i < m_MaxSessionCnt; i++)
	{
		m_pSessionIdxStack->Push(i);
	}

}

void CGameLibrary::Group_Init()
{
	m_GroupID = 0;
}

void CGameLibrary::Monitoring_Init()
{
	m_AcceptTPS = 0;
	m_RecvIOTPS = 0;
	m_SendIOTPS = 0;
	m_AcceptTotal = 0;
	m_CurSessionCnt = 0;
}

void CGameLibrary::Thread_Create(INT sendflag)
{
	for (int i = 0; i < m_CreateWorkerCnt; i++)
	{
		m_IOCPWorkerThread[i] = std::thread(&CGameLibrary::WorkerThread, this);
	}

	m_AcceptThread = std::thread(&CGameLibrary::AcceptThread, this);

	if (m_SendThFL == 1)
	{
		m_SendThread = std::thread(&CGameLibrary::SendThread, this);
	}

	m_FrameThread = std::thread(&CGameLibrary::FrameThread, this);

}

void CGameLibrary::FrameThread()
{
	DWORD curTime;
	DWORD oldTime;
	INT   cnt;
	UINT64 frametime;

	LOG(L"GameLibrary", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L"FrameThread Start : %d ", GetCurrentThreadId());
	while (!m_Endflag)
	{

		for (int i = 0; i < m_GroupID; i++)
		{
			oldTime = m_GroupArray[i]->GetOldTime();
			curTime = timeGetTime();
			frametime = m_GroupArray[i]->GetGroupFrame();
			cnt = (curTime - oldTime) / frametime;

			for (int j = 0; j < cnt; j++)
			{
				PostQueuedCompletionStatus(m_IOCP, NULL, (ULONG_PTR)i, (LPOVERLAPPED)en_FRAME);  // 그룹 ID를 KEY, lpOverlapped를 Task Type으로 사용
				m_GroupArray[i]->SetOldTime();
			}

		}

	}

	LOG(L"GameLibrary", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L"FrameThread  End... : %d ", GetCurrentThreadId());
	return;
}

void CGameLibrary::WorkerThread()
{
	wprintf(L"WorkerThread Start.. %d \n", GetCurrentThreadId());
	LOG(L"GameLibrary", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L"WorkerThread  Start... : %d ", GetCurrentThreadId());

	BOOL  retval;
	DWORD err;
	BOOL  ESC = false;

	while (!ESC)
	{
		DWORD       cbTransferred = -1;
		OVERLAPPED* pOverlapped = nullptr;     // IO에 사용된 Overlapped 구조체 주소값 or Task Type
		CSession*   pSession = nullptr;        // Key값을 저장할 지역 변수. PQCS에 따라 Key값이 세션 주소값이 아니라 버퍼 주소값이 될 수 있음.

		retval = GetQueuedCompletionStatus(m_IOCP, &cbTransferred, (PULONG_PTR)&pSession, (LPOVERLAPPED*)&pOverlapped, INFINITE);

		//false 인 상황(IOCP 완료 통지 큐에서 디큐잉이 안된 경우, IOCP 핸들이 CloseHandle로 인해 닫힌 경우)
		if (retval == false)
		{
			//IOCP가 닫히거나 IOCP 완료 통지 큐에서 디큐잉에 실패할 때 처리
			if (pOverlapped == nullptr)
			{
				err = GetLastError();
				LOG(L"GameLibrary", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"WorkerThread GQCS IOCP Failed \ Error Code : %d", err);

				ESC = true;
				break;
			}

			//WSASend, WSARecv로 요청한 IO가 실패한 경우(TCP가 끊긴 것임)
			else
			{
				err = GetLastError();
				LOG(L"GameLibrary", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"WorkerThread GQCS IO Failed \ Error Code : %d", err);
			}

		}

		if (pOverlapped == (LPOVERLAPPED)en_RELEASE)
		{
			ReleaseProc(pSession);
		}

		else if (pOverlapped == (LPOVERLAPPED)en_FRAME)
		{
			FrameProc((UINT16)pSession);
		}

		else if (pOverlapped == (LPOVERLAPPED)en_GROUPMOVE)
		{
			GroupMoveProc((CMessage*)pSession);
		}

		else if (pOverlapped == &pSession->m_RecvOverlapped)
		{
			RecvIOProc(pSession, cbTransferred);
		}

		else if (pOverlapped == &pSession->m_SendOverlapped)
		{
			SendIOProc(pSession, cbTransferred);
		}

	}


	LOG(L"GameLibrary", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L"WorkerThread  End... : %d ", GetCurrentThreadId());
	wprintf(L"WorkerThread End... %d \n", GetCurrentThreadId());
	return;
}

void CGameLibrary::SendThread()
{
	LOG(L"GameLibrary", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L"SendThread Start : %d ", GetCurrentThreadId());

	DWORD endtime;
	DWORD starttime;

	while (!m_Endflag)
	{
		starttime = timeGetTime();
		for (int i = 0; i < m_MaxSessionCnt; i++)
		{
			if (m_SessionTable[i].m_SessionID == df_INVALID_SESSIONID)
				continue;

			if (!SessionInvalid(&m_SessionTable[i], m_SessionTable[i].m_SessionID))
				continue;

			if (m_SessionTable[i].m_SendFlag != 0 || m_SessionTable[i].m_SendQ.GetUseSize() == 0)
			{
				Release(&m_SessionTable[i], m_SessionTable[i].m_SessionID, InterlockedDecrement64(&m_SessionTable[i].m_RefCnt));
				continue;
			}

			SendPost(&m_SessionTable[i]);

			Release(&m_SessionTable[i], m_SessionTable[i].m_SessionID, InterlockedDecrement64(&m_SessionTable[i].m_RefCnt));
		}

		endtime = timeGetTime();

		if (endtime - starttime >= 300)
		{
			LOG(L"GameLibrary", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L"SendThread 1Frame Send Proc Time : %d ", endtime - starttime);
		}

		// 전체 세션에 대해서 Send 작업 했으며 Sleep으로 버퍼링
		Sleep(m_SendFrame);
	}

	LOG(L"GameLibrary", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L"SendThread End : %d ", GetCurrentThreadId());
}

void CGameLibrary::AcceptThread()
{
	DWORD curThreadID = GetCurrentThreadId();
	LOG(L"GameLibrary", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L"AcceptThread Start : %d ", GetCurrentThreadId());
	bool ESC = false;

	while (!ESC)
	{
		SOCKADDR_IN clientAddr;
		SOCKET client_socket;
		DWORD bytesReceived = 0;
		DWORD flags = 0;
		int  addlen = sizeof(clientAddr);

		client_socket = accept(m_Listen, (SOCKADDR*)&clientAddr, &addlen);
		if (client_socket == INVALID_SOCKET)
		{
			LOG(L"GameLibrary", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L"AcceptThread accept() Failed \ Error Code : %d", WSAGetLastError());
			ESC = true;
			continue;
		}


		InterlockedIncrement(&m_AcceptTPS);


		//서버 가동시 설정한 최대 세션값 이상으로 연결이 들어오면 끊음.
		if (InterlockedIncrement16(&m_CurSessionCnt) > m_MaxSessionCnt)
		{
			closesocket(client_socket);
			InterlockedDecrement16(&m_CurSessionCnt);
			continue;
		}

		WCHAR szClientIP[df_GAMELIB_IP_LEN] = { 0 };
		InetNtopW(AF_INET, &clientAddr.sin_addr, szClientIP, df_GAMELIB_IP_LEN);


		//Overlapped IO로 작동시키기 위해 소켓 송신 버퍼 0으로 설정
		int sendBufferSize = 0;
		setsockopt(client_socket, SOL_SOCKET, SO_SNDBUF, (char*)&sendBufferSize, sizeof(sendBufferSize));

		//네이글 On/Off
		if (m_Nagle == true)
		{
			int flag = 1;
			if (setsockopt(m_Listen, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(int)) == -1)
			{
				LOG(L"GameLibrary", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"CGameLibrary::Start()_Nagle Error :%d ", WSAGetLastError());
				__debugbreak();
			}
			LOG(L"GameLibrary", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L"CGameLibrary::Start()_Nagle On Complete...");
		}

		linger so_linger;
		so_linger.l_onoff = 1;  // linger 옵션 사용
		so_linger.l_linger = 0; // 지연 시간 0 -> 즉시 RST

		if (setsockopt(client_socket, SOL_SOCKET, SO_LINGER, (char*)&so_linger, sizeof(so_linger)) == SOCKET_ERROR)
		{
			LOG(L"GameLibrary", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"GameLibrary::Linger Option Set Error :%d ", WSAGetLastError());
		}

		//세션 배열에 넣기 전에 일단 Index 찾기
		UINT16 Index;
		m_pSessionIdxStack->Pop(Index);

		// 세션 ID 만들기
		UINT64 sessionID = MakeSessionID(Index, m_AllocID);

		// 세션 초기화(그룹 ID 0번으로 초기화)
		m_SessionTable[Index].Init(client_socket, sessionID);


		m_AllocID++;
		m_AcceptTotal++;


		//클라이언트 소켓 IOCP에 등록 및 key값으로 세션 포인터를 설정
		HANDLE retCIOCP;
		retCIOCP = CreateIoCompletionPort((HANDLE)client_socket, m_IOCP, (ULONG_PTR)&m_SessionTable[Index], 0);
		if (retCIOCP == NULL)
		{
			LOG(L"GameLibrary", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L"AcceptThread RegisterSocketHANDLE Failed \ Session ID : %lld \ Error Code : %d ", m_SessionTable[Index].m_SessionID, GetLastError());
			break;
		}


		// 세션이 속한 그룹의 OnClientJoin 호출
		m_GroupArray[m_SessionTable[Index].m_GroupID]->ExclusiveGroupLock();
		m_GroupArray[m_SessionTable[Index].m_GroupID]->OnClientJoin(sessionID);
		m_GroupArray[m_SessionTable[Index].m_GroupID]->ExclusiveGroupUnlock();

		//Recv 등록
		RecvPost(&m_SessionTable[Index]);

		Release(&m_SessionTable[Index], m_SessionTable[Index].m_SessionID, InterlockedDecrement64(&m_SessionTable[Index].m_RefCnt));
	}


	LOG(L"GameLibrary", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L"AcceptThread End : %d ", GetCurrentThreadId());
}

bool CGameLibrary::SendPacket(UINT64 SessionID, CMessage* pMessage)
{
	int retrel;
	CSession* pSession;
	DWORD curThreadID = GetCurrentThreadId();

	FindSession(SessionID, &pSession);

	//못찾은 것임
	if (pSession == nullptr)
		return false;

	// 세션 찾았으면 유효성 확인해야 함.
	if (!SessionInvalid(pSession, SessionID))
		return false;

	if (pSession->m_SendQ.GetUseSize() >= df_SENDQ_MAX_SIZE)
	{
		Disconnect(pSession->m_SessionID);
		LOG(L"CGameLibrary", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"SendPacket SendQ Full \ SessionID : %lld , Time : %d ", pSession->m_SessionID, timeGetTime());
		Release(pSession, pSession->m_SessionID, InterlockedDecrement64(&pSession->m_RefCnt));
		return false;
	}


	if (pMessage->GetEncodingFlag() == 0)
	{
		//컨텐츠 메세지 길이 구해서 네트워크 헤더 만들기
		st_NETHEADER header;
		CHAR* ptemp = pMessage->GetReadPos();
		UCHAR        sum = 0;
		header.s_len = pMessage->GetDataSize(); // 직렬화 버퍼에 담긴 컨텐츠 메세지 크기를 len으로 설정
		header.s_code = m_PacketCode;
		header.s_randkey = rand() % 255;

		//체크섬 계산
		for (int i = 0; i < header.s_len; ++i)
		{
			sum += (UCHAR)*ptemp;
			ptemp++;
		}
		header.s_checksum = sum % 256;

		//직렬화 버퍼 앞단에 네트워크 헤더 넣기
		memcpy_s(pMessage->GetAllocPos(), sizeof(header), (char*)&header, sizeof(header));

		//인코딩 작업(checkSum 위치의 주소값을 전달함.
		Encoding(pMessage->GetReadPos() - sizeof(header.s_checksum), header.s_len + sizeof(header.s_checksum), header.s_randkey);

		pMessage->SetEncodingFlag(1);
	}

	
	pMessage->AddRef();
	pSession->m_SendQ.Enqueue(pMessage);

	if (m_SendThFL == 0)
	{
		SendPost(pSession);
	}

	Release(pSession, pSession->m_SessionID, InterlockedDecrement64(&pSession->m_RefCnt));

	return true;
}

bool CGameLibrary::Disconnect(UINT64 SessionID)
{
	int retrel;
	CSession* pSession = nullptr;


	FindSession(SessionID, &pSession);

	if (pSession == nullptr)
		return false;

	// 세션 찾았으면 유효성 확인해야 함.
	if (!SessionInvalid(pSession, SessionID))
		return false;


	InterlockedExchange(&pSession->m_DCFlag, 1);

	if (!CancelIoEx((HANDLE)pSession->m_Socket, &pSession->m_RecvOverlapped))
	{
		LOG(L"Contents", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"CNetServer::Disconnect()_CancelIOEx Recv Failed / Session ID : %llu / Error Code : %d", SessionID, GetLastError());
	}
	if (!CancelIoEx((HANDLE)pSession->m_Socket, &pSession->m_SendOverlapped))
	{
		LOG(L"Contents", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"CNetServer::Disconnect()_CancelIOEx Send Failed / Session ID : %llu / Error Code : %d", SessionID, GetLastError());
	}

	Release(pSession, pSession->m_SessionID, InterlockedDecrement64(&pSession->m_RefCnt));

	return true;
}

bool CGameLibrary::FindIP(UINT64 SessionID, std::wstring& OutIP)
{
	int retrel;
	SOCKADDR_IN ClientAddr;
	INT         len;
	INT64       ret;
	CSession* pSession;

	FindSession(SessionID, &pSession);
	if (pSession == nullptr)
		return false;

	// 세션 찾았으면 유효성 확인해야 함.
	if (!SessionInvalid(pSession, SessionID))
		return false;

	len = sizeof(ClientAddr);
	getpeername(pSession->m_Socket, (sockaddr*)&ClientAddr, &len);

	InetNtop(AF_INET, &ClientAddr.sin_addr, (PWSTR)OutIP.c_str(), df_GAMELIB_IP_LEN);


	Release(pSession, pSession->m_SessionID, InterlockedDecrement64(&pSession->m_RefCnt));

	return true;
}

bool CGameLibrary::GroupMove(std::wstring ToContents, UINT64 sessionID, IUser* pUser)
{
	// 세션 유효성 체크
	CSession* pSession = nullptr;

	FindSession(sessionID, &pSession);
	if (pSession == nullptr)
		return false;

	// 세션 유효성 체크
	if (!SessionInvalid(pSession, sessionID))
		return false;


	// wstring에 해당되는 그룹 ID 찾기
	std::unordered_map<std::wstring, UINT16>::iterator it = m_GroupMap.find(ToContents);

	if (it == m_GroupMap.end())
	{
		// 문자열과 대응되는 그룹 ID 없음
		__debugbreak();
	}


	CMessage* pMessage = CMessage::Alloc();
	pMessage->Clear();

	*pMessage << sessionID;
	*pMessage << (UINT64)pUser;
	*pMessage << it->second;


	PostQueuedCompletionStatus(m_IOCP, NULL, (ULONG_PTR)pMessage, (LPOVERLAPPED)en_GROUPMOVE);
	
	return true;
}

LONG CGameLibrary::GetAcceptTPS()
{
	return m_AcceptTPS;
}

LONG CGameLibrary::GetRecvIOTPS()
{
	return m_RecvIOTPS;
}

LONG CGameLibrary::GetSendIOTPS()
{
	return m_SendIOTPS;
}

INT64 CGameLibrary::GetAcceptTotal()
{
	return m_AcceptTotal;
}

SHORT CGameLibrary::GetCurSessionCount()
{
	return m_CurSessionCnt;
}

void CGameLibrary::SetAcceptTPS(LONG value)
{
	m_AcceptTPS = value;
}

void CGameLibrary::SetRecvIOTPS(LONG value)
{
	m_RecvIOTPS = value;
}

void CGameLibrary::SetSendIOTPS(LONG value)
{
	m_SendIOTPS = value;
}

CGroup* CGameLibrary::GetGroupPtr(std::wstring Contents)
{
	// wstring에 해당되는 그룹 ID 찾기
	std::unordered_map<std::wstring, UINT16>::iterator it = m_GroupMap.find(Contents);

	return m_GroupArray[it->second];
}

bool CGameLibrary::RecvPost(CSession* pSession)
{
	int ret;
	int err;
	int retrel;
	DWORD bytesReceived = 0;
	DWORD flags = 0;
	DWORD curThreadID = GetCurrentThreadId();
	WSABUF wsa[2];
	wsa[0].buf = pSession->m_RecvQ.GetWritePtr();
	wsa[0].len = pSession->m_RecvQ.DirectEnqueueSize();
	wsa[1].buf = pSession->m_RecvQ.GetAllocPtr();
	wsa[1].len = pSession->m_RecvQ.GetFreeSize() - wsa[0].len;


	if (pSession->m_DCFlag == 1)
		return false;

	InterlockedIncrement64(&pSession->m_RefCnt);

	ZeroMemory(&pSession->m_RecvOverlapped, sizeof(OVERLAPPED));

	ret = WSARecv(pSession->m_Socket, wsa, 2, &bytesReceived, &flags, &pSession->m_RecvOverlapped, NULL);
	if (ret == SOCKET_ERROR)
	{
		err = WSAGetLastError();

		//비동기 등록했으면
		if (err == ERROR_IO_PENDING)
			return true;

		else if (err != ERROR_IO_PENDING)
		{

			InterlockedExchange(&pSession->m_DCFlag, 1);
			CancelIoEx((HANDLE)pSession->m_Socket, &pSession->m_SendOverlapped);

			//비정상적인 에러시 로그 남기기
			if (err != WSAECONNRESET && err != WSAECONNABORTED && err != WSAEINTR)
			{
				LOG(L"NetLibrary", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"SendPost WSASend Return Failed \ Error Code : %d \ SessionID  : %d ", err, pSession->m_SessionID);
			}

			Release(pSession, pSession->m_SessionID, InterlockedDecrement64(&pSession->m_RefCnt));

		}

	}

	//wsarecv 리턴값이 0인게 wsarecv가 정상적으로 등록되었거나 동기적으로 완료되었다는 것임.
	else if (ret == 0)
		return true;

	return false;
}

bool CGameLibrary::SendPost(CSession* pSession)
{
	int ret;
	int err;
	int retrel;
	int usesize;
	DWORD bytesSend = 0;
	DWORD flags = 0;

	DWORD curThreadID = GetCurrentThreadId();

	if (pSession->m_DCFlag == 1)
		return false;

	if (m_SendThFL == 0)
	{
		while (1)
		{
			if (InterlockedExchange16(&pSession->m_SendFlag, 1) == 1)
			{
				return false;
			}

			usesize = pSession->m_SendQ.GetUseSize();
			if (usesize == 0)
			{
				InterlockedExchange16(&pSession->m_SendFlag, 0);

				//size 한번더 체크
				usesize = pSession->m_SendQ.GetUseSize();
				if (usesize == 0)
				{
					return false;
				}

				//2번째 size가 0이아니면 누가 넣었으니 다시 send flag 획득 시도
				continue;
			}

			//첫번째 size 체크시 0아니면 Send작업
			break;
		}
	}
	else
	{
		pSession->m_SendFlag = 1;
	}


	WSABUF wsa[df_SERVER_WSABUFSIZE];

	//송신 락프리큐에서 데이터 꺼내기(없으면 false 리턴)
	CMessage* temp;
	int index;
	for (index = 0; index < df_SERVER_WSABUFSIZE; index++)
	{
		if (!pSession->m_SendQ.Dequeue(pSession->m_SendArray[index]))
			break;
	}


	//송신 링버퍼에 아무것도 들어간게 없으면 false 리턴
	if (index == 0)
	{
		__debugbreak();
		return false;
	}

	//Dequeue 성공해서 SendArray에 저장한 갯수 갱신
	pSession->m_SendMsgCnt = index;

	//SendArray에 있는것을 wsaBuf에 셋팅
	for (int i = 0; i < index; i++)
	{
		wsa[i].buf = pSession->m_SendArray[i]->GetAllocPos();
		wsa[i].len = pSession->m_SendArray[i]->GetRealDataSize();
	}


	InterlockedIncrement64(&pSession->m_RefCnt);
	ZeroMemory(&pSession->m_SendOverlapped, sizeof(OVERLAPPED));

	ret = WSASend(pSession->m_Socket, wsa, pSession->m_SendMsgCnt, NULL, flags, &pSession->m_SendOverlapped, NULL);
	if (ret == SOCKET_ERROR)
	{
		err = WSAGetLastError();

		if (err == ERROR_IO_PENDING)
		{

			LOG(L"GameLibrary", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"SendPost_IO_PENDING \ Session ID : %llu ", pSession->m_SessionID);
			return true;
		}

		else if (err != ERROR_IO_PENDING)
		{
			InterlockedExchange(&pSession->m_DCFlag, 1);
			CancelIoEx((HANDLE)pSession->m_Socket, &pSession->m_RecvOverlapped);

			//비정상적인 에러시 로그 남기기
			if (err != WSAECONNRESET && err != WSAECONNABORTED && err != WSAEINTR)
			{
				LOG(L"GameLibrary", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"SendPost WSASend Return Failed \ Error Code : %d \ SessionID  : %llu ", err, pSession->m_SessionID);
			}


			Release(pSession, pSession->m_SessionID, InterlockedDecrement64(&pSession->m_RefCnt));

			return false;
		}

	}

	//동기 정상 등록한 경우
	else if (ret == 0)
	{
		return true;
	}

	return false;
}

bool CGameLibrary::Release(CSession* pSession, UINT64 CheckID, long long retIOCount)
{
	CMessage* peek = nullptr;
	st_IOFLAG check;
	check.s_cnt = 0;
	check.s_flag = 0;

	if (retIOCount < 0)
		__debugbreak();

	if (!InterlockedCompareExchange128(&pSession->m_RefCnt, 1, 0, (long long*)&check))
		return false;

	PostQueuedCompletionStatus(m_IOCP, 0, (ULONG_PTR)pSession, (LPOVERLAPPED)en_RELEASE);
	return true;
}

bool CGameLibrary::SessionInvalid(CSession* pSession, UINT64 CheckID)
{
	int retrel;
	retrel = InterlockedIncrement64(&pSession->m_RefCnt);

	//그런데 이미 누가 Release 하고 있으면 쓰면 안되니 감소 시키고 Release 
	if (pSession->m_RelFlag == 1)
	{
		Release(pSession, pSession->m_SessionID, InterlockedDecrement64((long long*)&pSession->m_RefCnt));
		return false;
	}

	//내가 찾던 세션이 아니라면 증가시킨것 감소
	if (CheckID != pSession->m_SessionID)
	{
		Release(pSession, pSession->m_SessionID, InterlockedDecrement64((long long*)&pSession->m_RefCnt));
		return false;
	}

	return true;
}

void CGameLibrary::RecvIOProc(CSession* pSession, DWORD cbTransferred)
{
	INT retPeekHeader = 0;
	INT retPeekPayload = 0;
	BOOL RecvError = false;
	LONGLONG retrel;

	if (cbTransferred == 0)
	{
		InterlockedExchange(&pSession->m_DCFlag, 1);
	}

	pSession->m_RecvQ.MoveWritePos(cbTransferred);

	//수신 링버퍼 빌 때 까지 메세지 추출해서 바로 클라이언트에게 보냄
	while (1)
	{

		if (pSession->m_DCFlag == 1)
			break;


		CMessage* pPacket = CMessage::Alloc();
		pPacket->Clear();

		//RecvQ에서 완성된 메세지 확인
		NETHEADER header;



		//수신 링버퍼에 len이 네트워크 헤더인데 이정도도 없으면 그냥 끝내기
		int usesize = pSession->m_RecvQ.GetUseSize();
		if (usesize <= sizeof(st_NETHEADER))
		{
			CMessage::Free(pPacket);
			break;
		}

		//네트워크 헤더 추출시 체크섬 제외하고 추출
		retPeekHeader = pSession->m_RecvQ.Peek((char*)&header, sizeof(st_NETHEADER));

		if (retPeekHeader == 0)
		{
			__debugbreak();
		}

		// 네트워크 헤더 코드 체크
		if (header.s_code != m_PacketCode)
		{
			LOG(L"NetLibrary", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"CNetServer::WorkerThread RecvIO NetHeader PacketCode Error / Session ID : %lld ", pSession->m_SessionID);
			Disconnect(pSession->m_SessionID);

			CMessage::Free(pPacket);
			break;
		}

		//헤더의 페이로드 len이 0이하 조작 메세지
		if (header.s_len <= 0)
		{
			RecvError = true;

			LOG(L"GameLibrary", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"CNetServer::WorkerThread RecvIO NetHeader Len Error / Session ID : %lld", pSession->m_SessionID);
			Disconnect(pSession->m_SessionID);

			CMessage::Free(pPacket);
			break;
		}


		//수신 링버퍼에 남은게 네트워크 헤더 + payload 크기 보다 작으면 그냥 peek만 해서 네트워크 헤더 보고 나가는 것임.
		if (usesize < header.s_len + sizeof(NETHEADER))
		{
			CMessage::Free(pPacket);
			break;
		}

		// 직렬화 버퍼 크기 이상으로 메세지가 온 경우
		if (pPacket->GetBufferSize() < header.s_len + sizeof(NETHEADER))
		{
			Disconnect(pSession->m_SessionID);
			CMessage::Free(pPacket);
			break;
		}


		//네트워크 헤더(체크섬 제외) 뽑은 만큼 옮기기
		pSession->m_RecvQ.MoveReadPos(retPeekHeader - sizeof(header.s_checksum));


		// 수신 링버퍼에서 체크섬 및 페이로드 추출 후 직렬화 버퍼에 저장
		retPeekPayload = pSession->m_RecvQ.Peek(pPacket->GetWritePos(), header.s_len + sizeof(header.s_checksum));
		if (retPeekPayload == 0)
		{
			__debugbreak();
		}

		pPacket->MoveWritePos(retPeekPayload);

		//추출한 직렬화 버퍼 디코딩
		Decoding(pPacket->GetReadPos(), header.s_len + sizeof(header.s_checksum), header.s_randkey);

		//디코딩 했으면 체크섬 계산
		header.s_checksum = *(pPacket->GetReadPos()); // 체크섬 저장
		pPacket->MoveReadPos(sizeof(header.s_checksum));

		CHAR* temprpos = pPacket->GetReadPos();
		INT  sum = 0;
		for (int i = 0; i < header.s_len; ++i)
		{
			sum += (UCHAR) * (temprpos);
			temprpos++;
		}

		//체크섬 다르면 디버깅 위해 중단
		if (header.s_checksum != (sum % 256))
		{
			RecvError = true;
			LOG(L"GameLibrary", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"CNetServer::WorkerThread RecvIO CheckSum Error \ Session ID : %llu ...", pSession->m_SessionID);
			Disconnect(pSession->m_SessionID);
			CMessage::Free(pPacket);
			break;
		}

		UINT16 id = pSession->m_GroupID;
		if (m_GroupArray[id]->GetSharedFlag())
		{
			m_GroupArray[id]->SharedGroupLock();
			m_GroupArray[id]->OnRecv(pSession->m_SessionID, pPacket);
			m_GroupArray[id]->SharedGroupUnlock();
		}
		else
		{
			m_GroupArray[id]->ExclusiveGroupLock();
			m_GroupArray[id]->OnRecv(pSession->m_SessionID, pPacket);
			m_GroupArray[id]->ExclusiveGroupUnlock();
		}

		CMessage::Free(pPacket);

		//Recv 버퍼 밀기
		pSession->m_RecvQ.MoveReadPos(retPeekPayload);

	}

	InterlockedIncrement(&m_RecvIOTPS);

	// Disconnect 아닐 때 RecvIO 등록
	if (pSession->m_DCFlag != 1)
	{
		RecvPost(pSession);
	}


	Release(pSession, pSession->m_SessionID, InterlockedDecrement64(&pSession->m_RefCnt));

	return;
}

void CGameLibrary::SendIOProc(CSession* pSession, DWORD cbTransferred)
{
	LONGLONG retrel;


	//사용한 직렬화 버퍼 메세지 메모리 풀에 반납
	for (int i = 0; i < pSession->m_SendMsgCnt; i++)
	{
		CMessage::Free(pSession->m_SendArray[i]);
	}

	//SendMessage 완료 했으니 증가
	InterlockedIncrement(&m_SendIOTPS);

	pSession->m_SendMsgCnt = 0;
	InterlockedExchange16(&pSession->m_SendFlag, 0);

	if (m_SendThFL == 0)
	{
		SendPost(pSession);
	}

	Release(pSession, pSession->m_SessionID, InterlockedDecrement64(&pSession->m_RefCnt));
}

void CGameLibrary::Encoding(char* ptr, int len, UCHAR randkey)
{
	UCHAR P = 0;
	UCHAR E = 0;

	for (int i = 0; i < len; i++)
	{
		P = (*ptr) ^ (P + randkey + i + 1);
		E = P ^ (E + m_FixedKey + i + 1);
		*ptr = E;
		ptr++;
	}
}

void CGameLibrary::Decoding(char* ptr, int len, UCHAR randkey)
{
	UCHAR P1 = 0;
	UCHAR P2 = 0;
	UCHAR D = 0;
	UCHAR E1 = 0;

	for (int i = 0; i < len; i++)
	{
		P2 = (*ptr) ^ (E1 + m_FixedKey + i + 1);
		D = P2 ^ (P1 + randkey + i + 1);
		P1 = P2;
		E1 = *ptr;
		*ptr = D;
		ptr++;
	}
}

void CGameLibrary::FindSession(UINT64 SessionID, CSession** ppSession)
{
	UINT64 index;

	*ppSession = nullptr;

	if (SessionID == df_INVALID_SESSIONID)
		return;

	// 인자로받은 세션 ID에서 Index 추출
	index = SessionID >> df_GAMELIB_INDEX_POS;

	if (index < 0 || index >= m_MaxSessionCnt)
	{
		__debugbreak();
	}

	*ppSession = &m_SessionTable[index];
}

UINT64 CGameLibrary::MakeSessionID(UINT16 index, UINT64 allocID)
{
	UINT64     idx;
	idx = index;
	idx = idx << df_GAMELIB_INDEX_POS;

	return (UINT64)(idx | allocID);
}

void CGameLibrary::ReleaseProc(CSession* pSession)
{
	CMessage* peek = nullptr;

	/*
	* 실제 Release 작업
	*/

	//SendArray에 있는 직렬화 버퍼 반납.(SendPost 내부에서 Release될 수 있음)
	for (int i = 0; i < pSession->m_SendMsgCnt; i++)
	{
		CMessage::Free(pSession->m_SendArray[i]);
	}

	//Send 락프리 큐에 있는 직렬화 버퍼 반납(SendPacket 함수에서 Release 될 수 있음)
	while (1)
	{
		//빼낼게 없으면 탈출
		if (!pSession->m_SendQ.Dequeue(peek))
			break;

		CMessage::Free(peek);

	}

	m_GroupArray[pSession->m_GroupID]->ExclusiveGroupLock();
	m_GroupArray[pSession->m_GroupID]->OnClientLeave(pSession->m_SessionID);
	m_GroupArray[pSession->m_GroupID]->ExclusiveGroupUnlock();


	//그 플래그를 바꾼 스레드가 EmptyIndexStack에 index를 push 해야 함.
	//index 추출
	uint16_t index;

	index = pSession->m_SessionID >> df_GAMELIB_INDEX_POS;
	closesocket(pSession->m_Socket);


	m_pSessionIdxStack->Push(index);
	InterlockedDecrement16(&m_CurSessionCnt);
}

void CGameLibrary::FrameProc(UINT16 groupID)
{
	m_GroupArray[groupID]->ExclusiveGroupLock();
	m_GroupArray[groupID]->OnUpdate();
	m_GroupArray[groupID]->ExclusiveGroupUnlock();
}

void CGameLibrary::GroupMoveProc(CMessage* pMessage)
{
	UINT64 sessionID;
	IUser* pUser;
	CSession* pSession;
	UINT16 groupid;

	*pMessage >> sessionID;
	*pMessage >> (UINT64&)pUser;
	*pMessage >> groupid;
	CMessage::Free(pMessage);

	FindSession(sessionID, &pSession);
	if (pSession == nullptr)
	{
		__debugbreak();
	}

	m_GroupArray[groupid]->ExclusiveGroupLock();
	m_GroupArray[groupid]->OnIUserMove(sessionID, pUser);
	m_GroupArray[groupid]->ExclusiveGroupUnlock();

	pSession->m_GroupID = groupid;

	Release(pSession, pSession->m_SessionID, InterlockedDecrement64(&pSession->m_RefCnt));
}

