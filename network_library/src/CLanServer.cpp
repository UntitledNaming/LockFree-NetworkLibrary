#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <string>
#include <thread>
#include <timeapi.h>
#include <unordered_map>

#include "LibraryHeader.h"
#include "MemoryPoolTLS.h"
#include "CMessage.h"
#include "LogClass.h"
#include "CMessage.h"
#include "LFStack.h"
#include "LFQMultiLive.h"
#include "Ring_Buffer.h"
#include "CSession.h"
#include "CLanServer.h"
#pragma comment(lib, "Ws2_32.lib")
#pragma warning(disable:4996)


CLanServer::CLanServer()
{
}

CLanServer::~CLanServer()
{
}

bool CLanServer::Start(WCHAR* SERVERIP, int SERVERPORT, int numberOfCreateThread, int numberOfRunningThread, int maxNumOfSession, int SendSleep, int SendTHFL, bool OffNagle)
{
	// Config ���Ͽ��� ���� ���� ��Ʈ��ũ ���̺귯�� ��� ����
	m_IP = *SERVERIP;
	m_Port = SERVERPORT;
	m_MaxSessionCnt = maxNumOfSession;
	m_CreateWorkerCnt = numberOfCreateThread;
	m_ConcurrentCnt = numberOfRunningThread;
	m_SendFrame = SendSleep;
	m_SendThFL = SendTHFL;


	Mem_Init();

	Thread_Create();

	Net_Init(SERVERIP, SERVERPORT, OffNagle);

	return true;
}

bool CLanServer::Disconnect(UINT64 SessionID)
{
	CSession* pSession = nullptr;

	FindSession(SessionID, &pSession);

	if (pSession == nullptr)
		return false;

	// ���� ã������ ��ȿ�� Ȯ���ؾ� ��.
	if (!SessionInvalid(pSession, SessionID))
		return false;


	if (InterlockedExchange(&pSession->m_DCFlag, 1) != 0)
	{
		Release(pSession, pSession->m_SessionID, InterlockedDecrement64(&pSession->m_RefCnt));
		return false;
	}

	CancelIoEx((HANDLE)pSession->m_Socket, &pSession->m_RecvOverlapped);
	CancelIoEx((HANDLE)pSession->m_Socket, &pSession->m_SendOverlapped);

	Release(pSession, pSession->m_SessionID, InterlockedDecrement64(&pSession->m_RefCnt));

	return true;
}

bool CLanServer::SendPacket(UINT64 SessionID, CMessage* pMessage)
{
	CSession* pSession;
	DWORD curThreadID = GetCurrentThreadId();

	FindSession(SessionID, &pSession);

	//��ã�� ����
	if (pSession == nullptr)
		return false;

	// ���� ã������ ��ȿ�� Ȯ���ؾ� ��.
	if (!SessionInvalid(pSession, SessionID))
		return false;

	if (pSession->m_SendQ.GetUseSize() >= SENDQ_MAX_SIZE)
	{
		Disconnect(pSession->m_SessionID);
		LOG(L"CNetLibrary", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"SendPacket SendQ Full \ SessionID : %lld , Time : %d ", pSession->m_SessionID, timeGetTime());
		Release(pSession, pSession->m_SessionID, InterlockedDecrement64(&pSession->m_RefCnt));

		return false;
	}

	LANHEADER header;
	header.s_len = pMessage->GetDataSize();

	memcpy_s(pMessage->GetAllocPos(), sizeof(header), (char*)&header, sizeof(header));
	pMessage->AddRef();

	pSession->m_SendQ.Enqueue(pMessage);

	if (m_SendThFL == 0)
	{
		SendPost(pSession);
	}

	Release(pSession, pSession->m_SessionID, InterlockedDecrement64(&pSession->m_RefCnt));

	return true;
}

bool CLanServer::FindIP(UINT64 SessionID, WCHAR* OutIP)
{
	SOCKADDR_IN ClientAddr;
	INT         len;
	INT64       ret;
	CSession* pSession;

	FindSession(SessionID, &pSession);
	if (pSession == nullptr)
		return false;

	// ���� ã������ ��ȿ�� Ȯ���ؾ� ��.
	if (!SessionInvalid(pSession, SessionID))
		return false;

	len = sizeof(ClientAddr);
	getpeername(pSession->m_Socket, (sockaddr*)&ClientAddr, &len);

	InetNtop(AF_INET, &ClientAddr.sin_addr, (PWSTR)OutIP, IP_LEN);


	Release(pSession, pSession->m_SessionID, InterlockedDecrement64(&pSession->m_RefCnt));

	return true;
}

void CLanServer::Stop()
{
	closesocket(m_Listen);
	LOG(L"CLanLibrary", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L"CLanServer::Stop()_closesocekt(g_ListenSocket) Complete...");

	// ��ü ���� Release �۾�
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

	// IOCP ������ ��Ŀ ������� �ڵ����� while�� Ż����
	CloseHandle(m_IOCP);
	LOG(L"CLanLibrary", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L"CLanServer::Stop()_CloseHandle(IOCP) Complete...");


	Thread_Destroy();


	// ��� ��ü ����
	delete[] m_SessionTable;
	delete m_pSessionIdxStack;
}

void CLanServer::WorkerThread()
{
	wprintf(L"WorkerThread Start.. %d \n", GetCurrentThreadId());
	LOG(L"CLanLibrary", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L"WorkerThread  Start... : %d ", GetCurrentThreadId());


	timeBeginPeriod(1);

	BOOL  retval;
	DWORD err;
	LONGLONG retrel;

	bool ESC = false;

	int log = 0;
	int loop = 0;

	char* temprpos = nullptr;

	while (!ESC)
	{
		DWORD       cbTransferred = -1;
		OVERLAPPED* pOverlapped = nullptr;     // IO�� ���� Overlapped ����ü �ּҰ� or Task Type
		CSession* pSession = nullptr;
		INT         sum = 0;                   //üũ�� ���

		retval = GetQueuedCompletionStatus(m_IOCP, &cbTransferred, (PULONG_PTR)&pSession, (LPOVERLAPPED*)&pOverlapped, INFINITE);


		//false �� ��Ȳ(IOCP �Ϸ� ���� ť���� ��ť���� �ȵ� ���, IOCP �ڵ��� CloseHandle�� ���� ���� ���)
		if (retval == false)
		{
			//IOCP�� �����ų� IOCP �Ϸ� ���� ť���� ��ť�׿� ������ �� ó��
			if (pOverlapped == nullptr)
			{
				err = GetLastError();
				LOG(L"CLanLibrary", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L"WorkerThread GQCS IOCP Failed \ Error Code : %d", err);

				ESC = true;
				break;
			}

			//WSASend, WSARecv�� ��û�� IO�� ������ ���(TCP�� ���� ����)
			else
			{
				err = GetLastError();
				LOG(L"CLanLibrary", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"WorkerThread GQCS IO Failed \ Error Code : %d", err);
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


	LOG(L"CLanLibrary", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L"WorkerThread  End... : %d ", GetCurrentThreadId());
	wprintf(L"WorkerThread End... %d \n", GetCurrentThreadId());
	return;
}

void CLanServer::AcceptThread()
{
	DWORD curThreadID = GetCurrentThreadId();
	LOG(L"CLanLibrary", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L"AcceptThread Start : %d ", GetCurrentThreadId());
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
			LOG(L"CLanLibrary", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L"AcceptThread accept() Failed \ Error Code : %d", WSAGetLastError());
			ESC = true;
			continue;
		}


		InterlockedIncrement(&m_AcceptTPS);


		//���� ������ ������ �ִ� ���ǰ� �̻����� ������ ������ ����.
		if (InterlockedIncrement16(&m_CurSessionCnt) > m_MaxSessionCnt)
		{
			closesocket(client_socket);
			InterlockedDecrement16(&m_CurSessionCnt);
			continue;
		}

		WCHAR szClientIP[IP_LEN] = { 0 };
		InetNtopW(AF_INET, &clientAddr.sin_addr, szClientIP, IP_LEN);
		OnConnectionRequest(szClientIP, clientAddr.sin_port);

		//Overlapped IO�� �۵���Ű�� ���� ���� �۽� ���� 0���� ����
		int sendBufferSize = 0;
		setsockopt(client_socket, SOL_SOCKET, SO_SNDBUF, (char*)&sendBufferSize, sizeof(sendBufferSize));

		linger so_linger;
		so_linger.l_onoff = 1;  // linger �ɼ� ���
		so_linger.l_linger = 0; // ���� �ð� 0 -> ��� RST

		if (setsockopt(client_socket, SOL_SOCKET, SO_LINGER, (char*)&so_linger, sizeof(so_linger)) == SOCKET_ERROR)
		{
			LOG(L"CNetLibrary", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"CNetLibrary::Linger Option Set Error :%d ", WSAGetLastError());
		}


		//���� �迭�� �ֱ� ���� �ϴ� Index ã��
		UINT16 Index;
		m_pSessionIdxStack->Pop(Index);

		// ���� ID �����
		UINT64 sessionID = MakeSessionID(Index, m_AllocID);

		// ���� �ʱ�ȭ(�׷� ID 0������ �ʱ�ȭ)
		m_SessionTable[Index].Init(client_socket, sessionID);


		m_AllocID++;
		m_AcceptTotal++;


		//Ŭ���̾�Ʈ ���� IOCP�� ��� �� key������ ���� �����͸� ����
		HANDLE retCIOCP;
		retCIOCP = CreateIoCompletionPort((HANDLE)client_socket, m_IOCP, (ULONG_PTR)&m_SessionTable[Index], 0);
		if (retCIOCP == NULL)
		{
			LOG(L"CLanLibrary", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"AcceptThread RegisterSocketHANDLE Failed \ Session ID : %lld \ Error Code : %d ", m_SessionTable[Index].m_SessionID, GetLastError());
			break;
		}


		// ������ ���� �׷��� OnClientJoin ȣ��
		OnClientJoin(sessionID);

		//Recv ���
		RecvPost(&m_SessionTable[Index]);

		Release(&m_SessionTable[Index], m_SessionTable[Index].m_SessionID, InterlockedDecrement64(&m_SessionTable[Index].m_RefCnt));
	}


	LOG(L"CLanLibrary", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L"AcceptThread End : %d ", GetCurrentThreadId());
}

void CLanServer::SendThread()
{
	LOG(L"CLanLibrary", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L"SendThread Start : %d ", GetCurrentThreadId());

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
				continue;

			// ���� Ȯ��
			if (!SessionInvalid(&m_SessionTable[i], m_SessionTable[i].m_SessionID))
				continue;

			// Send �÷��� �� SendQ üũ �� ������ �ȵǴ� ��Ȳ���� ���� ����
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
			LOG(L"CLanLibrary", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L"SendThread 1Frame Send Proc Time : %d ", endtime - starttime);
		}

		// ��ü ���ǿ� ���ؼ� Send �۾� ������ Sleep���� ���۸�
		Sleep(m_SendFrame);
	}

	LOG(L"CLanLibrary", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L"SendThread End : %d ", GetCurrentThreadId());
}

void CLanServer::Net_Init(WCHAR* SERVERIP, int SERVERPORT, bool OffNagle)
{
	int ret;
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
	{
		wprintf(L"WSAStartUp Failed....! \n");
		LOG(L"CLanLibrary", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"CLanLibrary::Net_Init WSAStartup Error : %d ", WSAGetLastError());
		__debugbreak();
	}

	// ���� ���� ����
	m_Listen = socket(AF_INET, SOCK_STREAM, 0);
	if (m_Listen == INVALID_SOCKET)
	{
		wprintf(L"socket() error \n");
		LOG(L"CLanLibrary", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"CLanLibrary::Net_Init socket() Error : %d ", WSAGetLastError());
		__debugbreak();
	}
	wprintf(L"socket() Complete... \n");

	//���̱� On/Off
	if (OffNagle == true)
	{
		//���̱� ���� ���� ��
		int flag = 1;
		if (setsockopt(m_Listen, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(int)) == -1)
		{
			LOG(L"CLanLibrary", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"CLanLibrary::Start()_Nagle Error :%d ", WSAGetLastError());
			__debugbreak();
		}
		LOG(L"CLanLibrary", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L"CLanLibrary::Start()_Nagle On Complete...");
	}


	//bind() ó��
	SOCKADDR_IN serveraddr;
	ZeroMemory(&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port = htons(SERVERPORT);
	InetPtonW(AF_INET, SERVERIP, &serveraddr.sin_addr);

	ret = bind(m_Listen, (SOCKADDR*)&serveraddr, sizeof(serveraddr));
	if (ret == SOCKET_ERROR)
	{
		LOG(L"CLanLibrary", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"CLanLibrary::Net_Init bind() Error... \ Error Code :", WSAGetLastError());
		__debugbreak();
	}


	ret = listen(m_Listen, SOMAXCONN);
	if (ret == SOCKET_ERROR)
	{
		LOG(L"CLanLibrary", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"CLanLibrary::Net_Init listen() Error... \ Error Code :", WSAGetLastError());
		__debugbreak();
	}

	wprintf(L"listen() Complete... \n");
	LOG(L"CLanLibrary", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L"CLanLibrary::Net_Init listen() Complete... ");
}

void CLanServer::Mem_Init()
{
	m_SessionTable = new CSession[m_MaxSessionCnt];
	m_pSessionIdxStack = new LFStack<UINT16>;
	m_AllocID = 0;
	m_AcceptTPS = 0;
	m_RecvIOTPS = 0;
	m_SendIOTPS = 0;
	m_AcceptTotal = 0;
	m_CurSessionCnt = 0;


	m_IOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, m_ConcurrentCnt); // ���� ������ ������ ��Ŀ��Ʈ ������ ����
	if (m_IOCP == NULL)
	{
		wprintf(L"CreateIoCompletionPort Failed...\n");
		LOG(L"CLanLibrary", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"CreateIoCompletionPort Failed...");

		return;
	}
	wprintf(L"Create IOCP Resoure Success! \n");
	LOG(L"CLanLibrary", en_LOG_LEVEL::dfLOG_LEVEL_SYSTEM, L"CreateIoCompletionPort Complete...");

	for (int i = 0; i < m_MaxSessionCnt; i++)
	{
		m_pSessionIdxStack->Push(i);
		m_SessionTable[i].m_SessionID = df_INVALID_SESSIONID;
		m_SessionTable[i].m_Socket = INVALID_SOCKET;
	}

}

void CLanServer::Thread_Create()
{
	for (int i = 0; i < m_CreateWorkerCnt; i++)
	{
		m_IOCPWorkerThread[i] = std::thread(&CLanServer::WorkerThread, this);
	}

	m_AcceptThread = std::thread(&CLanServer::AcceptThread, this);
	if (m_SendThFL == 1)
	{
		m_SendThread = std::thread(&CLanServer::SendThread, this);
	}
}

void CLanServer::Thread_Destroy()
{
	//Accept ������ ���� Ȯ��
	if (m_AcceptThread.joinable())
	{
		m_AcceptThread.join();
	}

	//������ ��Ŀ ������ ���� Ȯ��
	for (int i = 0; i < m_CreateWorkerCnt; i++)
	{
		if (m_IOCPWorkerThread[i].joinable())
		{
			m_IOCPWorkerThread[i].join();
		}
	}

	// send �÷��� ���� ������ 
	if (m_SendThFL == 1)
	{
		m_SendThFL = 0;

		if (m_SendThread.joinable())
		{
			m_SendThread.join();
		}
	}
}

void CLanServer::FindSession(UINT64 SessionID, CSession** ppSession)
{
	UINT64 index;

	*ppSession = nullptr;

	if (SessionID == df_INVALID_SESSIONID)
		return;

	// ���ڷι��� ���� ID���� Index ����
	index = SessionID >> INDEX_POS;

	*ppSession = &m_SessionTable[index];
}

UINT64 CLanServer::MakeSessionID(UINT16 index, UINT64 allocID)
{
	UINT64     idx;
	idx = index;
	idx = idx << INDEX_POS;

	return (UINT64)(idx | allocID);
}

bool CLanServer::RecvPost(CSession* pSession)
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

	InterlockedIncrement64(&pSession->m_RefCnt);

	ZeroMemory(&pSession->m_RecvOverlapped, sizeof(OVERLAPPED));

	ret = WSARecv(pSession->m_Socket, wsa, 2, &bytesReceived, &flags, &pSession->m_RecvOverlapped, NULL);
	if (ret == SOCKET_ERROR)
	{
		err = WSAGetLastError();

		//�񵿱� ���������
		if (err == ERROR_IO_PENDING)
			return true;

		else if (err != ERROR_IO_PENDING)
		{

			Disconnect(pSession->m_SessionID);

			//���������� ������ �α� �����
			if (err != WSAECONNRESET && err != WSAECONNABORTED && err != WSAEINTR)
			{
				LOG(L"CLanLibrary", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"SendPost WSASend Return Failed \ Error Code : %d \ SessionID  : %d ", err, pSession->m_SessionID);
			}


			Release(pSession, pSession->m_SessionID, InterlockedDecrement64(&pSession->m_RefCnt));

		}

	}

	//wsarecv ���ϰ��� 0�ΰ� wsarecv�� ���������� ��ϵǾ��ų� ���������� �Ϸ�Ǿ��ٴ� ����.
	else if (ret == 0)
		return true;

	return false;
}

bool CLanServer::SendPost(CSession* pSession)
{
	int ret;
	int err;
	int retrel;
	int usesize;
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

			usesize = pSession->m_SendQ.GetUseSize();
			if (usesize == 0)
			{
				InterlockedExchange16(&pSession->m_SendFlag, 0);

				//size �ѹ��� üũ
				usesize = pSession->m_SendQ.GetUseSize();
				if (usesize == 0)
				{
					return false;
				}

				//2��° size�� 0�̾ƴϸ� ���� �־����� �ٽ� send flag ȹ�� �õ�
				continue;
			}

			//ù��° size üũ�� 0�ƴϸ� Send�۾�
			break;
		}
	}
	else
	{
		pSession->m_SendFlag = 1;
	}

	WSABUF wsa[WSABUFSIZE];

	//�۽� ������ť���� ������ ������(������ false ����)
	CMessage* temp;
	int index;
	for (index = 0; index < WSABUFSIZE; index++)
	{
		if (!pSession->m_SendQ.Dequeue(pSession->m_SendArray[index]))
			break;
	}


	//Dequeue �����ؼ� SendArray�� ������ ���� ����
	pSession->m_SendMsgCnt = index;

	//SendArray�� �ִ°��� wsaBuf�� ����
	for (int i = 0; i < index; i++)
	{
		wsa[i].buf = pSession->m_SendArray[i]->GetAllocPos();
		wsa[i].len = pSession->m_SendArray[i]->GetRealDataSize(1);
	}


	InterlockedIncrement64(&pSession->m_RefCnt);
	ZeroMemory(&pSession->m_SendOverlapped, sizeof(OVERLAPPED));

	ret = WSASend(pSession->m_Socket, wsa, pSession->m_SendMsgCnt, NULL, flags, &pSession->m_SendOverlapped, NULL);
	if (ret == SOCKET_ERROR)
	{
		err = WSAGetLastError();

		if (err == ERROR_IO_PENDING)
		{
			LOG(L"CLanLibrary", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"SendPost_IO_PENDING \ Session ID : %llu ", pSession->m_SessionID);
			return true;
		}

		else if (err != ERROR_IO_PENDING)
		{
			Disconnect(pSession->m_SessionID);

			//���������� ������ �α� �����
			if (err != WSAECONNRESET && err != WSAECONNABORTED && err != WSAEINTR)
			{
				LOG(L"CLanLibrary", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"SendPost WSASend Return Failed \ Error Code : %d \ SessionID  : %llu ", err, pSession->m_SessionID);
			}


			Release(pSession, pSession->m_SessionID, InterlockedDecrement64(&pSession->m_RefCnt));

			return false;
		}

	}

	else if (ret == 0)
	{
		LOG(L"CLanLibrary", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"SendPost WSARecv Return 0  \ Session ID : %d", pSession->m_SessionID);
		return true;
	}

	return false;

}

bool CLanServer::SessionInvalid(CSession* pSession, UINT64 SessionID)
{
	InterlockedIncrement64(&pSession->m_RefCnt);

	//�׷��� �̹� ���� Release �ϰ� ������ ���� �ȵǴ� ���� ��Ű�� Release 
	if (pSession->m_RelFlag == 1)
	{
		Release(pSession, pSession->m_SessionID, InterlockedDecrement64((long long*)&pSession->m_RefCnt));
		return false;
	}

	//���� ã�� ������ �ƴ϶�� ������Ų�� ����
	if (SessionID != pSession->m_SessionID)
	{
		Release(pSession, pSession->m_SessionID, InterlockedDecrement64((long long*)&pSession->m_RefCnt));
		return false;
	}

	return true;
}

bool CLanServer::Release(CSession* pSession, UINT64 CheckID, long retIOCount)
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

void CLanServer::RecvIOProc(CSession* pSession, DWORD cbTransferred)
{
	INT retPeekHeader = 0;
	INT retPeekPayload = 0;
	BOOL RecvError = false;
	LONGLONG retrel;

	if (cbTransferred == 0)
		pSession->m_DCFlag = 1;

	pSession->m_RecvQ.MoveWritePos(cbTransferred);

	//���� ������ �� �� ���� �޼��� �����ؼ� �ٷ� Ŭ���̾�Ʈ���� ����
	while (1)
	{
		if (pSession->m_DCFlag == 1)
			break;

		CMessage* pPacket = CMessage::Alloc();
		pPacket->Clear();

		//RecvQ���� �ϼ��� �޼��� Ȯ��
		LANHEADER header;

		//���� �����ۿ� len�� ��Ʈ��ũ ����ε� �������� ������ �׳� ������
		int usesize = pSession->m_RecvQ.GetUseSize();
		if (usesize <= sizeof(LANHEADER))
		{
			CMessage::Free(pPacket);
			break;
		}

		//��Ʈ��ũ ��� ����� üũ�� �����ϰ� ����
		retPeekHeader = pSession->m_RecvQ.Peek((char*)&header, sizeof(LANHEADER));

		if (retPeekHeader == 0)
		{
			//peek �غôµ� 0�� ���°��� �������� �ϴ� ������ ũ��(���ũ��) ���� ���� �����۰� ������� �����Ͱ� �� ���� ��Ȳ��
			LOG(L"CLanLibrary", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"WorkerThread RecQ.Peek RecvRingBuffer UseSize Not Enough Error / Session ID : %lld ", pSession->m_SessionID);
			Disconnect(pSession->m_SessionID);
			CMessage::Free(pPacket);
			break;
		}


		//���� �����ۿ� ������ ��Ʈ��ũ ��� + payload ũ�� ���� ������ �׳� peek�� �ؼ� ��Ʈ��ũ ��� ���� ������ ����.
		if (usesize < header.s_len + sizeof(LANHEADER))
		{
			CMessage::Free(pPacket);
			break;
		}


		//��Ʈ��ũ ��� ���� ��ŭ �ű��
		pSession->m_RecvQ.MoveReadPos(retPeekHeader);


		// ���� �����ۿ��� üũ�� �� ���̷ε� ���� �� ����ȭ ���ۿ� ����
		retPeekPayload = pSession->m_RecvQ.Peek(pPacket->GetWritePos(), header.s_len);
		if (retPeekPayload == 0)
		{
			LOG(L"CLanLibrary", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"WorkerThread RecQ.Peek PayLoad Error / Session ID : %lld", pSession->m_SessionID);
			Disconnect(pSession->m_SessionID);
			CMessage::Free(pPacket);
			break;
		}

		pPacket->MoveWritePos(retPeekPayload);

		OnRecv(pSession->m_SessionID, pPacket);

		CMessage::Free(pPacket);

		//Recv ���� �б�
		pSession->m_RecvQ.MoveReadPos(retPeekPayload);

	}

	InterlockedIncrement(&m_RecvIOTPS);

	// Disconnect �ƴ� �� RecvIO ���
	if (pSession->m_DCFlag != 1)
	{
		RecvPost(pSession);
	}

	Release(pSession, pSession->m_SessionID, InterlockedDecrement64(&pSession->m_RefCnt));

	return;
}

void CLanServer::SendIOProc(CSession* pSession, DWORD cbTransferred)
{
	LONGLONG retrel;

	//����� ����ȭ ���� �޼��� �޸� Ǯ�� �ݳ�
	for (int i = 0; i < pSession->m_SendMsgCnt; i++)
	{
		CMessage::Free(pSession->m_SendArray[i]);
	}

	//SendMessage �Ϸ� ������ ����
	InterlockedIncrement(&m_SendIOTPS);

	pSession->m_SendMsgCnt = 0;
	InterlockedExchange16(&pSession->m_SendFlag, 0);

	if (m_SendThFL == 0)
	{
		SendPost(pSession);
	}


	Release(pSession, pSession->m_SessionID, InterlockedDecrement64(&pSession->m_RefCnt));
}

void CLanServer::ReleaseProc(CSession* pSession)
{
	CMessage* peek = nullptr;

	/*
	* ���� Release �۾�
	*/

	//SendArray�� �ִ� ����ȭ ���� �ݳ�.(SendPost ���ο��� Release�� �� ����)
	for (int i = 0; i < pSession->m_SendMsgCnt; i++)
	{
		CMessage::Free(pSession->m_SendArray[i]);
	}

	//Send ������ ť�� �ִ� ����ȭ ���� �ݳ�(SendPacket �Լ����� Release �� �� ����)
	while (1)
	{
		//������ ������ Ż��
		if (!pSession->m_SendQ.Dequeue(peek))
			break;

		CMessage::Free(peek);

	}

	OnClientLeave(pSession->m_SessionID);


	//�� �÷��׸� �ٲ� �����尡 EmptyIndexStack�� index�� push �ؾ� ��.
	//index ����
	uint16_t index;

	index = pSession->m_SessionID >> INDEX_POS;
	closesocket(pSession->m_Socket);


	m_pSessionIdxStack->Push(index);
	InterlockedDecrement16(&m_CurSessionCnt);
}


