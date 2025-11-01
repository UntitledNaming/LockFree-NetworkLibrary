#include <iostream>
#include <windows.h>
#include <time.h>
#include "MemoryPoolTLS.h"
#include "LockFreeMemoryPoolLive.h"
#include "CMessage.h"

#pragma warning(disable:4996)

CMPoolTLS<CMessage>* CMessage::m_pMessagePool = nullptr;
INT CMessage::m_iNetHeaderSize = 0;
INT CMessage::m_iLanHeaderSize = 0;
bool CMessage::m_netHderFlag = false;


//직렬화 버퍼 생성자에서는 refCnt = 0으로 하고 Alloc 하고 Clear할 때 refCnt = 1로 만듬.

CMessage::CMessage()
{
	m_iAllocPtr = (char*)malloc(eBuffer_Default);
	m_iReadPos = m_iAllocPtr;
	m_iWritePos = m_iAllocPtr;
	m_iRefCnt = 0;
	m_iBufferSize = eBuffer_Default;
	m_iDataSize = 0;
	m_iError = false;
	m_iResizeCount = 0;
	m_EncodingFlag = 0;
}

CMessage::CMessage(int size)
{
	if (size > eBuffer_Max)
	{
		__debugbreak();
	}
	
	m_iAllocPtr = (char*)malloc(size);
	m_iReadPos = m_iAllocPtr;
	m_iWritePos = m_iAllocPtr;
	m_iRefCnt = 0;
	m_iBufferSize = size;
	m_iDataSize = 0;
	m_iError = false;
	m_iResizeCount = 0;
	m_EncodingFlag = 0;
}

CMessage::~CMessage()
{
	free(m_iAllocPtr);
	m_iAllocPtr = nullptr;
	m_iReadPos = nullptr;
	m_iWritePos = nullptr;
}

CMessage* CMessage::Alloc()
{
	return m_pMessagePool->Alloc();
}

bool CMessage::Free(CMessage* pMessage)
{
	int ret;
	ret = pMessage->SubRef();

	if (ret < 0)
		__debugbreak();

	else if (ret == 0)
	{
		m_pMessagePool->Free(pMessage);
		return true;
	}

	return false;
}

void CMessage::Init(int lanHeaderSize, int netHeaderSize)
{
	if (m_pMessagePool == nullptr)
	{
		m_pMessagePool = new CMPoolTLS<CMessage>;
	}

	m_iNetHeaderSize = netHeaderSize;
	m_iLanHeaderSize = lanHeaderSize;

	if (netHeaderSize != 0 || lanHeaderSize != 0)
	{
		m_netHderFlag = true;
	}
}

void CMessage::PoolDestroy()
{
	delete m_pMessagePool;
	m_pMessagePool = nullptr;
}

void CMessage::Clear(int type)
{
	m_iReadPos = m_iAllocPtr;
	m_iWritePos = m_iAllocPtr;
	m_iDataSize = 0;
	m_iRefCnt = 1;
	m_iError = false;
	m_EncodingFlag = 0;

	if (m_netHderFlag == true)
	{
		SetNetHeader(type);
	}
}

int CMessage::GetEncodingFlag()
{
	return m_EncodingFlag;
}

int CMessage::GetBufferSize()
{
	return m_iBufferSize;
}

int CMessage::GetDataSize()
{
	return m_iDataSize;
}

int CMessage::GetRealDataSize(int type)
{
	if (type == 0)
	{
		return m_iDataSize + m_iNetHeaderSize;
	}
	else if (type == 1)
	{
		return m_iDataSize + m_iLanHeaderSize;
	}

	return -1;
}

char* CMessage::GetReadPos()
{
	return m_iReadPos;
}

char* CMessage::GetWritePos()
{
	return m_iWritePos;
}

char* CMessage::GetAllocPos()
{
	return m_iAllocPtr;
}

bool CMessage::GetLastError()
{
	return m_iError;
}

void CMessage::SetEncodingFlag(int value)
{
	InterlockedExchange((long*) & m_EncodingFlag, value);
}

void CMessage::SetNetHeader(int type)
{
	if (type == 0)
	{
		m_iReadPos = m_iAllocPtr + m_iNetHeaderSize;
		m_iWritePos = m_iAllocPtr + m_iNetHeaderSize;
	}
	else if (type == 1)
	{
		m_iReadPos = m_iAllocPtr + m_iLanHeaderSize;
		m_iWritePos = m_iAllocPtr + m_iLanHeaderSize;
	}
}

int CMessage::GetData(char* chpDest, int iSize)
{
	if (m_iDataSize < iSize)
		return 0;

	memcpy_s(chpDest, iSize, m_iReadPos, iSize);

	m_iReadPos += iSize;
	m_iDataSize -= iSize;

	return iSize;
}

int CMessage::MoveWritePos(int iSize)
{
	if (iSize > (m_iBufferSize - m_iDataSize))
		return 0;


	m_iWritePos += iSize;
	m_iDataSize += iSize;

	return iSize;
}

int CMessage::MoveReadPos(int iSize)
{
	if (m_iDataSize < iSize)
		return 0;

	m_iReadPos += iSize;
	m_iDataSize -= iSize;

	return iSize;
}

int CMessage::PutData(char* chpSrc, int iSrcSize)
{
	if (iSrcSize > (m_iBufferSize - m_iDataSize))
		return 0;

	memcpy_s(m_iWritePos, iSrcSize, chpSrc, iSrcSize);

	m_iWritePos += iSrcSize;
	m_iDataSize += iSrcSize;

	return iSrcSize;
}

bool CMessage::Resize()
{
	if (m_iBufferSize * 2 >= eBuffer_Max)
		return false;

	// 임시 버퍼 생성
	char* pTemp = (char*)malloc(m_iBufferSize * 2); //2배 큰 직렬화 버퍼 생성
	memcpy_s(pTemp, m_iBufferSize, m_iAllocPtr, m_iBufferSize); //기존 직렬화 버퍼 복사
	
	__int64 tempWpos = m_iWritePos - m_iAllocPtr; //offset 구하기
	__int64 tempRpos = m_iReadPos - m_iAllocPtr;

	//기존 버퍼 해제
	free(m_iAllocPtr);

	//임시 버퍼 포인터를 m_iBuffer 값으로 설정 및 버퍼 크기 재설정
	m_iAllocPtr = pTemp;
	m_iBufferSize = m_iBufferSize * 2;
	m_iWritePos = m_iAllocPtr+ tempWpos;
	m_iReadPos = m_iAllocPtr + tempRpos;

	m_iResizeCount++;

	return true;
}

int CMessage::ResizeCount()
{
	return m_iResizeCount;
}

int CMessage::AddRef()
{
	return InterlockedIncrement((volatile long*)&m_iRefCnt);
}

int CMessage::SubRef()
{
	return InterlockedDecrement((volatile long*)&m_iRefCnt);
}

#pragma region 연산자 오버로딩
CMessage& CMessage::operator=(CMessage& clSrcMessage)
{
	m_iDataSize = clSrcMessage.GetDataSize();

	PutData(clSrcMessage.m_iReadPos, m_iDataSize);

	return *this;
}

CMessage& CMessage::operator<<(BYTE byValue)
{
	*(BYTE*)m_iWritePos = byValue;
	m_iWritePos += sizeof(BYTE);
	m_iDataSize += sizeof(BYTE);


	return *this;
}

CMessage& CMessage::operator<<(char chValue)
{
	*(char*)m_iWritePos = chValue;
	m_iWritePos += sizeof(char);
	m_iDataSize += sizeof(char);

	return *this;
}

CMessage& CMessage::operator<<(short shValue)
{
	*(short*)m_iWritePos = shValue;
	m_iWritePos += sizeof(short);
	m_iDataSize += sizeof(short);

	return *this;
}

CMessage& CMessage::operator<<(WORD wValue)
{
	*(WORD*)m_iWritePos = wValue;
	m_iWritePos += sizeof(WORD);
	m_iDataSize += sizeof(WORD);

	return *this;
}

CMessage& CMessage::operator<<(int iValue)
{
	*(int*)m_iWritePos = iValue;
	m_iWritePos += sizeof(int);
	m_iDataSize += sizeof(int);

	return *this;
}

CMessage& CMessage::operator<<(DWORD lValue)
{
	*(DWORD*)m_iWritePos = lValue;
	m_iWritePos += sizeof(DWORD);
	m_iDataSize += sizeof(DWORD);

	return *this;
}

CMessage& CMessage::operator<<(float fValue)
{
	*(float*)m_iWritePos = fValue;
	m_iWritePos += sizeof(float);
	m_iDataSize += sizeof(float);

	return *this;
}

CMessage& CMessage::operator<<(__int64 iValue)
{
	*(__int64*)m_iWritePos = iValue;
	m_iWritePos += sizeof(__int64);
	m_iDataSize += sizeof(__int64);

	return *this;
}

CMessage& CMessage::operator<<(unsigned long long  iValue)
{
	*(unsigned long long*)m_iWritePos = iValue;
	m_iWritePos += sizeof(unsigned long long);
	m_iDataSize += sizeof(unsigned long long);

	return *this;
}

CMessage& CMessage::operator<<(double iValue)
{
	*(double*)m_iWritePos = iValue;
	m_iWritePos += sizeof(double);
	m_iDataSize += sizeof(double);

	return *this;
}

CMessage& CMessage::operator>>(BYTE& byValue)
{
	byValue = *(BYTE*)m_iReadPos;

	if (m_iDataSize < sizeof(BYTE))
	{
		m_iError = true;
		return *this;
	}

	m_iReadPos += sizeof(BYTE);
	m_iDataSize -= sizeof(BYTE);

	return *this;
}

CMessage& CMessage::operator>>(char& chValue)
{
	chValue = *(char*)m_iReadPos;

	if (m_iDataSize < sizeof(char))
	{
		m_iError = true;
		return *this;
	}

	m_iReadPos += sizeof(char);
	m_iDataSize -= sizeof(char);

	return *this;
}

CMessage& CMessage::operator>>(short& shValue)
{
	shValue = *(short*)m_iReadPos;

	if (m_iDataSize < sizeof(short))
	{
		m_iError = true;
		return *this;
	}


	m_iReadPos += sizeof(short);
	m_iDataSize -= sizeof(short);

	return *this;
}

CMessage& CMessage::operator>>(WORD& wValue)
{
	wValue = *(WORD*)m_iReadPos;

	if (m_iDataSize < sizeof(WORD))
	{
		m_iError = true;
		return *this;
	}


	m_iReadPos += sizeof(WORD);
	m_iDataSize -= sizeof(WORD);

	return *this;
}

CMessage& CMessage::operator>>(int& iValue)
{
	iValue = *(int*)m_iReadPos;

	if (m_iDataSize < sizeof(int))
	{
		m_iError = true;
		return *this;
	}


	m_iReadPos += sizeof(int);
	m_iDataSize -= sizeof(int);

	return *this;
}

CMessage& CMessage::operator>>(DWORD& dwValue)
{
	dwValue = *(DWORD*)m_iReadPos;

	if (m_iDataSize < sizeof(DWORD))
	{
		m_iError = true;
		return *this;
	}



	m_iReadPos += sizeof(DWORD);
	m_iDataSize -= sizeof(DWORD);

	return *this;
}

CMessage& CMessage::operator>>(float& fValue)
{
	fValue = *(float*)m_iReadPos;

	if (m_iDataSize < sizeof(float))
	{
		m_iError = true;
		return *this;
	}


	m_iReadPos += sizeof(float);
	m_iDataSize -= sizeof(float);

	return *this;
}

CMessage& CMessage::operator>>(__int64& iValue)
{
	iValue = *(__int64*)m_iReadPos;

	if (m_iDataSize < sizeof(__int64))
	{
		m_iError = true;
		return *this;
	}

	m_iReadPos += sizeof(__int64);
	m_iDataSize -= sizeof(__int64);

	return *this;
}

CMessage& CMessage::operator>>(unsigned long long& iValue)
{
	iValue = *(unsigned long long*)m_iReadPos;

	if (m_iDataSize < sizeof(unsigned long long))
	{
		m_iError = true;
		return *this;
	}

	m_iReadPos += sizeof(unsigned long long);
	m_iDataSize -= sizeof(unsigned long long);

	return *this;
}

CMessage& CMessage::operator>>(double& dValue)
{
	dValue = *(double*)m_iReadPos;

	if (m_iDataSize < sizeof(double))
	{
		m_iError = true;
		return *this;
	}


	m_iReadPos += sizeof(double);
	m_iDataSize -= sizeof(double);

	return *this;
}

#pragma endregion