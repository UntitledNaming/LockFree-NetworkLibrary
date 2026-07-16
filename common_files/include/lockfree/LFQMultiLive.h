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
	UINT64                              m_TailCnt;
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
		m_TailCnt = 0;
		m_Qid = (UINT64)&m_size;


		//더미 노드 1개 생성
		Node* dmyNode = m_pMemoryPool->Alloc();
		dmyNode->_next = nullptr;
		dmyNode->_Qid = m_Qid;


		m_pHead = (Node*)((UINT64)dmyNode | (InterlockedIncrement64((volatile __int64*)&m_HeadCnt) << 47));
		m_pTail = (Node*)((UINT64)dmyNode | (InterlockedIncrement64((volatile __int64*)&m_TailCnt) << 47));

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
		T temp;
		while (Dequeue(temp))
		{

		}

		m_size = 0;
	}

	void Enqueue(T InputParam)
	{
		Node*    newNode = nullptr;
		Node*    localTail = nullptr;
		Node*    localRealTail = nullptr;
		Node*    localTailNext = nullptr;
		UINT64   retCnt;

		//비교 노드
		CmpNode cmp;

		//신규 노드 생성
		newNode = m_pMemoryPool->Alloc();
		newNode->_data = InputParam;
		newNode->_next = (Node*)0xFFFFFFFFFFFFFFFF;
		newNode->_Qid = m_Qid;


		retCnt = InterlockedIncrement64((long long*)&m_TailCnt);

		//사전 작업
		while (1)
		{
			localTail = m_pTail;
			localRealTail = (Node*)((UINT64)localTail & BITMASK);
			localTailNext = localRealTail->_next;

			if (localTailNext == nullptr)
				break;

			if (localTailNext == (Node*)0xFFFFFFFFFFFFFFFF)
				continue;

			localTailNext = (Node*)((UINT64)localTailNext | (retCnt << 47));

			//next가 nullptr이 아니라면 tail을 바꾸자.
			if (InterlockedCompareExchange64((__int64*)&m_pTail, (__int64)localTailNext, (__int64)localTail) == (__int64)localTail)
			{
				retCnt = InterlockedIncrement64((long long*)&m_TailCnt);
			}
		}


		//CAS 작업
		while (1)
		{
			localTail = m_pTail;
			localRealTail = (Node*)((UINT64)localTail & BITMASK);
			cmp.s_Qid = m_Qid;
			cmp.s_next = nullptr;

			//_tail->next 원자적으로 변경 시도
			if (InterlockedCompareExchange128((long long*)&localRealTail->_next, (long long)m_Qid, (long long)newNode, (long long*)&cmp) == 1)
			{
				newNode->_next = nullptr;

				newNode = (Node*)((UINT64)newNode | (retCnt << 47));

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
		Node*    localHead = nullptr;
		Node*    localHeadNext = nullptr;
		Node*    realHead = nullptr;
		Node*    localRealTail = nullptr;
		Node*    realHeadNext = nullptr;
		Node*    localTail;
		Node*    localTailNext;
		UINT64   retCntHead;
		UINT64   retCntTail;
		T        temp;

		retCntTail = InterlockedIncrement64((long long*)&m_TailCnt);

		//사전 작업
		while (1)
		{
			localTail = m_pTail;
			localRealTail = (Node*)((UINT64)localTail & BITMASK);
			localTailNext = localRealTail->_next;

			if ((localTailNext == nullptr))
				break;

			if (localTailNext == (Node*)0xFFFFFFFFFFFFFFFF)
				continue;

			localTailNext = (Node*)((UINT64)localTailNext | (retCntTail << 47));

			//next가 nullptr이 아니라면 tail을 바꾸자.
			if (InterlockedCompareExchange64((__int64*)&m_pTail, (__int64)localTailNext, (__int64)localTail) == (__int64)localTail)
			{
				retCntTail = InterlockedIncrement64((long long*)&m_TailCnt);
			}

		}


		retCntHead = InterlockedIncrement64((long long*)&m_HeadCnt);

		while (1)
		{
			localHead = m_pHead;
			realHead = (Node*)((UINT64)localHead & BITMASK);
			realHeadNext = realHead->_next;

			// 내가 바라본 head와 다르면 다시 스냅샷 뜨기
			// 이 작업을 통해 내가 바라봤던 head가 실제 head가 아니라 다른 쪽에서 enq하려고 재활용한 노드인데 head로 착각하는 상황 방지
			if (localHead != m_pHead)
				continue;

			// 이 사이에 1st CAS해서 큐에 삽입한 것은 인지 불가

			// 그 당시 바라봤던 head의 next가 nullptr이거나 FFFF면 그냥 뺄 노드가 없다고 판단하고 false 리턴.
			if (realHeadNext == nullptr || realHeadNext == (Node*)0xFFFFFFFFFFFFFFFF)
				return false;

			localHeadNext = (Node*)((UINT64)realHeadNext | (retCntHead << 47));
			temp = realHeadNext->_data;

			if (InterlockedCompareExchange64((volatile __int64*)&m_pHead, (__int64)localHeadNext, (__int64)localHead) != (UINT64)localHead)
				continue;

			break;
		}

		//데이터 반환
		OutputParam = temp;

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