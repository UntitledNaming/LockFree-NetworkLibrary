#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <string>
#include <thread>
#include <timeapi.h>
#include <unordered_map>

#include "LibraryHeader.h"
#include "LogClass.h"
#include "MemoryPoolTLS.h"
#include "CMessage.h"
#include "LFStack.h"
#include "Ring_Buffer.h"
#include "LFQMultiLive.h"
#include "CSession.h"
#include "CNetServer.h"
#pragma warning(disable:4996)

CNetServer::CNetServer() : m_AcceptTPS(0), m_AcceptTotal(0), m_AllocID(0), m_ConcurrentCnt(0), m_CreateWorkerCnt(0), m_CurSessionCnt(0), m_FixedKey(0), 
m_IOCP(INVALID_HANDLE_VALUE), m_Listen(INVALID_SOCKET), m_MaxSessionCnt(0), m_Nagle(false), m_PacketCode(0), m_Port(-1), m_RecvIOTPS(0),m_SendFrame(-1),m_SendIOTPS(0)
,m_SendThFL(0),m_SessionTable(nullptr), m_pSessionIdxStack(nullptr)
{

}

CNetServer::~CNetServer()
{
}

bool CNetServer::Start(WCHAR* SERVERIP, int SERVERPORT, int numberOfCreateThread, int numberOfRunningThread, int maxNumOfSession, int SendSleep, int SendTHFL ,WORD packetCode, WORD fixedkey, bool Nagle)
{
	// Config 파일에서 얻어온 정보 네트워크 라이브러리 멤버 세팅
	m_IP = *SERVERIP;
	m_Port = SERVERPORT;
	m_MaxSessionCnt = maxNumOfSession;
	m_CreateWorkerCnt = numberOfCreateThread;
	m_ConcurrentCnt = numberOfRunningThread;
	m_PacketCode = packetCode;
	m_FixedKey = fixedkey;
	m_SendFrame = SendSleep;
	m_SendThFL = SendTHFL;
	m_Nagle = Nagle;

	Mem_Init();

	Net_Init(SERVERIP, SERVERPORT, Nagle);

	Thread_Create();

	return true;
}

bool CNetServer::Disconnect(UINT64 SessionID)
{
	CSession* pSession = nullptr;


	FindSession(SessionID, &pSession);

	if (pSession == nullptr)
		return false;


	// 세션 찾았으면 유효성 확인해야 함.
	if (!SessionInvalid(pSession, SessionID))
	{
		return false;
	}

	if (InterlockedExchange(&pSession->m_DCFlag, 1) != 0)
	{
		Release(pSession, InterlockedDecrement64(&pSession->m_RefCnt));
		return false;
	}


	CancelIoEx((HANDLE)pSession->m_Socket, &pSession->m_RecvOverlapped);
	CancelIoEx((HANDLE)pSession->m_Socket, &pSession->m_SendOverlapped);

	Release(pSession,  InterlockedDecrement64(&pSession->m_RefCnt));

	return true;
}

bool CNetServer::SendPacket(UINT64 SessionID, CMessage* pMessage)
{
	CSession* pSession;
	DWORD curThreadID = GetCurrentThreadId();

	FindSession(SessionID, &pSession);

	//못찾은 것임
	if (pSession == nullptr)
		return false;


	// 세션 찾았으면 유효성 확인해야 함.
	if (!SessionInvalid(pSession, SessionID))
	{
		return false;
	}

	if (pSession->m_SendQ.GetUseSize() >= SENDQ_MAX_SIZE)
	{
		Disconnect(pSession->m_SessionID);
		LOG(L"CNetLibrary", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"SendPacket SendQ Full / SessionID : %lld , Time : %d ", pSession->m_SessionID, timeGetTime());
		Release(pSession,  InterlockedDecrement64(&pSession->m_RefCnt));

		return false;
	}


	//인코딩 작업
	if (pMessage->GetEncodingFlag() == 0)
	{
		//컨텐츠 메세지 길이 구해서 네트워크 헤더 만들기
		NETHEADER header;
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


	Release(pSession, InterlockedDecrement64(&pSession->m_RefCnt));

	return true;
}

bool CNetServer::SendPacketAll(CMessage* pMessage)
{
	// 전체 세션에 대해서 유효한 ID면 일단 SendPacket 호출.
	for (int i = 0; i < m_MaxSessionCnt; i++)
	{
		if (m_SessionTable[i].m_SessionID == df_INVALID_SESSIONID)
			continue;

		SendPacket(m_SessionTable[i].m_SessionID, pMessage);

	}

	return true;
}

bool CNetServer::FindIP(UINT64 SessionID, WCHAR* OutIP)
{
	SOCKADDR_IN ClientAddr;
	INT         len;
	CSession* pSession;

	FindSession(SessionID, &pSession);
	if (pSession == nullptr)
		return false;

	// 세션 찾았으면 유효성 확인해야 함.
	if (!SessionInvalid(pSession, SessionID))
		return false;

	len = sizeof(ClientAddr);
	getpeername(pSession->m_Socket, (sockaddr*)&ClientAddr, &len);

	InetNtop(AF_INET, &ClientAddr.sin_addr, (PWSTR)OutIP, IP_LEN);

	Release(pSession,InterlockedDecrement64(&pSession->m_RefCnt));

	return true;
}

void CNetServer::Stop()
{
	closesocket(m_Listen);
	LOG(L"CNetLibrary", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L"CNetServer::Stop()_closesocekt(g_ListenSocket) Complete...");

	// 전체 세션 Release 작업
	while (1)
	{
		if (m_CurSessionCnt == 0)
			break;

		for (int i = 0; i < m_MaxSessionCnt; i++)
		{
			if (m_SessionTable[i].m_SessionID == INVALID_ID)
				continue;

			Disconnect(m_SessionTable[i].m_SessionID);
		}

	}

	// IOCP 닫으면 워커 스레드는 자동으로 while문 탈출함
	CloseHandle(m_IOCP);
	LOG(L"CNetLibrary", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L"CNetServer::Stop()_CloseHandle(IOCP) Complete...");


	Thread_Destroy();


	// 멤버 객체 정리
	delete[] m_SessionTable;
	delete   m_pSessionIdxStack;
}

void CNetServer::WorkerThread()
{
	wprintf(L"WorkerThread Start.. %d \n", GetCurrentThreadId());
	LOG(L"CNetLibrary", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L"WorkerThread  Start... : %d ", GetCurrentThreadId());

	timeBeginPeriod(1);

	BOOL  retval = true;
	DWORD err = -1;
	bool ESC = false;

	while (!ESC)
	{
		DWORD       cbTransferred = 0;
		OVERLAPPED* pOverlapped = nullptr;     // IO에 사용된 Overlapped 구조체 주소값 or Task Type
		CSession*   pSession = nullptr;
		INT         sum = 0;                   //체크섬 계산

		retval = GetQueuedCompletionStatus(m_IOCP, &cbTransferred, (PULONG_PTR)&pSession, (LPOVERLAPPED*)&pOverlapped, INFINITE);
		
		
		//false 인 상황(IOCP 완료 통지 큐에서 디큐잉이 안된 경우, IOCP 핸들이 CloseHandle로 인해 닫힌 경우)
		if (retval == false)
		{
			//IOCP가 닫히거나 IOCP 완료 통지 큐에서 디큐잉에 실패할 때 처리
			if (pOverlapped == nullptr)
			{
				err = GetLastError();
				LOG(L"CNetLibrary", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L"WorkerThread GQCS IOCP Failed / Error Code : %d", err);

				ESC = true;
				break;
			}

			//WSASend, WSARecv로 요청한 IO가 실패한 경우(TCP가 끊긴 것임)
			else
			{
				err = GetLastError();
				LOG(L"CNetLibrary", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"WorkerThread GQCS IO Failed / Error Code : %d", err);
			}

		}


		if (pOverlapped == (LPOVERLAPPED)en_RELEASE)
		{
			ReleaseProc(pSession);
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


	LOG(L"CNetLibrary", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L"WorkerThread  End... : %d ", GetCurrentThreadId());
	wprintf(L"WorkerThread End... %d \n", GetCurrentThreadId());
	return;
}

void CNetServer::AcceptThread()
{
	LOG(L"CNetLibrary", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L"AcceptThread Start : %d ", GetCurrentThreadId());
	wprintf(L"AcceptThread Start... %d \n", GetCurrentThreadId());

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
			LOG(L"CNetLibrary", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L"AcceptThread accept() Failed / Error Code : %d", WSAGetLastError());
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

		WCHAR szClientIP[IP_LEN] = { 0 };
		InetNtopW(AF_INET, &clientAddr.sin_addr, szClientIP, IP_LEN);
		OnConnectionRequest(szClientIP, clientAddr.sin_port);


		//Overlapped IO로 작동시키기 위해 소켓 송신 버퍼 0으로 설정
		int sendBufferSize = 0;
		setsockopt(client_socket, SOL_SOCKET, SO_SNDBUF, (char*)&sendBufferSize, sizeof(sendBufferSize));

		//네이글 On/Off
		if (m_Nagle == 1)
		{
			int flag = 0;
			if (setsockopt(m_Listen, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(int)) == -1)
			{
				LOG(L"CNetLibrary", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"CNetLibrary::Nagle Error :%d ", WSAGetLastError());
				__debugbreak();
			}
		}
		

		linger so_linger;
		so_linger.l_onoff = 1;  // linger 옵션 사용
		so_linger.l_linger = 0; // 지연 시간 0 -> 즉시 RST

		if (setsockopt(client_socket, SOL_SOCKET, SO_LINGER, (char*)&so_linger, sizeof(so_linger)) == SOCKET_ERROR) 
		{
			LOG(L"CNetLibrary", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"CNetLibrary::Linger Option Set Error :%d ", WSAGetLastError());
		}



		//세션 배열에 넣기 전에 일단 Index 찾기
		UINT16 Index = 0;
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
			LOG(L"CNetLibrary", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"AcceptThread RegisterSocketHANDLE Failed / Session ID : %lld / Error Code : %d ", m_SessionTable[Index].m_SessionID, GetLastError());
			break;
		}


		// 세션이 속한 그룹의 OnClientJoin 호출
		OnClientJoin(sessionID);

		//Recv 등록
		RecvPost(&m_SessionTable[Index]);

		Release(&m_SessionTable[Index],  InterlockedDecrement64(&m_SessionTable[Index].m_RefCnt));

	}

	wprintf(L"AcceptThread End... %d \n", GetCurrentThreadId());
	LOG(L"CNetLibrary", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L"AcceptThread End : %d ", GetCurrentThreadId());
}

void CNetServer::SendThread()
{
	LOG(L"CNetLibrary", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L"SendThread Start : %d ", GetCurrentThreadId());

	DWORD endtime;
	DWORD starttime;

	while (true)
	{
		if (m_SendThFL == 0)
			break;

		starttime = timeGetTime();
		for (int i = 0; i < m_MaxSessionCnt; i++)
		{

			if (m_SessionTable[i].m_SessionID == df_INVALID_SESSIONID)
			{
				continue;
			}

			// 세션 확보
			if (!SessionInvalid(&m_SessionTable[i], m_SessionTable[i].m_SessionID))
			{
				continue;
			}

			// Send 플래그 및 SendQ 체크 시 보내면 안되는 상황에서 세션 포기
			if (m_SessionTable[i].m_SendFlag != 0 || m_SessionTable[i].m_SendQ.GetUseSize() == 0)
			{
				Release(&m_SessionTable[i], InterlockedDecrement64(&m_SessionTable[i].m_RefCnt));
				continue;
			}

			SendPost(&m_SessionTable[i]);

			Release(&m_SessionTable[i],  InterlockedDecrement64(&m_SessionTable[i].m_RefCnt));
		}

		endtime = timeGetTime();

		if (endtime - starttime >= 300)
		{
			LOG(L"CNetLibrary", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L"SendThread 1Frame Send Proc Time : %d ", endtime - starttime);
		}

		// 전체 세션에 대해서 Send 작업 했으며 Sleep으로 버퍼링
		Sleep(m_SendFrame);

	}

	LOG(L"CNetLibrary", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L"SendThread End : %d ", GetCurrentThreadId());
}

void CNetServer::Net_Init(WCHAR* SERVERIP, int SERVERPORT, bool Nagle)
{
	int ret;
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
	{
		wprintf(L"WSAStartUp Failed....! \n");
		LOG(L"CNetLibrary", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"CNetLibrary::Net_Init WSAStartup Error : %d ", WSAGetLastError());
		__debugbreak();
	}

	// 리슨 소켓 생성
	m_Listen = socket(AF_INET, SOCK_STREAM, 0);
	if (m_Listen == INVALID_SOCKET)
	{
		wprintf(L"socket() error \n");
		LOG(L"CNetLibrary", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"CNetLibrary::Net_Init socket() Error : %d ", WSAGetLastError());
		__debugbreak();
	}
	wprintf(L"socket() Complete... \n");


	//네이글 On/Off
	if (Nagle == 1)
	{
		//네이글 키고 싶을 때
		int flag = 0;
		if (setsockopt(m_Listen, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(int)) == -1)
		{
			LOG(L"CLanLibrary", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"CLanLibrary::Start()_Nagle Error :%d ", WSAGetLastError());
			__debugbreak();
		}
		LOG(L"CLanLibrary", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L"CLanLibrary::Start()_Nagle On Complete...");
	}
	else
	{
		//네이글 끄고 싶을 때
		int flag = 1;
		if (setsockopt(m_Listen, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(int)) == -1)
		{
			LOG(L"CLanLibrary", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"CLanLibrary::Start()_Nagle Error :%d ", WSAGetLastError());
			__debugbreak();
		}
		LOG(L"CLanLibrary", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L"CLanLibrary::Start()_Nagle Off Complete...");
	}


	//bind() 처리
	SOCKADDR_IN serveraddr;
	ZeroMemory(&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port = htons(SERVERPORT);
	InetPtonW(AF_INET, SERVERIP, &serveraddr.sin_addr);

	ret = bind(m_Listen, (SOCKADDR*)&serveraddr, sizeof(serveraddr));
	if (ret == SOCKET_ERROR)
	{
		LOG(L"CNetLibrary", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"CNetLibrary::Net_Init bind() Error... / Error Code : %d", WSAGetLastError());
		__debugbreak();
	}


	ret = listen(m_Listen, SOMAXCONN);
	if (ret == SOCKET_ERROR)
	{
		LOG(L"CNetLibrary", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"CNetLibrary::Net_Init listen() Error... / Error Code : %d", WSAGetLastError());
		__debugbreak();
	}

	wprintf(L"listen() Complete... \n");
	LOG(L"CNetLibrary", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L"CNetLibrary::Net_Init listen() Complete... ");
}

void CNetServer::Mem_Init()
{
	m_SessionTable = new CSession[m_MaxSessionCnt];
	m_pSessionIdxStack = new LFStack<UINT16>;
	m_AllocID = 0;
	m_AcceptTPS = 0;
	m_RecvIOTPS = 0;
	m_SendIOTPS = 0;
	m_AcceptTotal = 0;
	m_CurSessionCnt = 0;
	m_Listen = INVALID_SOCKET;

	m_IOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, m_ConcurrentCnt); // 러닝 스레드 갯수로 컨커런트 스레드 설정
	if (m_IOCP == NULL)
	{
		wprintf(L"CreateIoCompletionPort Failed...\n");
		LOG(L"CNetLibrary", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"CreateIoCompletionPort Failed...");

		return;
	}
	wprintf(L"Create IOCP Resoure Success! \n");
	LOG(L"CNetLibrary", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L"CreateIoCompletionPort Complete...");

	for (int i = 0; i < m_MaxSessionCnt; i++)
	{
		m_pSessionIdxStack->Push(i);
			
	}


}

void CNetServer::Thread_Create()
{
	for (int i = 0; i < m_CreateWorkerCnt; i++)
	{
		m_IOCPWorkerThread[i] = std::thread(&CNetServer::WorkerThread, this);
	}

	m_AcceptThread = std::thread(&CNetServer::AcceptThread, this);

	if (m_SendThFL == 1)
	{
		m_SendThread = std::thread(&CNetServer::SendThread, this);
	}
}

void CNetServer::Thread_Destroy()
{
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
		m_SendThFL = 0;

		if (m_SendThread.joinable())
		{
			m_SendThread.join();
		}
	}

}

void CNetServer::FindSession(UINT64 SessionID, CSession** ppSession)
{
	UINT64 index;

	*ppSession = nullptr;

	if (SessionID == df_INVALID_SESSIONID)
		return;

	// 인자로받은 세션 ID에서 Index 추출
	index = SessionID >> df_NET_INDEX_POS;

	*ppSession = &m_SessionTable[index];
}

UINT64 CNetServer::MakeSessionID(UINT16 index, UINT64 allocID)
{
	UINT64     idx;
	idx = index;
	idx = idx << df_NET_INDEX_POS;

	return (UINT64)(idx | allocID);
}

bool CNetServer::RecvPost(CSession* pSession)
{

	int ret;
	int err;
	DWORD bytesReceived = 0;
	DWORD flags = 0;
	DWORD curThreadID = GetCurrentThreadId();
	WSABUF wsa[2];
	wsa[0].buf = pSession->m_RecvQ.GetWritePtr();
	wsa[0].len = static_cast<ULONG>(pSession->m_RecvQ.DirectEnqueueSize());
	wsa[1].buf = pSession->m_RecvQ.GetAllocPtr();
	wsa[1].len = static_cast<ULONG>(pSession->m_RecvQ.GetFreeSize() - wsa[0].len);


	InterlockedIncrement64(&pSession->m_RefCnt);

	ZeroMemory(&pSession->m_RecvOverlapped, sizeof(OVERLAPPED));


	ret = WSARecv(pSession->m_Socket, wsa, 2, &bytesReceived, &flags, &pSession->m_RecvOverlapped, NULL);
	if (ret == SOCKET_ERROR)
	{
		err = WSAGetLastError();

		//비동기 등록했으면
		if (err == ERROR_IO_PENDING)
		{
			return true;
		}

		else if (err != ERROR_IO_PENDING)
		{

			Disconnect(pSession->m_SessionID);

			//비정상적인 에러시 로그 남기기
			if (err != WSAECONNRESET && err != WSAECONNABORTED && err != WSAEINTR)
			{
				LOG(L"NetLibrary", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"SendPost WSASend Return Failed / Error Code : %d / SessionID  : %d ", err, pSession->m_SessionID);
			}

			Release(pSession,  InterlockedDecrement64(&pSession->m_RefCnt));
			return false;
		}

	}

	//wsarecv 리턴값이 0인게 wsarecv가 정상적으로 등록되었거나 동기적으로 완료되었다는 것임.
	else if (ret == 0)
	{
		return true;
	}


	return false;
}

bool CNetServer::SendPost(CSession* pSession)
{

	int ret;
	int err;
	DWORD bytesSend = 0;
	DWORD flags = 0;

	DWORD curThreadID = GetCurrentThreadId();

	if (pSession->m_DCFlag == 1)
	{
		return false;
	}

	if (m_SendThFL == 0)
	{
                                      
		while (1)
		{
			if (InterlockedExchange16(&pSession->m_SendFlag, 1) == 1)
			{
				return false;
			}

			if (pSession->m_SendQ.GetUseSize() == 0)
			{
				InterlockedExchange16(&pSession->m_SendFlag, 0);

				//size 한번더 체크
				if (pSession->m_SendQ.GetUseSize() == 0)
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
	int index;
	for (index = 0; index < df_SERVER_WSABUFSIZE; index++)
	{
		if (!pSession->m_SendQ.Dequeue(pSession->m_SendArray[index]))
			break;
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
			LOG(L"CNetLibrary", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"SendPost_IO_PENDING / Session ID : %llu ", pSession->m_SessionID);
			return true;
		}

		else if (err != ERROR_IO_PENDING)
		{

			Disconnect(pSession->m_SessionID);

			//비정상적인 에러시 로그 남기기
			if (err != WSAECONNRESET && err != WSAECONNABORTED && err != WSAEINTR)
			{
				LOG(L"CNetLibrary", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"SendPost WSASend Return Failed / Error Code : %d / SessionID  : %llu ", err, pSession->m_SessionID);
			}


			Release(pSession,  InterlockedDecrement64(&pSession->m_RefCnt));
			return false;
		}

	}

	else if (ret == 0)
	{
		LOG(L"CNetLibrary", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"SendPost WSARecv Return 0  / Session ID : %d", pSession->m_SessionID);
		return true;
	}

	return false;

}

bool CNetServer::SessionInvalid(CSession* pSession, UINT64 SessionID)
{
	InterlockedIncrement64(&pSession->m_RefCnt);

	//그런데 이미 누가 Release 하고 있으면 쓰면 안되니 감소 시키고 Release 
	if (pSession->m_RelFlag == 1)
	{
		Release(pSession,  InterlockedDecrement64((long long*)&pSession->m_RefCnt));
		return false;
	}

	//내가 찾던 세션이 아니라면 증가시킨것 감소
	if (SessionID != pSession->m_SessionID)
	{
		Release(pSession,  InterlockedDecrement64((long long*)&pSession->m_RefCnt));
		return false;
	}

	return true;
}

bool CNetServer::Release(CSession* pSession, long long retIOCount)
{

	CMessage* peek = nullptr;
	st_IOFLAG check;
	check.Cnt = 0;
	check.Flag = 0;


	if (!InterlockedCompareExchange128(&pSession->m_RefCnt, 1, 0, (long long*)&check))
	{
		return false;
	}


	PostQueuedCompletionStatus(m_IOCP, -1, (ULONG_PTR)pSession, (LPOVERLAPPED)en_RELEASE);

	return true;
}

void CNetServer::RecvIOProc(CSession* pSession, DWORD cbTransferred)
{
	INT retPeekHeader = 0;
	INT retPeekPayload = 0;

	if (cbTransferred == 0)
		pSession->m_DCFlag = 1;

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
		unsigned long long usesize = pSession->m_RecvQ.GetUseSize();
		if (usesize <= sizeof(NETHEADER))
		{
			CMessage::Free(pPacket);
			break;
		}

		//네트워크 헤더 추출시 체크섬 제외하고 추출
		retPeekHeader = pSession->m_RecvQ.Peek((char*)&header, sizeof(NETHEADER));

		if (retPeekHeader == 0)
		{
			__debugbreak();
		}

		// 네트워크 헤더 코드 체크
		if (header.s_code != m_PacketCode)
		{
			LOG(L"CNetLibrary", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"CNetServer::WorkerThread RecvIO NetHeader PacketCode Error / Session ID : %lld ", pSession->m_SessionID);
			Disconnect(pSession->m_SessionID);

			CMessage::Free(pPacket);
			break;
		}

		//헤더의 페이로드 len이 0이하 조작 메세지
		if (header.s_len <= 0)
		{

			LOG(L"CNetLibrary", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"CNetServer::WorkerThread RecvIO NetHeader Len Error / Session ID : %lld", pSession->m_SessionID);
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
			LOG(L"CNetLibrary", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"CNetServer::WorkerThread RecvIO CheckSum Error / Session ID : %llu ...", pSession->m_SessionID);
			Disconnect(pSession->m_SessionID);
			CMessage::Free(pPacket);
			break;
		}

		OnRecv(pSession->m_SessionID, pPacket);

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


	Release(pSession,  InterlockedDecrement64(&pSession->m_RefCnt));
	return;
}

void CNetServer::SendIOProc(CSession* pSession, DWORD cbTransferred)
{

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


	Release(pSession,  InterlockedDecrement64(&pSession->m_RefCnt));

}

void CNetServer::ReleaseProc(CSession* pSession)
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

	OnClientLeave(pSession->m_SessionID);



	//그 플래그를 바꾼 스레드가 EmptyIndexStack에 index를 push 해야 함.
	//index 추출
	uint16_t index;

	index = pSession->m_SessionID >> df_NET_INDEX_POS;
	closesocket(pSession->m_Socket);
	m_pSessionIdxStack->Push(index);
	InterlockedDecrement16(&m_CurSessionCnt);
}

void CNetServer::Encoding(char* ptr, int len, UCHAR randkey)
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

void CNetServer::Decoding(char* ptr, int len, UCHAR randkey)
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
