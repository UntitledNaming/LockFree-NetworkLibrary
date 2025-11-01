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
	// CLanClient �������̽� �Լ�
	////////////////////////////////////////////////////
	virtual void OnEnterJoinServer() = 0;               // ������ ���� ���� ��       
	virtual void OnLeaveServer() = 0;                   // ������ ���� ������ ��
	virtual void OnRecv(CMessage* pMessage) = 0;        // ��Ŷ ���� �Ϸ� ��
	virtual void OnSend(int sendsize) = 0;              // ��Ŷ �۽� �Ϸ� ��


	////////////////////////////////////////////////////
	// �ܺ� ���� �Լ�
	////////////////////////////////////////////////////
	bool  Connect(WCHAR* SERVERIP, INT SERVERPORT);
	bool  Disconnect();
	bool  SendPacket(CMessage* pMessage);
	bool  ReConnect();                                  // ConnectAlive �Լ��� ���� ���� ����� Ȯ�ε� �� ȣ���ϴ� �Լ�(Not Thread Safe Func)
	bool  ConnectAlive();
	void  Destroy();

private:
	void  WorkerThread();


private:
	bool  Mem_Init(WCHAR* ServerIP, INT ServerPort);
	bool  CreateAndSetSocket(SOCKET& outParam);                                                           // true : ���� ���� �� �ɼ� ���� ����, false : ���� ���� �Ǵ� �ɼ� ���� ����
	bool  ConnectTry(SOCKET inputParam);                                                                  // true : ������ ���� ���� , false : ���� ����
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