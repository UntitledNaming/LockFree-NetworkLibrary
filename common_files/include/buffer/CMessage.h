#pragma once
class CMessage
{
public:
	enum en_Message
	{
		eBuffer_Default = 500, // 직렬화 버퍼 기본 사이즈
		eBuffer_Max = 10000,
	};

	CMessage();
	CMessage(int size);
	~CMessage();

public:

	static CMessage* Alloc();
	static bool      Free(CMessage* pMessage);
	static void      Init(int lanHeaderSize, int netHeaderSize);        // 직렬화 버퍼 메모리 풀 동적할당 하기. 인자값으로 네트워크 헤더 크기 1번 받기
	static void      PoolDestroy();
				     										                  
															                  
public:														                  
	void             Clear(int type = 0);                               // 직렬화 버퍼 객체 초기화(Lan 환경(1) or Net 환경(0) 에서 쓸지 전달)
	void             SetNetHeader(int type);                            // 네트워크 헤더를 먼저 버퍼에 넣기 위한 함수. 네트워크 헤더를 위한 공간 확보함.
	void             SetEncodingFlag(int value);



	int              GetEncodingFlag();
	int              GetBufferSize();                                   // 반환값 : 버퍼 사이즈
	int              GetDataSize();                                     // 반환값 : 현재 사용중인 데이터 크기(네트워크 헤더 제외)
	int              GetRealDataSize(int type = 0);                     // 반환값 : 네트워크 헤더 포함 크기

	char*            GetReadPos();
	char*            GetWritePos();
	char*            GetAllocPos();

	bool             GetLastError();                                    // 연산자 오버로딩에서 오류가 났는지 체크하는 함수
																        
	int              MoveWritePos(int iSize);                           // 버퍼의 Pos 이동
	int              MoveReadPos(int iSize);					        
	                 											        
	int              GetData(char* chpDest, int iSize);                 // 반환값 : 외부로 복사한 데이터 크기
	int              PutData(char* chpSrc, int iSrcSize);               // 반환값 : 직렬화 버퍼로 복사한 사이즈
																           
	bool             Resize();                                          // 직렬화 버퍼 크기 resize
																           
	int              ResizeCount();                                     // resize 횟수 확인 함수

	int              AddRef();
	int              SubRef();

	//연산자 오버로딩 =
	CMessage& operator=(CMessage& clSrcMessage);

	//연산자 오버로딩 << , 직렬화 버퍼에 데이터 넣기
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


	//연산자 오버로딩 >> , 직렬화 버퍼에서 데이터 빼기
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

	static INT                  m_iNetHeaderSize;    // 헤더 사이즈
	static INT                  m_iLanHeaderSize;    // 헤더 사이즈
											         
	static BOOL                 m_netHderFlag;       // 네트워크 헤더 사용 플래그

private:
	CHAR*                       m_iAllocPtr;           // 직렬화 버퍼 할당 메모리 포인터
	CHAR*                       m_iReadPos;
	CHAR*                       m_iWritePos;
		                        
	INT                         m_iBufferSize;       // 직렬화 버퍼 크기
	INT                         m_iDataSize;         // 현재 사용중인 크기(네트워크 헤더 제외)
	INT                         m_iResizeCount;      // resize 횟수
	INT                         m_iRefCnt;	       
	INT                         m_EncodingFlag;      
			                      			       
	BOOL                        m_iError;            // 연산자 오버로딩에서 오류 탐지 플래그
};

