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

	//���� �ʱ�ȭ
	WSADATA  wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
	{
		__debugbreak();
	}

	// ��� �ʱ�ȭ
	if (!Mem_Init(SERVERIP, SERVERPORT))
		return false;

	// ������ ����
	Thread_Create();

	// ���� ���� �� �ɼ� ����
	if (!CreateAndSetSocket(sock))
	{

		if (sock != INVALID_SOCKET)
			closesocket(sock);

		return false;
	}


	// connect �õ�
	if (!ConnectTry(sock))
	{
		closesocket(sock);
		return false;
	}

	Session_Init(sock);

	OnEnterJoinServer();

	// Recv IO ��� �۾�
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

	// ���� Release �� �� ���� ReConnect ó�� �Ұ�
	if (m_pSession->s_RelFlag != 1)
		return false;


	// ���� ���� �Ǿ����� �ٽ� ���� ���� �� ���� �ʱ�ȭ �۾�
	if (!CreateAndSetSocket(sock))
	{
		if (sock != INVALID_SOCKET)
			closesocket(sock);

		return false;
	}

	// connect �õ�
	if (!ConnectTry(sock))
	{
		closesocket(sock);
		return false;
	}

	// ���� �ʱ�ȭ
	Session_Init(sock);

	// Recv IO ��� �۾�
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

	// ���� �����Ҷ� ���� ����
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
		OVERLAPPED* pOverlapped = nullptr;//�Ϸ��� wsaoverlapped ����ü �ּҰ�
		LONGLONG    Key;
		retval = GetQueuedCompletionStatus(m_iocp, &cbTransferred, (PULONG_PTR)&Key, (LPOVERLAPPED*)&pOverlapped, INFINITE);


		//false �� ��Ȳ(IOCP �Ϸ� ���� ť���� ��ť���� �ȵ� ���, IOCP �ڵ��� CloseHandle�� ���� ���� ���)
		if (retval == false)
		{
			//IOCP�� �����ų� IOCP �Ϸ� ���� ť���� ��ť�׿� ������ �� ó��
			if (pOverlapped == nullptr)
			{
				LOG(L"CLanClient", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"WorkerThread GQCS IOCP Failed  / Error Code : %d", GetLastError());

				//������ �� ��쿡�� �׳� ��Ŀ �����带 �ı��ؾ� ��.
				break;
			}

			//WSASend, WSARecv�� ��û�� IO�� ������ ���(TCP�� ���� ����)
			else
			{
				//������ ��Ŀ ������ �ϴܿ��� �˾Ƽ� Release�� ����.
				LOG(L"CLanClient", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"WorkerThread GQCS IO Failed / Error Code : %d", GetLastError());

			}

		}

		
		//Recv �Ϸ��� ���
		if (pOverlapped  == &m_pSession->s_RecvOverlapped )
		{
			RecvIOProc(cbTransferred);
		}

		//Send �Ϸ��� ���
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

	// IOCP ��ü ���� �� �ʱ�ȭ
	m_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, CLIENT_WORKER_COUNT); // ���� ������ ������ ��Ŀ��Ʈ ������ ����
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

	//socket ����
	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == INVALID_SOCKET)
	{
		LOG(L"CLanClient", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"CLanClient::socket() ErrorCode : %d ...", WSAGetLastError());
		return false;
	}

	// Overlapped IO�� �۵���Ű�� ���� ���� �۽� ���� 0���� ����
	int sendBufferSize = 0;
	setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char*)&sendBufferSize, sizeof(sendBufferSize));

	// RST ������ ���� linger �ɼ� ����
	linger so_linger;
	so_linger.l_onoff = 1;  // linger �ɼ� ���
	so_linger.l_linger = 0; // ���� �ð� 0 -> ��� RST

	if (setsockopt(sock, SOL_SOCKET, SO_LINGER, (char*)&so_linger, sizeof(so_linger)) == SOCKET_ERROR)
	{
		LOG(L"CNetLibrary", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"CNetLibrary::Linger Option Set Error :%d ", WSAGetLastError());
		return false;
	}


	// IOCP�� ���� ���
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

	//������ connect ���� ���� ����ü�� ���� IP, PORT �����ؼ� connect �Լ��� �ѱ��
	SOCKADDR_IN clientaddr;
	ZeroMemory(&clientaddr, sizeof(clientaddr));
	clientaddr.sin_family = AF_INET;
	clientaddr.sin_port = htons(m_serverport);
	InetPtonW(clientaddr.sin_family, m_serverIP.c_str(), &clientaddr.sin_addr); //���ڿ� IP�� ������IP�� �ٲٴ� �Լ�

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

		//�񵿱� ���������
		if (err == ERROR_IO_PENDING)
		{
			return true;
		}

		else if (err != ERROR_IO_PENDING)
		{

			Disconnect();

			//���������� ������ �α� �����
			if (err != WSAECONNRESET && err != WSAECONNABORTED && err != WSAEINTR)
			{
				LOG(L"CLanClient", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"SendPost WSASend Return Failed / Error Code : %d", err);
			}

			Release(InterlockedDecrement(&m_pSession->s_IORefCnt));
		}

	}

	//wsarecv ���ϰ��� 0�ΰ� wsarecv�� ���������� ��ϵǾ��ų� ���������� �Ϸ�Ǿ��ٴ� ����.
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

	//���࿡ send flag�� �ٲٴµ� ���� send flag ���°� 1�̶�� io ��� �̹� ������ �ٷ� ������
	while (1)
	{
		if (InterlockedExchange16(&m_pSession->s_SendFlag, 1) == 1)
		{
			return false;
		}

		if (m_pSession->s_SendQ.GetUseSize() == 0)
		{
			InterlockedExchange16(&m_pSession->s_SendFlag, 0);

			//size �ѹ��� üũ
			if (m_pSession->s_SendQ.GetUseSize() == 0)
			{
				return false;
			}

			//2��° size�� 0�̾ƴϸ� ���� �־����� �ٽ� send flag ȹ�� �õ�
			continue;
		}

		//ù��° size üũ�� 0�ƴϸ� Send�۾�
		break;
	}


	WSABUF wsa[CLIENT_WSABUFSIZE];

	//�۽� ������ť���� ������ ������(������ false ����)
	int index;
	for (index = 0; index < CLIENT_WSABUFSIZE; index++)
	{
		if (!m_pSession->s_SendQ.Dequeue(m_pSession->s_SendArray[index]))
			break;
	}

	//Dequeue �����ؼ� SendArray�� ������ ���� ����
	m_pSession->s_SendMsgCnt = index;

	//SendArray�� �ִ°��� wsaBuf�� ����
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

			//���������� ������ �α� �����
			if (err != WSAECONNRESET && err != WSAECONNABORTED && err != WSAEINTR)
			{
				LOG(L"CLanClient", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"SendPost WSASend Return Failed / Error Code : %d", err);
			}

			Release(InterlockedDecrement(&m_pSession->s_IORefCnt));

			return false;
		}

	}

	//���� ���� ����� ���
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
	

	//SendArray�� �ִ� ����ȭ ���� �ݳ�.(�Ƹ� ���� ������ ����)
	for (int i = 0; i < m_pSession->s_SendMsgCnt; i++)
	{
		CMessage::Free(m_pSession->s_SendArray[i]);
	}

	//Send ������ ť�� �ִ� ����ȭ ���� �ݳ�
	while (1)
	{
		//������ ������ Ż��
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

	//���� ������ �� �� ���� �޼��� �����ؼ� �ٷ� Ŭ���̾�Ʈ���� ����
	while (1)
	{

		CMessage* pPacket = CMessage::Alloc();
		pPacket->Clear();

		//RecvQ���� �ϼ��� �޼��� Ȯ��
		LANHEADER header;



		//���� �����ۿ� len�� ��Ʈ��ũ ����ε� �������� ������ �׳� ������
		unsigned long long usesize = m_pSession->s_RecvQ.GetUseSize();
		if (usesize <= sizeof(LANHEADER))
		{
			CMessage::Free(pPacket);
			break;
		}

		//��Ʈ��ũ ��� ����� üũ�� �����ϰ� ����
		retPeekHeader = m_pSession->s_RecvQ.Peek((char*)&header, sizeof(LANHEADER));

		if (retPeekHeader == 0)
		{
			__debugbreak();
			break;
		}


		//���� �����ۿ� ������ ��Ʈ��ũ ��� + payload ũ�� ���� ������ �׳� peek�� �ؼ� ��Ʈ��ũ ��� ���� ������ ����.
		if (usesize < header.s_len + sizeof(LANHEADER))
		{
			CMessage::Free(pPacket);
			break;
		}

		//��Ʈ��ũ ��� ���� ��ŭ �ű��
		m_pSession->s_RecvQ.MoveReadPos(retPeekHeader);

		//���̷ε� ����
		retPeekPayload = m_pSession->s_RecvQ.Peek(pPacket->GetWritePos(), header.s_len);
		if (retPeekPayload == 0)
		{
			__debugbreak();
			break;
		}

		pPacket->MoveWritePos(retPeekPayload);

		OnRecv(pPacket);

		CMessage::Free(pPacket);

		//Recv ���� �б�
		m_pSession->s_RecvQ.MoveReadPos(retPeekPayload);

	}

	if (!m_pSession->s_DCFlag)
	{
		//Recv���
		RecvPost();
	}

	Release(InterlockedDecrement(&m_pSession->s_IORefCnt));

	return ;
}

void CLanClient::SendIOProc(DWORD cbTransferred)
{
	//����� ����ȭ ���� �޼��� �޸� Ǯ�� �ݳ�
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



