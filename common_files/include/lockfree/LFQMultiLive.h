#pragma once
#include "LockFreeMemoryPoolLive.h"

#define LOG_BUFFER_SIZE 5000
#define MAX_LEN         300
#define USER_MEMORY_MAX 0x00007FFFFFFFFFFF
#define BITMASK         0x00007FFFFFFFFFFF
#define TAGMASK         0xFFFF800000000000


template<typename T>
class LFQueueMul
{
private:
	struct Node
	{
		T                               _data;
		alignas(16)Node*                _next;
		UINT64                          _Qid;
	};

	struct CmpNode
	{
		Node*                           s_next;
		UINT64                          s_Qid;
	};

private:
	Node*                               m_pHead;
	Node*                               m_pTail;
	LONG                                m_size;
	UINT64                              m_HeadCnt;
	UINT64                              m_Qid;

public:
	static CMemoryPool<Node>*           m_pMemoryPool;
	static LONG                         m_refCnt;


public:
	LFQueueMul(int size = 0)
	{
		//주소 bit 체크
		SYSTEM_INFO info;
		GetSystemInfo(&info);

		if (!((UINT64)info.lpMaximumApplicationAddress & USER_MEMORY_MAX))
		{
			wprintf(L"UserMemory Address for Tag bit is not 17Bit\n");
			__debugbreak();
		}

		// static 메모리 풀 생성 확인
		if (InterlockedIncrement(&m_refCnt) == 1)
			m_pMemoryPool = new CMemoryPool<Node>;
		else
		{
			while (m_pMemoryPool == nullptr)
			{

			}
		}


		//멤버 변수 초기화
		m_size = size;
		m_HeadCnt = 0;
		m_Qid = (UINT64)&m_size;


		//더미 노드 1개 생성
		Node* dmyNode = m_pMemoryPool->Alloc();
		dmyNode->_next = nullptr;
		dmyNode->_Qid = m_Qid;


		m_pHead = (Node*)((UINT64)dmyNode | (InterlockedIncrement64((volatile __int64*)&m_HeadCnt) << 47));
		m_pTail = dmyNode;

	}
	~LFQueueMul()
	{
		if (InterlockedDecrement(&m_refCnt) == 0)
		{
			delete m_pMemoryPool;
			m_pMemoryPool = nullptr;
		}
	}

	void Clear()
	{
		m_size = 0;
		m_HeadCnt = 0;
	}

	void Enqueue(T InputParam)
	{
		DWORD    curID = GetCurrentThreadId();
		Node*    newNode;
		Node*    localTail;
		Node*    localTailNext;

		//비교 노드
		CmpNode cmp;

		//신규 노드 생성
		newNode = m_pMemoryPool->Alloc();
		newNode->_data = InputParam;
		newNode->_next = (Node*)0xFFFFFFFFFFFFFFFF;
		newNode->_Qid = m_Qid;


		//사전 작업
		while (1)
		{
			localTail = m_pTail;
			localTailNext = localTail->_next;

			if ((localTailNext == (Node*)0xFFFFFFFFFFFFFFFF) || (localTailNext == nullptr) )
				break;

			//next가 nullptr이 아니라면 tail을 바꾸자.
			InterlockedCompareExchange64((__int64*)&m_pTail, (__int64)localTailNext, (__int64)localTail);

		}

		//CAS 작업
		while (1)
		{
			localTail = m_pTail;
			cmp.s_Qid = m_Qid;
			cmp.s_next = nullptr;

			//_tail->next 원자적으로 변경 시도
			if (InterlockedCompareExchange128((long long*)&m_pTail->_next, (long long)m_Qid, (long long)newNode, (long long*)&cmp) == 1)
			{
				newNode->_next = nullptr;

				//성공하면 tail도 원자적으로 변경
				InterlockedCompareExchange64((__int64*)&m_pTail, (__int64)newNode, (__int64)localTail);
				break;
			}
		}


		//size 증가는 모든 처리 끝나고
		InterlockedIncrement(&m_size);
	}


	bool Dequeue(T& OutputParam)
	{
		DWORD    curID = GetCurrentThreadId();
		Node*    localHead = nullptr;
		Node*    localHeadNext = nullptr;
		Node*    realHead = nullptr;
		Node*    realHeadNext = nullptr;
		Node*    localTail;
		Node*    localTailNext;
		UINT64   retCnt;


		//사전 작업
		while (1)
		{
			localTail = m_pTail;
			localTailNext = localTail->_next;

			if ((localTailNext == (Node*)0xFFFFFFFFFFFFFFFF) || (localTailNext == nullptr))
				break;

			//next가 nullptr이 아니라면 tail을 바꾸자.
			InterlockedCompareExchange64((__int64*)&m_pTail, (__int64)localTailNext, (__int64)localTail);

		}

		retCnt = InterlockedIncrement(&m_HeadCnt);

		while (1)
		{
			localHead = m_pHead;
			realHead = (Node*)((UINT64)localHead & BITMASK);
			realHeadNext = realHead->_next;
			if (realHeadNext == nullptr)
				return false;


			localHeadNext = (Node*)((UINT64)realHeadNext | (retCnt << 47));

			if (InterlockedCompareExchange64((volatile __int64*)&m_pHead, (__int64)localHeadNext, (__int64)localHead) != (UINT64)localHead)
				continue;

			break;
		}


		//데이터 반환
		localHeadNext = (Node*)((UINT64)localHeadNext & BITMASK);

		OutputParam = localHeadNext->_data;

		//노드 제거
		if (!m_pMemoryPool->Free(realHead))
			__debugbreak();

		InterlockedDecrement(&m_size);

		return true;
	}

	inline int GetUseSize()
	{
		return m_size;
	}

};

template <typename T>
CMemoryPool<typename LFQueueMul<T>::Node>* LFQueueMul<T>::m_pMemoryPool = nullptr;

template <typename T>
LONG LFQueueMul<T>::m_refCnt = 0;