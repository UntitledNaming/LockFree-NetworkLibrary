#pragma once

#define MAX_THREAD_COUNT   100
#define IP_LEN             16
#define INDEX_POS          48
#define INVALID_ID         -1
#define SENDQ_MAX_SIZE     10000

class CSession;

class CNetServer
{
private:
	struct st_IOFLAG
	{
		long long Cnt;
		long long Flag;
	};


public:
	enum PQCS_TYPE
	{
		en_RELEASE = 1,
	};


private:
	HANDLE                 m_IOCP;                  // IOCP ���ҽ� �ڵ�
	SOCKET                 m_Listen;                // ���� ���� ����

	///////////////////////////////////////////////////////////////////////////////////////
	// Config ����
	///////////////////////////////////////////////////////////////////////////////////////
	std::string            m_IP;                    // ���� IP
	INT                    m_Port;                  // ���� Port
	INT                    m_MaxSessionCnt;         // �ִ� ���� ���� ����
	INT                    m_CreateWorkerCnt;       // ���� ��Ŀ ������ ����
	INT                    m_ConcurrentCnt;         // IOCP ���� ������ ����
	INT                    m_SendFrame;             // Send ������ Sleep��
	INT                    m_PacketCode;            // ��Ʈ��ũ ���̺귯�� ��Ŷ �ڵ�
	INT                    m_FixedKey;              // ���ڵ� ���� Ű ��
	INT                    m_SendThFL;              // Send ������ ���� ���� �÷��� ( 0 : ���� / 1 : �ѱ� )
	INT                    m_Nagle;                 // ���̱� �ɼ�

public:
	///////////////////////////////////////////////////////////////////////////////////////
	// ����͸� ����
	///////////////////////////////////////////////////////////////////////////////////////
	LONG                   m_AcceptTPS;             // �ʴ� Accept ó�� Ƚ�� ����͸� ��
	LONG                   m_RecvIOTPS;             // �ʴ� Recv ó�� Ƚ�� ����͸� ��
	LONG                   m_SendIOTPS;             // �ʴ� Send ó�� Ƚ�� ����͸� ��
	INT64                  m_AcceptTotal;           // Acceptó���� �ִ� ���� ����
	SHORT                  m_CurSessionCnt;         // ���� �迭���� ������ ������� ���� ����


private:
	///////////////////////////////////////////////////////////////////////////////////////
	// ������ ���� �� �ڷᱸ��
	///////////////////////////////////////////////////////////////////////////////////////
	std::thread            m_IOCPWorkerThread[MAX_THREAD_COUNT]; // Worker ���� �迭
	std::thread            m_AcceptThread;                       // AcceptThread ����
	std::thread            m_SendThread;                         // SendThread


	///////////////////////////////////////////////////////////////////////////////////////
	// ���� ���� �ڷᱸ�� �� ��� ����
	///////////////////////////////////////////////////////////////////////////////////////
	CSession*              m_SessionTable;      	             // ���� ����ü�� ������ �迭
	LFStack<UINT16>*       m_pSessionIdxStack;                   // ���� �迭�� index ������ �ڷᱸ��
	UINT64                 m_AllocID;                            // ���ǿ��� �Ҵ��� ���� ID

public:
	         CNetServer();
	virtual ~CNetServer();

public:
	// �ܺ� ���� �Լ� //

	bool          Start(WCHAR* SERVERIP, int SERVERPORT, int numberOfCreateThread, int numberOfRunningThread, int maxNumOfSession, int SendSleep, int SendTHFL , WORD packetCode, WORD fixedkey, bool OffNagle);
	bool          Disconnect(UINT64 SessionID);
	bool          SendPacket(UINT64 SessionID, CMessage* pMessage);
	bool          SendPacketAll(CMessage* pMessage);
	bool          FindIP(UINT64 SessionID, WCHAR* OutIP);

	void          Stop();

protected:
	virtual bool  OnConnectionRequest(WCHAR* InputIP, unsigned short InputPort) = 0;

	virtual void  OnClientJoin(UINT64 SessionID) = 0;

	virtual void  OnClientLeave(UINT64 SessionID) = 0;

	virtual void  OnRecv(UINT64 SessionID, CMessage* pMessage) = 0;

private:
	void          WorkerThread();
	void          AcceptThread();
	void          SendThread();
	
public:
	//------------------------------------------------------------------------------
	// ��Ʈ��ũ ���̺꿡���� ����� �Լ�
	//------------------------------------------------------------------------------
	void          Net_Init(WCHAR* SERVERIP, int SERVERPORT, bool OffNagle);
	void          Mem_Init();
	void          Thread_Create();
	void          Thread_Destroy();
	void          FindSession(UINT64 SessionID, CSession** ppSession); // �ƿ��Ķ���� nullptr�̸� ����ID�� Invalid��.
	UINT64        MakeSessionID(UINT16 index, UINT64 allocID);

	bool          RecvPost(CSession* pSession);              //WSARecv�ؼ� ���� ������ �޴� �Լ�
	bool          SendPost(CSession* pSession);              //WSASend�ؼ� ������ �۽��ϴ� �Լ�
	bool          SessionInvalid(CSession* pSession, UINT64 SessionID);        //���� ��ȿ�� üũ
	bool          Release(CSession* pSession, UINT64 CheckID, long retIOCount);
	
	void          RecvIOProc(CSession* pSession, DWORD cbTransferred);
	void          SendIOProc(CSession* pSession, DWORD cbTransferred);
	void          ReleaseProc(CSession* pSession);

	void          Encoding(char* ptr, int len, UCHAR randkey); //�ش� ��ġ ���� len��ŭ ���ڵ� �۾�
	void          Decoding(char* ptr, int len, UCHAR randkey); //�ش� ��ġ ���� len��ŭ ���ڵ� �۾� 



};

