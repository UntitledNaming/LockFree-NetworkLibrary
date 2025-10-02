#pragma once
#define PTR_BITMASK     0x00007FFFFFFFFFFF
#define TIMEOUT1        3000      // 3�� Ÿ�Ӿƿ�
#define TIMEOUT2        40000     // 30�� Ÿ�Ӿƿ�
#define MESSAGE_LEN_MAX 256
#define SESSION_KEY_MAX 64


class CMonitorClient;
class ProcessMonitor;

template <typename T>
class CMemoryPool;

template <typename T>
class LFQueue;

class CUser;



class ChatServer : public CNetServer
{
private:
	enum JobType
	{
		// Update ������ Job Type
		en_Join = 0,
		en_Recv,
		en_Delete,
		en_End,
	};

	struct st_JOB
	{
		UINT64     s_Id;
		WORD       s_Type;
		CMessage*  s_ptr;   

	}typedef JOB;

private:
	////////////////////////////////////////////////////////////////////////////
	// ChatServer �ڷ� ���� �� ��� ��ü
	////////////////////////////////////////////////////////////////////////////
	std::unordered_map<UINT64, CUser*>  m_UserMap;
	std::unordered_map<UINT64, DWORD>   m_NonUserMap;
	std::list<CUser*>                   m_Sector[SECTOR_Y_MAX][SECTOR_X_MAX];
	LFQueue<JOB*>*                      m_pUpdateJobQ;
	CMemoryPool<CUser>*                 m_pUserPool;
	CMemoryPool<JOB>*                   m_pJobPool;
	ProcessMonitor*                     m_pPDH;
	CMonitorClient*                     m_pMonitorClient;  // ����͸� ���� ���� Ŭ���̾�Ʈ
	BOOL                                m_MonitorFlag;     // ����͸� ������ ���� �÷���


private:
	///////////////////////////////////////////////////////////////////////////
	// ChatServer ������
	///////////////////////////////////////////////////////////////////////////
	std::thread                         m_Update;
	std::thread                         m_Monitor;

private:
	///////////////////////////////////////////////////////////////////////////
    // ChatServer ����͸� ����
    ///////////////////////////////////////////////////////////////////////////
	LONG                                m_UpdateLoopTime;// ���� ������ �ð� ����
	LONG                                m_UpdateTPS;
	LONG                                m_ResTPS;        // �ʴ� �޼��� ���� ����(�α��� ��û, ���� �̵�, �޼��� ��û ��)
	LONG                                m_ReqTPS;        // �ʴ� �޼��� ��û ����(���� �̵�, ä�� �޼���, �α��� ��û �պ�)
	LONG                                m_ReqLoginTPS;   // �ʴ� �޼��� ��û ����(�α��� ��û�� ���� ����)
	LONG                                m_ResLoginTPS;   // �ʴ� �޼��� ���� ����(�α��� ��û�� ���� ����)
	LONG                                m_ReqMoveTPS;    // �ʴ� �޼��� ��û ����(���� �̵� ��û�� ���� ����)
	LONG                                m_ResMoveTPS;    // �ʴ� �޼��� ���� ����(���� �̵� ��û�� ���� ����)
	LONG                                m_ReqChatMsgTPS; // �ʴ� �޼��� ��û ����(ä�� ������ ��û�� ���� ����, �ֺ� ���� �������� SendPacket�Ҷ� ���� ī����)
	LONG                                m_ResChatMsgTPS; // �ʴ� �޼��� ���� ����(ä�� ������ ��û�� ���� ����, �ֺ� ���� �������� SendPacket�Ҷ� ���� ī����)


	///////////////////////////////////////////////////////////////////////////
    // ChatServer Config ����
    ///////////////////////////////////////////////////////////////////////////
	INT                                 m_UserMaxCnt;
	INT                                 m_UpdateFrame;

public:
	ChatServer();
	~ChatServer();

	BOOL         RunServer();
	void         StopServer();

private:
	void         Mem_Init(INT userMAX, INT updateFrame, WCHAR* monitorserverip, INT monitorserverport);
	void         Thread_Create();
	void         Thread_Destroy();
		         
		         

	///////////////////////////////////////////////////////////////////////////
    // CNetServer Callback �Լ� ����
    ///////////////////////////////////////////////////////////////////////////
	virtual bool OnConnectionRequest(WCHAR* InputIP, USHORT InputPort);
	virtual void OnClientJoin(UINT64 SessionID);
	virtual void OnClientLeave(UINT64 SessionID);
	virtual void OnRecv(UINT64 SessionID, CMessage* pMessage);

	///////////////////////////////////////////////////////////////////////////
	// Ÿ�Ӿƿ� �Լ�
	///////////////////////////////////////////////////////////////////////////
	void NonUserTimeOut();
	void UserTimeOut();

	///////////////////////////////////////////////////////////////////////////
	// ���� ���� �Լ�
	///////////////////////////////////////////////////////////////////////////
	void SendPacket_SectorOne(CMessage* pMessage, WORD xpos, WORD ypos, CUser* pUser);
	void SendPakcet_SectorAround(CMessage* pMessage, CUser* pUser);
	void SectorFind(SECTOR_AROUND* pAround, WORD xpos, WORD ypos);
	BOOL SectorRemove(CUser* pUser);
	BOOL SectorRangeCheck(WORD xpos, WORD ypos);

	///////////////////////////////////////////////////////////////////////////
    // Recv Message �ڵ鷯
    ///////////////////////////////////////////////////////////////////////////
	void LoginProc(CMessage* pMessage, UINT64 sessionid);
	void SectorMoveProc(CMessage* pMessage, UINT64 sessionid);
	void ChatMessageProc(CMessage* pMessage, UINT64 sessionid);
	void HeartBeatProc(CMessage* pMessage, UINT64 sessionid);

	///////////////////////////////////////////////////////////////////////////
    // Update Job �ڵ鷯
    ///////////////////////////////////////////////////////////////////////////
	void JoinProc(UINT64 sessionID);
	void RecvProc(UINT64 sessionID, CMessage* pMessage);
	void DeleteProc(UINT64 sessionID);

	void UpdateThread();
	void MonitorThread();
};