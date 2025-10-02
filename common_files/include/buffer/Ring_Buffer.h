#pragma once

#define DEFAULT_SIZE 10000

class CRingBuffer
{
private:
	int _size; //총 크기
	int _usingSize; // 사용 중인 크기
	char* _writePos; // 삽입하기 시작할 위치
	char* _readPos; // 빼내기 시작할 위치
	char* _allocPos; // 할당 위치

public:
	CRingBuffer();
	CRingBuffer(int iBufferSize);
	~CRingBuffer();
	inline int GetBufferSize()
	{
		return _size;
	}


	//현재 사용중인 용량 얻기
	inline int GetUseSize()
	{
		char* oldWritePos = _writePos;
		char* oldReadPos = _readPos;


		if (oldWritePos >= oldReadPos)
		{
			return oldWritePos - oldReadPos;
		}

		return (oldWritePos - _allocPos) + (_allocPos + _size - oldReadPos);

	}

	//현재 버퍼에 남은 용량 얻기
	inline int GetFreeSize()
	{
		char* oldWritePos = _writePos;
		char* oldReadPos = _readPos;


		if (oldWritePos >= oldReadPos)
		{

			return (oldReadPos - _allocPos) + (_allocPos + _size - oldWritePos);
		}

		return (oldReadPos - oldWritePos);
	}


	//WritePos에 데이터를 삽입.
	//반환값은 넣은 데이터 크기, 링버퍼에 남은 크기보다 iSize 클 때 0반환
	int Enqueue(const char* chpData, int iSize);

	//ReadPos에서 데이터를 가져옴
	//반환값 : 가져온 크기, 
	int Dequeue(char* chpData, int iSize);


	//ReadPos에서 데이터를 읽어옴. ReadPos는 안움직임
	//반환값 : 가져온 크기
	int Peek(char* chpDatat, int iSize);

	//버퍼의 모든 데이터 삭제
	inline void Clear()
	{
		_writePos = _allocPos;
		_readPos = _allocPos;
		_usingSize = 0;
	}


	//실제 링버퍼는 1차원으로 이어져 있을 것임. 그래서 WritePos부터 Enqueue를 하면서 WritePos가 이동하는데
	//할당 받은 메모리 주소까지 이동하면 다시 맨 앞 메모리로 이동해야 함. 이 때 WritePos부터 할당 받은 메모리 끝까지의 길이거나
	// 1차원 메모리 배열 상 WritePos가 ReadPos 앞에 올 수 있음. 이러면 ReadPos와 WritePos 사이 길이
	// DirectDequeueSize도 마찬가지로
	//반환값 : 사용가능한 용량
	inline int DirectEnqueueSize()
	{
		char* oldReadPos = _readPos;
		char* oldWritePos = _writePos;

		if (_size == GetUseSize())
			return 0;

		if (oldWritePos < oldReadPos)
			return (oldReadPos - oldWritePos);

		return (_allocPos + _size - oldWritePos);
	}
	inline int DirectDequeueSize()
	{
		char* oldReadPos = _readPos;
		char* oldWritePos = _writePos;

		int useSize = GetUseSize();

		if (useSize == 0)
			return 0;

		if (oldReadPos < oldWritePos)
		{

			return (oldWritePos - oldReadPos);
		}

		return(_allocPos + _size - oldReadPos);
	}


	//원하는 길이만큼 ReadPos에서 해당 길이 만큼 삭제
	//원하는 길이만큼 WritePos에서 해당 길이 만큼 이동
	//반환값 : 이동 크기
	inline int MoveWritePos(int iSize)
	{
		int left;

		_writePos += iSize;
		if (_writePos >= _allocPos + _size)
		{
			left = _writePos - (_allocPos + _size);
			_writePos = _allocPos + left;
		}

		_usingSize += iSize;

		return iSize;
	}

	inline int MoveReadPos(int iSize)
	{
		int left;

		_readPos += iSize;
		if (_readPos >= _allocPos + _size)
		{
			left = _readPos - (_allocPos + _size);
			_readPos = _allocPos + left;
		}

		_usingSize -= iSize;

		return iSize;
	}


// 버퍼의 Front포인터 획득
//반환값 : 버퍼 포인터
	inline char* GetWritePtr()
	{
		return _writePos;
	}

	//버퍼의 ReadPos 포인터 얻음.
	//반환값 : 버퍼 포인터
	inline char* GetReadPtr()
	{
		return _readPos;
	}

	inline char* GetAllocPtr() { return _allocPos; }

};
