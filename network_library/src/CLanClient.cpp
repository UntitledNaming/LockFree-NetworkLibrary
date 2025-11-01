#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <string>
#include <thread>
#include <unordered_map>

#include "LibraryHeader.h"
#include "MemoryPoolTLS.h"
#include "CMessage.h"
#include "LFQMultiLive.h"
#include "Ring_Buffer.h"
#include "CLanClient.h"
#include "LogClass.h"
#pragma comment(lib, "Ws2_32.lib")

CLanClient::CLanClient() : m_iocp(INVALID_HANDLE_VALUE), m_pSession(nullptr), m_serverport(-1)
{

}

CLanClient::~CLanClient()
{

}

bool CLanClient::Connect(WCHAR* SERVERIP, INT SERVERPORT)
{
	SOCKET sock = INVALID_SOCKET;

	//윈속 초기화
	WSADATA  wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
	{
		__debugbreak();
	}

	// 멤버 초기화
	if (!Mem_Init(SERVERIP, SERVERPORT))
		return false;

	// 스레드 생성
	Thread_Create();

	// 소켓 생성 및 옵션 설정
	if (!CreateAndSetSocket(sock))
	{

		if (sock != INVALID_SOCKET)
			closesocket(sock);

		return false;
	}


	// connect 시도
	if (!ConnectTry(sock))
	{
		closesocket(sock);
		return false;
	}

	Session_Init(sock);

	OnEnterJoinServer();

	// Recv IO 등록 작업
	if (!RecvPost())
	{
		return false;
	}

	return true;
}

bool CLanClient::Disconnect()
{
	if (InterlockedExchange(&m_pSession->s_DCFlag, 1) != 0)
		return false;

	InterlockedDecrement(&m_pSession->s_IORefCnt);

	CancelIoEx((HANDLE)m_pSession->s_Socket, &m_pSession->s_RecvOverlapped);
	CancelIoEx((HANDLE)m_pSession->s_Socket, &m_pSession->s_SendOverlapped);

	return true;
}

bool CLanClient::SendPacket(CMessage* pMessage)
{
	LANHEADER header;
	header.s_len = pMessage->GetDataSize();

	memcpy_s(pMessage->GetAllocPos(), sizeof(header), (char*)&header, sizeof(header));
	pMessage->AddRef();

	m_pSession->s_SendQ.Enqueue(pMessage);

	SendPost();

	return true;
}

bool CLanClient::ReConnect()
{
	SOCKET sock = INVALID_SOCKET;

	// 세션 Release 할 때 까지 ReConnect 처리 불가
	if (m_pSession->s_RelFlag != 1)
		return false;


	// 세션 정리 되었으면 다시 소켓 생성 및 세션 초기화 작업
	if (!CreateAndSetSocket(sock))
	{
		if (sock != INVALID_SOCKET)
			closesocket(sock);

		return false;
	}

	// connect 시도
	if (!ConnectTry(sock))
	{
		closesocket(sock);
		return false;
	}

	// 세션 초기화
	Session_Init(sock);

	// Recv IO 등록 작업
	if (!RecvPost())
		return false;

	OnEnterJoinServer();

	return true;
}

bool CLanClient::ConnectAlive()
{
	if (m_pSession->s_DCFlag == 1)
		return false;

	return true;
}

void CLanClient::Destroy()
{
	Disconnect();

	// 세션 정리할때 까지 루프
	while (m_pSession->s_RelFlag != 1)
	{

	}

	Thread_Destroy();

	delete m_pSession;
}

void CLanClient::WorkerThread()
{
	int retval;

	DWORD curThreadID = GetCurrentThreadId();

	LOG(L"CLanClient", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L"CLanClient::WorkerThread  Start... : %d ", curThreadID);


	while (1)
	{
		DWORD       cbTransferred = -1;
		OVERLAPPED* pOverlapped = nullptr;//완료한 wsaoverlapped 구조체 주소값
		LONGLONG    Key;
		retval = GetQueuedCompletionStatus(m_iocp, &cbTransferred, (PULONG_PTR)&Key, (LPOVERLAPPED*)&pOverlapped, INFINITE);


		//false 인 상황(IOCP 완료 통지 큐에서 디큐잉이 안된 경우, IOCP 핸들이 CloseHandle로 인해 닫힌 경우)
		if (retval == false)
		{
			//IOCP가 닫히거나 IOCP 완료 통지 큐에서 디큐잉에 실패할 때 처리
			if (pOverlapped == nullptr)
			{
				LOG(L"CLanClient", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"WorkerThread GQCS IOCP Failed  / Error Code : %d", GetLastError());

				//어차피 이 경우에는 그냥 워커 스레드를 파괴해야 함.
				break;
			}

			//WSASend, WSARecv로 요청한 IO가 실패한 경우(TCP가 끊긴 것임)
			else
			{
				//어차피 워커 스레드 하단에서 알아서 Release할 것임.
				LOG(L"CLanClient", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"WorkerThread GQCS IO Failed / Error Code : %d", GetLastError());

			}

		}

		
		//Recv 완료인 경우
		if (pOverlapped  == &m_pSession->s_RecvOverlapped )
		{
			RecvIOProc(cbTransferred);
		}

		//Send 완료인 경우
		else if (pOverlapped  == &m_pSession->s_SendOverlapped)
		{
			SendIOProc(cbTransferred);
		}

	}

	LOG(L"CLanClient", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L"CLanClient::WorkerThread  End... : %d ", curThreadID);

}

bool CLanClient::Mem_Init( WCHAR* ServerIP, INT ServerPort)
{
	m_serverIP = ServerIP;
	m_serverport = ServerPort;
	m_pSession = new SESSION;

	// IOCP 객체 생성 및 초기화
	m_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, CLIENT_WORKER_COUNT); // 러닝 스레드 갯수로 컨커런트 스레드 설정
	if (m_iocp == NULL)
	{
		LOG(L"CLanClient", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"CLanClient::Connect()_CreateIoCompletionPort Failed...");
		return false;
	}

	LOG(L"CLanClient", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L"CLanClient::Connect()_CreateIoCompletionPort Complete...");
	return true;
}

bool CLanClient::CreateAndSetSocket(SOCKET& outParam)
{
	SOCKET sock;

	//socket 생성
	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == INVALID_SOCKET)
	{
		LOG(L"CLanClient", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"CLanClient::socket() ErrorCode : %d ...", WSAGetLastError());
		return false;
	}

	// Overlapped IO로 작동시키기 위해 소켓 송신 버퍼 0으로 설정
	int sendBufferSize = 0;
	setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char*)&sendBufferSize, sizeof(sendBufferSize));

	// RST 날리기 위해 linger 옵션 설정
	linger so_linger;
	so_linger.l_onoff = 1;  // linger 옵션 사용
	so_linger.l_linger = 0; // 지연 시간 0 -> 즉시 RST

	if (setsockopt(sock, SOL_SOCKET, SO_LINGER, (char*)&so_linger, sizeof(so_linger)) == SOCKET_ERROR)
	{
		LOG(L"CNetLibrary", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"CNetLibrary::Linger Option Set Error :%d ", WSAGetLastError());
		return false;
	}


	// IOCP에 소켓 등록
	HANDLE retCIOCP;
	retCIOCP = CreateIoCompletionPort((HANDLE)sock, m_iocp, (ULONG_PTR)NULL, 0);
	if (retCIOCP == NULL)
	{
		LOG(L"CLanClient", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"CLanClient RegisterSocketHANDLE Failed  / Error Code : %d ", GetLastError());
		return false;
	}

	outParam = sock;
	return true;
}

bool CLanClient::ConnectTry(SOCKET inputParam)
{
	int retval;

	//서버와 connect 위해 소켓 구조체에 서버 IP, PORT 지정해서 connect 함수에 넘기기
	SOCKADDR_IN clientaddr;
	ZeroMemory(&clientaddr, sizeof(clientaddr));
	clientaddr.sin_family = AF_INET;
	clientaddr.sin_port = htons(m_serverport);
	InetPtonW(clientaddr.sin_family, m_serverIP.c_str(), &clientaddr.sin_addr); //문자열 IP를 정수값IP로 바꾸는 함수

	retval = connect(inputParam, (SOCKADDR*)&clientaddr, sizeof(clientaddr));
	if (retval == SOCKET_ERROR)
	{
		LOG(L"CLanClient", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"CLanClient::connect() ErrorCode : %d ...", WSAGetLastError());
		return false;
	}


	return true;
}

bool CLanClient::RecvPost()
{
	int ret;
	int err;
	DWORD bytesReceived = 0;
	DWORD flags = 0;

	if (m_pSession->s_DCFlag == 1)
		return false;


	WSABUF wsa[2];
	wsa[0].buf = m_pSession->s_RecvQ.GetWritePtr();
	wsa[0].len = static_cast<ULONG>(m_pSession->s_RecvQ.DirectEnqueueSize());
	wsa[1].buf = m_pSession->s_RecvQ.GetAllocPtr();
	wsa[1].len = static_cast<ULONG>(m_pSession->s_RecvQ.GetFreeSize() - wsa[0].len);

	InterlockedIncrement(&m_pSession->s_IORefCnt);

	ZeroMemory(&m_pSession->s_RecvOverlapped, sizeof(OVERLAPPED));

	ret = WSARecv(m_pSession->s_Socket, wsa, 2, &bytesReceived, &flags, &m_pSession->s_RecvOverlapped, NULL);
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

			Disconnect();

			//비정상적인 에러시 로그 남기기
			if (err != WSAECONNRESET && err != WSAECONNABORTED && err != WSAEINTR)
			{
				LOG(L"CLanClient", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"SendPost WSASend Return Failed / Error Code : %d", err);
			}

			Release(InterlockedDecrement(&m_pSession->s_IORefCnt));
		}

	}

	//wsarecv 리턴값이 0인게 wsarecv가 정상적으로 등록되었거나 동기적으로 완료되었다는 것임.
	else if (ret == 0)
		return true;


	return false;
}

bool CLanClient::SendPost()
{
	int ret;
	int err;
	DWORD bytesSend = 0;
	DWORD flags = 0;

	DWORD curThreadID = GetCurrentThreadId();

	if (m_pSession->s_DCFlag == 1)
		return false;

	//만약에 send flag를 바꾸는데 원래 send flag 상태가 1이라면 io 등록 이미 했으니 바로 나오기
	while (1)
	{
		if (InterlockedExchange16(&m_pSession->s_SendFlag, 1) == 1)
		{
			return false;
		}

		if (m_pSession->s_SendQ.GetUseSize() == 0)
		{
			InterlockedExchange16(&m_pSession->s_SendFlag, 0);

			//size 한번더 체크
			if (m_pSession->s_SendQ.GetUseSize() == 0)
			{
				return false;
			}

			//2번째 size가 0이아니면 누가 넣었으니 다시 send flag 획득 시도
			continue;
		}

		//첫번째 size 체크시 0아니면 Send작업
		break;
	}


	WSABUF wsa[CLIENT_WSABUFSIZE];

	//송신 락프리큐에서 데이터 꺼내기(없으면 false 리턴)
	int index;
	for (index = 0; index < CLIENT_WSABUFSIZE; index++)
	{
		if (!m_pSession->s_SendQ.Dequeue(m_pSession->s_SendArray[index]))
			break;
	}

	//Dequeue 성공해서 SendArray에 저장한 갯수 갱신
	m_pSession->s_SendMsgCnt = index;

	//SendArray에 있는것을 wsaBuf에 셋팅
	for (int i = 0; i < index; i++)
	{
		wsa[i].buf = m_pSession->s_SendArray[i]->GetAllocPos();
		wsa[i].len = m_pSession->s_SendArray[i]->GetRealDataSize(1);
	}


	InterlockedIncrement(&m_pSession->s_IORefCnt);
	ZeroMemory(&m_pSession->s_SendOverlapped, sizeof(OVERLAPPED));

	ret = WSASend(m_pSession->s_Socket, wsa, m_pSession->s_SendMsgCnt, NULL, flags, &m_pSession->s_SendOverlapped, NULL);
	if (ret == SOCKET_ERROR)
	{
		err = WSAGetLastError();

		if (err == ERROR_IO_PENDING)
		{
			return true;
		}

		else if (err != ERROR_IO_PENDING)
		{
			Disconnect();

			//비정상적인 에러시 로그 남기기
			if (err != WSAECONNRESET && err != WSAECONNABORTED && err != WSAEINTR)
			{
				LOG(L"CLanClient", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"SendPost WSASend Return Failed / Error Code : %d", err);
			}

			Release(InterlockedDecrement(&m_pSession->s_IORefCnt));

			return false;
		}

	}

	//동기 정상 등록한 경우
	else if (ret == 0)
		return true;

	return false;
}

void CLanClient::Release(int refCnt)
{
	CMessage* peek = nullptr;

	if (refCnt < 0)
		__debugbreak();

	else if (refCnt != 0)
		return;
	

	//SendArray에 있는 직렬화 버퍼 반납.(아마 없을 것으로 보임)
	for (int i = 0; i < m_pSession->s_SendMsgCnt; i++)
	{
		CMessage::Free(m_pSession->s_SendArray[i]);
	}

	//Send 락프리 큐에 있는 직렬화 버퍼 반납
	while (1)
	{
		//빼낼게 없으면 탈출
		if (!m_pSession->s_SendQ.Dequeue(peek))
			break;

		CMessage::Free(peek);

	}

	OnLeaveServer();

	closesocket(m_pSession->s_Socket);
	
	InterlockedExchange(&m_pSession->s_RelFlag,1);

	return;
}

void CLanClient::RecvIOProc(DWORD cbTransferred)
{
	INT retPeekHeader = -1;
	INT retPeekPayload = -1;
	BOOL RecvError = false;

	m_pSession->s_RecvQ.MoveWritePos(cbTransferred);

	//수신 링버퍼 빌 때 까지 메세지 추출해서 바로 클라이언트에게 보냄
	while (1)
	{

		CMessage* pPacket = CMessage::Alloc();
		pPacket->Clear();

		//RecvQ에서 완성된 메세지 확인
		LANHEADER header;



		//수신 링버퍼에 len이 네트워크 헤더인데 이정도도 없으면 그냥 끝내기
		unsigned long long usesize = m_pSession->s_RecvQ.GetUseSize();
		if (usesize <= sizeof(LANHEADER))
		{
			CMessage::Free(pPacket);
			break;
		}

		//네트워크 헤더 추출시 체크섬 제외하고 추출
		retPeekHeader = m_pSession->s_RecvQ.Peek((char*)&header, sizeof(LANHEADER));

		if (retPeekHeader == 0)
		{
			__debugbreak();
			break;
		}


		//수신 링버퍼에 남은게 네트워크 헤더 + payload 크기 보다 작으면 그냥 peek만 해서 네트워크 헤더 보고 나가는 것임.
		if (usesize < header.s_len + sizeof(LANHEADER))
		{
			CMessage::Free(pPacket);
			break;
		}

		//네트워크 헤더 뽑은 만큼 옮기기
		m_pSession->s_RecvQ.MoveReadPos(retPeekHeader);

		//페이로드 추출
		retPeekPayload = m_pSession->s_RecvQ.Peek(pPacket->GetWritePos(), header.s_len);
		if (retPeekPayload == 0)
		{
			__debugbreak();
			break;
		}

		pPacket->MoveWritePos(retPeekPayload);

		OnRecv(pPacket);

		CMessage::Free(pPacket);

		//Recv 버퍼 밀기
		m_pSession->s_RecvQ.MoveReadPos(retPeekPayload);

	}

	if (!m_pSession->s_DCFlag)
	{
		//Recv등록
		RecvPost();
	}

	Release(InterlockedDecrement(&m_pSession->s_IORefCnt));

	return ;
}

void CLanClient::SendIOProc(DWORD cbTransferred)
{
	//사용한 직렬화 버퍼 메세지 메모리 풀에 반납
	for (int i = 0; i <m_pSession->s_SendMsgCnt; i++)
	{
		CMessage::Free(m_pSession->s_SendArray[i]);
	}

	m_pSession->s_SendMsgCnt = 0;

	OnSend(cbTransferred);

	InterlockedExchange16(&m_pSession->s_SendFlag, 0);

	SendPost();
	
	Release(InterlockedDecrement(&m_pSession->s_IORefCnt));

	return;
}

void CLanClient::Thread_Create()
{
	for (int i = 0; i < CLIENT_WORKER_COUNT; i++)
	{
		m_IOCPWorkerThread[i] = std::thread(&CLanClient::WorkerThread, this);
	}
}

void CLanClient::Thread_Destroy()
{
	CloseHandle(m_iocp);

	for (int i = 0; i < CLIENT_WORKER_COUNT; i++)
	{
		if (m_IOCPWorkerThread[i].joinable())
		{
			m_IOCPWorkerThread[i].join();
		}
	}
}

void CLanClient::Session_Init(SOCKET socket)
{
	m_pSession->s_Socket = socket;
	m_pSession->s_SendMsgCnt = 0;
	m_pSession->s_IORefCnt = 1;
	m_pSession->s_DCFlag = 0;
	m_pSession->s_SendFlag = 0;
	m_pSession->s_RelFlag = 0;
	m_pSession->s_RecvQ.Clear();
	m_pSession->s_SendQ.Clear();

	ZeroMemory(&m_pSession->s_RecvOverlapped, sizeof(OVERLAPPED));
	ZeroMemory(&m_pSession->s_SendOverlapped, sizeof(OVERLAPPED));
}



