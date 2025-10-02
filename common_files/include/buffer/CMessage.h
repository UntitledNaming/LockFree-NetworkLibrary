#pragma once
class CMessage
{
public:
	enum en_Message
	{
		eBuffer_Default = 500, // ����ȭ ���� �⺻ ������
		eBuffer_Max = 10000,
	};

	CMessage();
	CMessage(int size);
	~CMessage();

public:

	static CMessage* Alloc();
	static bool      Free(CMessage* pMessage);
	static void      Init(int lanHeaderSize, int netHeaderSize);        // ����ȭ ���� �޸� Ǯ �����Ҵ� �ϱ�. ���ڰ����� ��Ʈ��ũ ��� ũ�� 1�� �ޱ�
	static void      PoolDestroy();
				     										                  
															                  
public:														                  
	void             Clear(int type = 0);                               // ����ȭ ���� ��ü �ʱ�ȭ(Lan ȯ��(1) or Net ȯ��(0) ���� ���� ����)
	void             SetNetHeader(int type);                            // ��Ʈ��ũ ����� ���� ���ۿ� �ֱ� ���� �Լ�. ��Ʈ��ũ ����� ���� ���� Ȯ����.
	void             SetEncodingFlag(int value);



	int              GetEncodingFlag();
	int              GetBufferSize();                                   // ��ȯ�� : ���� ������
	int              GetDataSize();                                     // ��ȯ�� : ���� ������� ������ ũ��(��Ʈ��ũ ��� ����)
	int              GetRealDataSize(int type = 0);                     // ��ȯ�� : ��Ʈ��ũ ��� ���� ũ��

	char*            GetReadPos();
	char*            GetWritePos();
	char*            GetAllocPos();

	bool             GetLastError();                                    // ������ �����ε����� ������ ������ üũ�ϴ� �Լ�
																        
	int              MoveWritePos(int iSize);                           // ������ Pos �̵�
	int              MoveReadPos(int iSize);					        
	                 											        
	int              GetData(char* chpDest, int iSize);                 // ��ȯ�� : �ܺη� ������ ������ ũ��
	int              PutData(char* chpSrc, int iSrcSize);               // ��ȯ�� : ����ȭ ���۷� ������ ������
																           
	bool             Resize();                                          // ����ȭ ���� ũ�� resize
																           
	int              ResizeCount();                                     // resize Ƚ�� Ȯ�� �Լ�

	int              AddRef();
	int              SubRef();

	//������ �����ε� =
	CMessage& operator=(CMessage& clSrcMessage);

	//������ �����ε� << , ����ȭ ���ۿ� ������ �ֱ�
	CMessage& operator<<(BYTE byValue);
	CMessage& operator<<(char chValue);
	CMessage& operator<<(short shValue);
	CMessage& operator<<(WORD wValue);
	CMessage& operator<<(int iValue);
	CMessage& operator<<(DWORD lValue);
	CMessage& operator<<(float fValue);
	CMessage& operator<<(__int64 iValue);
	CMessage& operator<<(unsigned long long iValue);
	CMessage& operator<<(double iValue);


	//������ �����ε� >> , ����ȭ ���ۿ��� ������ ����
	CMessage& operator >> (BYTE& byValue);
	CMessage& operator >> (char& chValue);
	CMessage& operator >> (short& shValue);
	CMessage& operator >> (WORD& wValue);
	CMessage& operator >> (int& iValue);
	CMessage& operator >> (DWORD& dwValue);
	CMessage& operator >> (float& fValue);
	CMessage& operator >> (__int64& iValue);
	CMessage& operator >> (unsigned long long& iValue);
	CMessage& operator >> (double& dValue);

public:
	static CMPoolTLS<CMessage>* m_pMessagePool;

	static INT                  m_iNetHeaderSize;    // ��� ������
	static INT                  m_iLanHeaderSize;    // ��� ������
											         
	static BOOL                 m_netHderFlag;       // ��Ʈ��ũ ��� ��� �÷���

private:
	CHAR*                       m_iAllocPtr;           // ����ȭ ���� �Ҵ� �޸� ������
	CHAR*                       m_iReadPos;
	CHAR*                       m_iWritePos;
		                        
	INT                         m_iBufferSize;       // ����ȭ ���� ũ��
	INT                         m_iDataSize;         // ���� ������� ũ��(��Ʈ��ũ ��� ����)
	INT                         m_iResizeCount;      // resize Ƚ��
	INT                         m_iRefCnt;	       
	INT                         m_EncodingFlag;      
			                      			       
	BOOL                        m_iError;            // ������ �����ε����� ���� Ž�� �÷���
};

