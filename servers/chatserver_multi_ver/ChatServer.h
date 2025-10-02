#pragma once
#define df_TIMEOUT1        3000      // 3�� Ÿ�Ӿƿ�
#define df_TIMEOUT2        40000     // 30�� Ÿ�Ӿƿ�
#define df_FRAME           10

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

	struct st_SECTOR_LIST
	{
		SRWLOCK          s_lock;
		std::list<CUser*> s_list;
	}typedef SECTORLIST;

private:
	////////////////////////////////////////////////////////////////////////////
	// ChatServer �ڷ� ���� �� ��� ��ü
	////////////////////////////////////////////////////////////////////////////
	std::unordered_map<UINT64, CUser*>  m_UserMap;
	std::unordered_map<UINT64, DWORD>   m_NonUserMap;
	SECTORLIST                          m_Sector[SECTOR_Y_MAX][SECTOR_X_MAX];
	SRWLOCK                             m_UserMapLock;
	SRWLOCK                             m_NonUserMapLock;
	CMemoryPool<CUser>*                 m_pUserPool;
	ProcessMonitor*                     m_pPDH;
	CMonitorClient*                     m_pMonitorClient;  // ����͸� ���� ���� Ŭ���̾�Ʈ
	BOOL                                m_EndFlag;         // �����,  ������ ���� �÷���


private:
	///////////////////////////////////////////////////////////////////////////
	// ChatServer ������
	///////////////////////////////////////////////////////////////////////////
	std::thread                         m_Frame;
	std::thread                         m_Monitor;

private:
	///////////////////////////////////////////////////////////////////////////
    // ChatServer ����͸� ����
    ///////////////////////////////////////////////////////////////////////////
	LONG                                m_ReqTPS;        // �ʴ� �޼��� ���� ����(���� �̵�, ä�� �޼���, �α��� ��û �պ�)
	LONG                                m_ReqLoginTPS;   // �ʴ� �޼��� ���� ����(�α��� ��û�� ���� ����)
	LONG                                m_ResLoginTPS;   // �ʴ� �޼��� ���� ����(�α��� ��û�� ���� ����)
	LONG                                m_ReqMoveTPS;    // �ʴ� �޼��� ���� ����(���� �̵� ��û�� ���� ����)
	LONG                                m_ResMoveTPS;    // �ʴ� �޼��� ���� ����(���� �̵� ��û�� ���� ����)
	LONG                                m_ReqChatMsgTPS; // �ʴ� �޼��� ���� ����(ä�� ������ ��û�� ���� ����, �ֺ� ���� �������� SendPacket�Ҷ� ���� ī����)
	LONG                                m_ResChatMsgTPS; // �ʴ� �޼��� ���� ����(ä�� ������ ��û�� ���� ����, �ֺ� ���� �������� SendPacket�Ҷ� ���� ī����)


	///////////////////////////////////////////////////////////////////////////
    // ChatServer Config ����
    ///////////////////////////////////////////////////////////////////////////
	INT                                 m_UserMaxCnt;

public:
	ChatServer();
	~ChatServer();

	BOOL         RunServer();
	void         StopServer();

private:
	void         Mem_Init(INT userMAX,WCHAR* bindip, WCHAR* serverip, INT serverport);
	void         Thread_Create();
	void         Thread_Destroy();
		         
		         
	UINT64       SumResMsgTPS();

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
	void AcquireSectorExclusiveLock(WORD eXpos, WORD eYpos, WORD iXpos, WORD iYpos);
	void ReleaseSectorExclusiveLock(WORD eXpos, WORD eYpos, WORD iXpos, WORD iYpos);
	void SendPacket_SectorOne(CMessage* pMessage, WORD xpos, WORD ypos, CUser* pUser);
	void SendPakcet_SectorAround(CMessage* pMessage, CUser* pUser);
	void SectorFind(SECTOR_AROUND* pAround, WORD xpos, WORD ypos);
	BOOL SectorRangeCheck(WORD xpos, WORD ypos);

	///////////////////////////////////////////////////////////////////////////
    // Recv Message �ڵ鷯
    ///////////////////////////////////////////////////////////////////////////
	void LoginProc(CMessage* pMessage, UINT64 sessionid);
	void SectorMoveProc(CMessage* pMessage, UINT64 sessionid);
	void ChatMessageProc(CMessage* pMessage, UINT64 sessionid);
	void HeartBeatProc(UINT64 sessionid);

	void FrameThread();
	void MonitorThread();
};