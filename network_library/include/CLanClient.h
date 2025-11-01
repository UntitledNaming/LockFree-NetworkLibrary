#pragma once
#define CLIENT_WSABUFSIZE   50
#define MAX_NUM_THREAD      5
#define CLIENT_WORKER_COUNT 2

class CLanClient
{
public:
	struct st_SESSION
	{
		SOCKET                 s_Socket;
		OVERLAPPED             s_RecvOverlapped;
		OVERLAPPED             s_SendOverlapped;
		CRingBuffer            s_RecvQ;
		LFQueueMul<CMessage*>  s_SendQ;
		CMessage*              s_SendArray[CLIENT_WSABUFSIZE];

		SHORT                  s_SendFlag;
		INT                    s_SendMsgCnt;
		LONG                   s_DCFlag;
		LONG                   s_IORefCnt;
		LONG                   s_RelFlag;
	}typedef SESSION;

public:
	CLanClient();
	virtual ~CLanClient();

	////////////////////////////////////////////////////
	// CLanClient 인터페이스 함수
	////////////////////////////////////////////////////
	virtual void OnEnterJoinServer() = 0;               // 서버와 연결 성공 후       
	virtual void OnLeaveServer() = 0;                   // 서버와 연결 끊어진 후
	virtual void OnRecv(CMessage* pMessage) = 0;        // 패킷 수신 완료 후
	virtual void OnSend(int sendsize) = 0;              // 패킷 송신 완료 후


	////////////////////////////////////////////////////
	// 외부 제공 함수
	////////////////////////////////////////////////////
	bool  Connect(WCHAR* SERVERIP, INT SERVERPORT);
	bool  Disconnect();
	bool  SendPacket(CMessage* pMessage);
	bool  ReConnect();                                  // ConnectAlive 함수를 통해 연결 끊긴게 확인될 때 호출하는 함수(Not Thread Safe Func)
	bool  ConnectAlive();
	void  Destroy();

private:
	void  WorkerThread();


private:
	bool  Mem_Init(WCHAR* ServerIP, INT ServerPort);
	bool  CreateAndSetSocket(SOCKET& outParam);                                                           // true : 소켓 생성 및 옵션 설정 성공, false : 소켓 생성 또는 옵션 설정 실패
	bool  ConnectTry(SOCKET inputParam);                                                                  // true : 서버와 연결 성공 , false : 연결 실패
	bool  RecvPost();              
	bool  SendPost();              
	void  Release(int refCnt);
	void  RecvIOProc(DWORD cbTransferred);
	void  SendIOProc(DWORD cbTransferred);
	void  Thread_Create();
	void  Thread_Destroy();
	void  Session_Init(SOCKET socket);


private:
	HANDLE                    m_iocp;
	INT                       m_serverport;
	SESSION*                  m_pSession;
	std::thread               m_IOCPWorkerThread[MAX_NUM_THREAD];               
	std::wstring              m_serverIP;
	
};