#pragma once

#define df_GAMELIB_MAX_THREAD_COUNT     100
#define df_GAMELIB_DEFAULT_GROUP_COUNT  100
#define df_GAMELIB_IP_LEN               16
#define df_GAMELIB_INDEX_POS            48
#define df_GAMELIB_ID_POS               47
#define df_SENDQ_MAX_SIZE               10000

class CGroup;
class CSession;
class CMessage;
class IUser;

template <typename T>
class CMemoryPool;

template <typename T>
class LFStack;


class CGameLibrary
{
public:
	enum TASK_TYPE
	{
		en_RELEASE,       // GQCS Key : CSession*
		en_FRAME,         // GQCS Key : GroupID
		en_GROUPMOVE,     // GQCS Key : CMessage* (sessionID, IUser*)
	};


public:
	struct st_IOFLAG
	{
		long long s_cnt;
		long long s_flag;
	};

private:
	HANDLE                                   m_IOCP;                                          // IOCP ���ҽ� �ڵ�
	SOCKET                                   m_Listen;                                        // ���� ���� ����
	BOOL                                     m_Endflag;

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Config ����
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	std::string                              m_IP;                                            // ���� IP
	INT                                      m_Port;                                          // ���� Port
	INT                                      m_MaxSessionCnt;                                 // �ִ� ���� ���� ����
	INT                                      m_CreateWorkerCnt;                               // ���� ��Ŀ ������ ����
	INT                                      m_ConcurrentCnt;                                 // IOCP ���� ������ ����
	INT                                      m_SendFrame;                                     // Send ������ Sleep��
	INT                                      m_PacketCode;                                    // ��Ʈ��ũ ���̺귯�� ��Ŷ �ڵ�
	INT                                      m_FixedKey;                                      // ���ڵ� ���� Ű ��
	INT                                      m_SendThFL;                                      // Send ������ ���� ���� �÷��� ( 0 : ���� / 1 : �ѱ� )
	INT                                      m_Nagle;                                         // ���̱� �ɼ�
																	                          

public:
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// ����͸� ����
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	LONG                                     m_AcceptTPS;                                     // �ʴ� Accept ó�� Ƚ�� ����͸� ��
	LONG                                     m_RecvIOTPS;                                     // �ʴ� Recv ó�� Ƚ�� ����͸� ��
	LONG                                     m_SendIOTPS;                                     // �ʴ� Send ó�� Ƚ�� ����͸� ��
	INT64                                    m_AcceptTotal;                                   // Acceptó���� �ִ� ���� ����
	SHORT                                    m_CurSessionCnt;                                 // ���� �迭���� ������ ������� ���� ����


private:
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// ������ ���� �� �ڷᱸ��
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	std::thread                              m_IOCPWorkerThread[df_GAMELIB_MAX_THREAD_COUNT]; // Worker ���� �迭
	std::thread                              m_AcceptThread;                                  // AcceptThread ����
	std::thread                              m_SendThread;                                    // SendThread
	std::thread                              m_FrameThread;                                   // ������ üũ�� ������


	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// ���� ���� �ڷᱸ�� �� ��� ����
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	CSession*                                m_SessionTable;      	                          // ���� ����ü�� ������ �迭
	LFStack<UINT16>*                         m_pSessionIdxStack;                              // ���� �迭�� index ������ �ڷᱸ��
	UINT64                                   m_AllocID;                                       // ���ǿ��� �Ҵ��� ���� ID



	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// �׷� ���� ���� �� �ڷᱸ��
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	std::vector<CGroup*>                     m_GroupArray;                                    // ������ ��ü ������ �ڷᱸ��
	std::unordered_map<std::wstring, UINT16> m_GroupMap;                                      // Attach ���ڷ� ���� ���ڿ��� �׷� ��ü �迭 index ���� �ڷᱸ��
	UINT16                                   m_GroupID;                                       // �׷� ��Ͻ� �ο��� ID (�� �ڷᱸ���� Index�� ���)


public:
	CGameLibrary();
	~CGameLibrary();

	bool          Run();                                                                      // ���� ���̺귯�� �۵� �Լ�
	void          Stop();                                                                     // ���� ���̺귯�� ��� ���� �� ������ ����
		          																	          
	bool          Attach(CGroup* pContents, std::wstring contentsType, UINT64 groupframe, BOOL shared = FALSE);          // �׷���� ���Ϳ� ����� �Լ�

private:
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// �ʱ�ȭ, ������ ����, ���� �Լ�
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	void          Mem_Init(INT sessionmax, INT createiothread, INT activethread, WORD packet_code, WORD packet_key, INT sendframe, INT sendflag, INT nagle);  // ��� �ʱ�ȭ
	void          Net_Init(WCHAR* serverIp, INT serverport);           // ��Ʈ��ũ  �ʱ�ȭ
	void          Session_Init();
	void          Group_Init();
	void          Monitoring_Init();
		          
	void          Thread_Create(INT sendflag);
	void          Thread_Destroy();

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// ���� ���̺귯�� ������
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	void          FrameThread();                                                              // �׷� ��ü ������ ���� PQCS�� ��� ���� ������
	void          WorkerThread();                                                             // Task �� ��Ʈ��ũ ó�� ���� ������ 
	void          SendThread();                                                               // Send ��� ������
	void          AcceptThread();                                                             // Accept ������


public:
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// �ܺ� ���� �Լ�
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	bool          SendPacket(UINT64 SessionID, CMessage* pMessage);
	bool          Disconnect(UINT64 SessionID);
	bool          FindIP(UINT64 SessionID, std::wstring& OutIP);
	bool          GroupMove(std::wstring ToContents, UINT64 sessionID, IUser* pUser);
		          
	LONG          GetAcceptTPS();
	LONG          GetRecvIOTPS();
	LONG          GetSendIOTPS();
	INT64         GetAcceptTotal();
	SHORT         GetCurSessionCount();
		          
	void          SetAcceptTPS(LONG value);
	void          SetRecvIOTPS(LONG value);
	void          SetSendIOTPS(LONG value);
			      
	CGroup*       GetGroupPtr(std::wstring Contents);

private:
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// ���Ӷ��̺귯�� ��Ʈ��ũ ���� ó�� �Լ�
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	bool          RecvPost(CSession* pSession);
	bool          SendPost(CSession* pSession);
	bool          Release(CSession* pSession,long long retIOCount);
	bool          SessionInvalid(CSession* pSession, UINT64 CheckID);                         // ���� ��ȿ�� üũ �Լ�(true�� Count �ϳ� �ø�)
																	                          
	void          RecvIOProc(CSession* pSession, DWORD cbTransferred);                        
	void          SendIOProc(CSession* pSession, DWORD cbTransferred);                        
	void          Encoding(char* ptr, int len, UCHAR randkey);                                //�ش� ��ġ ���� len��ŭ ���ڵ� �۾�
	void          Decoding(char* ptr, int len, UCHAR randkey);                                //�ش� ��ġ ���� len��ŭ ���ڵ� �۾� 
	void          FindSession(UINT64 sessionID, CSession** ppSession);                        // �ܼ��� ����ID�� ���� �迭�� ���� ã�� �Լ�, �ƿ��Ķ���� nullptr�̸� ����ID�� INVALID
	UINT64        MakeSessionID(UINT16 index, UINT64 allocID);




private:
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// IOCP Worker ������ PQCS ó�� �Լ�
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	void          ReleaseProc(CSession* pSession);
	void          FrameProc(UINT16 groupID);
	void          GroupMoveProc(CMessage* pMessage);
};

