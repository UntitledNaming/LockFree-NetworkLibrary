#pragma comment(lib, "winmm.lib")
#include <iostream>
#include <windows.h>
#include <atomic>
#include <process.h>
#include "Ring_Buffer.h"

//주의사항함.
//
// recv 체크하는 버퍼는 링 버퍼 보다 커야 



#pragma region RingBuffer


CRingBuffer::CRingBuffer()
{
	_size = DEFAULT_SIZE; //디폴트로 링 버퍼 크기 100바이트로 설정
	_usingSize = 0;

	_allocPos = (char*)malloc(_size);
	_readPos = _allocPos;
	_writePos = _allocPos;

}

CRingBuffer::CRingBuffer(int iBufferSize)
{
	_size = iBufferSize;
	_usingSize = 0;

	_allocPos = (char*)malloc(_size);
	_readPos = _allocPos;
	_writePos = _allocPos;

}

CRingBuffer::~CRingBuffer()
{
	free(_allocPos);
}

// 링버퍼에 복사할 데이터 크기가 링버퍼에 남은 데이터 보다 클 때와 
// 만약 writePos를 옮겼을 때 readPos와 겹치면 0을 반환해서 Enqueue작업을 안할 것임.
int CRingBuffer::Enqueue(const char* chpData, int iSize)
{
	//링버퍼에 복사할 데이터 크기가 남은 링버퍼 크기보다 클 때
	if (iSize >= GetFreeSize())
	{

		return 0;
	}

	long long des;
	__int64 leftenq;

	//과거의 readPos로 판단할 것임.
	char* oldReadPos = _readPos;


	des = DirectEnqueueSize();

	//실제 Enqueue 작업
	if (iSize <= des)
	{
		//여기서 writePos값을 미리 계산 해보고 만약 readPos와 겹치면 Enqueue작업 안할 것임
		if ((_writePos + iSize == oldReadPos))
			return 0;

		memcpy_s(_writePos, iSize, chpData, iSize);


		_writePos += iSize;

		if (_writePos == _allocPos + _size)
			_writePos = _allocPos;

	}


	//넣을 크기가 DES보다 크면 한번 더 작업 해야 함
	else
	{
		leftenq = iSize - des;

		if (oldReadPos == _allocPos + leftenq)
			return 0;


		memcpy_s(_writePos, des, chpData, des);
		_writePos += des;
		_writePos = _allocPos;

		memcpy_s(_writePos, leftenq, chpData + des, leftenq);

		_writePos += leftenq;
	}


	_usingSize += iSize;


	return iSize;
}
// 디큐작업하고 나서 readPos의 예상되는 위치가 writePos가 같으면 애초에 그냥 Dequeue 작업 안할 것임.
int CRingBuffer::Dequeue(char* chpData, int iSize)
{
	long long dds;
	__int64 left;
	int len;

	if (iSize > GetUseSize())
		return 0;

	len = iSize;


	dds = DirectDequeueSize();

	//그게 아니면 그냥 빼낼 크기만큼만 루프 돌면 됨.
	if (len <= dds)
	{

		memcpy_s(chpData, len, _readPos, len);

		_readPos = _readPos + len;

		if (_readPos == _allocPos + _size)
			_readPos = _allocPos;

	}

	

	//확정된 빼낼 크기가 DirectDequeueSize보다 크면 포인터 이동해서 루프 한번 더 돌아야 함.
	else
	{
		left = len - dds;


		memcpy_s(chpData, dds, _readPos, dds);

		_readPos += dds;
		_readPos = _allocPos;

		memcpy_s(chpData + dds, left, _readPos, left);

		_readPos = _readPos + left;

		if (_readPos == _allocPos + _size)
			_readPos = _allocPos;

	}


	_usingSize -= len;

	return len;
}

int CRingBuffer::Peek(char* chpData, int iSize)
{
	long long dds;
	__int64 left;

	if (iSize > GetUseSize())
		return 0;

	dds = DirectDequeueSize();

	//그게 아니면 그냥 빼낼 크기만큼만 루프 돌면 됨.
	if (iSize <= dds)
	{
		memcpy_s(chpData, iSize, _readPos, iSize);
		
	}

	//확정된 빼낼 크기가 DirectDequeueSize보다 크면 포인터 이동해서 루프 한번 더 돌아야 함.

	else
	{
		left = iSize - dds;

		memcpy_s(chpData, dds, _readPos, dds);
		memcpy_s(chpData + dds, left, _allocPos, left);

	}


	return iSize;
}

#pragma endregion

