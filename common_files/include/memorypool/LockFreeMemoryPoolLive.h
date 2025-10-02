#ifndef  __MEMORY_POOL__
#define  __MEMORY_POOL__
#include <new.h>
#include <malloc.h>
#include <windows.h>

#define  UPCAPACITYSIZE 50
#define  BITMASK        0x00007FFFFFFFFFFF

typedef bool     MYBOOL;

template <typename T>
class CMemoryPool
{
private:

	//////////////////////////////////////////////////////////////////////////
	// 메모리 풀에서 사용할 노드 구조체
	// s_guard  : 노드 앞에 메모리 침범 체크할 가드, Free할때 체크
	// s_data   : 노드에 객체 자체를 저장
	// s_pNext  : 노드의 다음 주소
	// s_poolID : 타입별 메모리 풀 마다 ID 부여할때 체크하기 위한 변수
	//////////////////////////////////////////////////////////////////////////
	struct Node
	{
		T        s_data;   
		Node*    s_pNext;  
		UINT64   s_poolD;
	};

private:

private:
	Node*                               m_pTopNode;
	MYBOOL                              m_bPlacementNew;
	UINT                                m_iCapacity;
	UINT                                m_iUseCnt;
	UINT                                m_iTopCnt;
	UINT64                              m_iOriginID;


public:
	//////////////////////////////////////////////////////////////////////////
    // 생성자, 파괴자.
    //
    // Parameters:	(int) 초기 블럭 개수.
    //				(bool) Alloc 시 생성자 / Free 시 파괴자 호출 여부
    // Return:
    //////////////////////////////////////////////////////////////////////////

	CMemoryPool(int iBlockNum = 0, bool bPlacementNew = false) : m_bPlacementNew(bPlacementNew)
	{
		//메모리 풀 ID 설정. 
		m_iOriginID = (UINT64)& m_pTopNode;

		//멤버 변수 초기화
		m_pTopNode = nullptr;
		m_iUseCnt = 0;
		m_iTopCnt = 0;



		for (int i = 0; i < iBlockNum; i++)
		{
			PushBack();
		}

	}

	~CMemoryPool()
	{
		// Free에서 소멸자 호출 안했으니 소멸자 호출해주고 메모리 풀 노드 지우기

		Node* tempTop;
		Node* newTop;
		Node* realTop = (Node*)((UINT64)m_pTopNode & BITMASK);

		while (realTop != nullptr)
		{

			newTop = realTop->s_pNext;

			if (m_bPlacementNew == false)
			{
				//소멸자 호출
				(realTop->s_data).~T();
			}

			free(realTop);

			realTop = newTop;
		}
	}

	void PushBack()
	{
		//노드 생성 및 초기화
		Node* newNode = (Node*)malloc(sizeof(Node));
		newNode->s_pNext = nullptr;
		newNode->s_poolD = m_iOriginID;

		//객체 생성자 호출
		new(&newNode->s_data) T;

		//기존 TopNode에 연결
		newNode->s_pNext = m_pTopNode;

		m_pTopNode = newNode;

		m_iCapacity++;
	}
	void PushBack(int Num)
	{
		Node* t;
		Node* real;
		UINT64 retCnt;
		UINT64 bitmask = 0x00007FFFFFFFFFFF;
		uint64_t index;



		for (int i = 0; i < Num; i++)
		{
			//노드 생성 및 초기화
			Node* newNode = (Node*)malloc(sizeof(Node));
			newNode->s_poolD = m_iOriginID;

			//객체 생성자 호출
			new(&newNode->s_data) T;

			retCnt = InterlockedIncrement(&m_iTopCnt);

			do {
				//CAS 실패하면 newNode에 붙인 tag때기
				newNode = (Node*)(((UINT64)newNode << 17) >> 17);

				t = m_pTopNode;
				real = (Node*)((UINT64)t & BITMASK);
				newNode->s_pNext = real;

				newNode = (Node*)((UINT64)newNode | (retCnt << 47));

			} while (InterlockedCompareExchange64((volatile __int64*)&m_pTopNode, (__int64)newNode, (__int64)t) != (__int64)t);

		}

		m_iCapacity += Num;
		wprintf(L"MemoryPool Capacity Up : %d \n", m_iCapacity);
	}

	//Alloc에서 아웃파라미터로 노드 포인터 줄 것임
	void CPushBack(Node** ppNode)
	{
		//노드 생성 및 초기화
		Node* newNode = (Node*)malloc(sizeof(Node));
		newNode->s_pNext = nullptr;
		newNode->s_poolD = m_iOriginID;

		//객체 생성자 호출
		new(&newNode->s_data) T;

		InterlockedIncrement(&m_iCapacity);
		InterlockedIncrement(&m_iUseCnt);

		if (newNode == nullptr)
			__debugbreak();

		//아웃 파라미터로 준 변수에 동적할당한 노드 주기
		*ppNode = newNode;

	}


	//////////////////////////////////////////////////////////////////////////
    // 현재 확보 된 블럭 개수를 얻는다. (메모리풀 내부의 전체 개수)
    //
    // Parameters: 없음.
    // Return: (int) 메모리 풀 내부 전체 개수
    //////////////////////////////////////////////////////////////////////////
	inline int GetCapacityCnt()
	{
		return m_iCapacity;
	}

	//////////////////////////////////////////////////////////////////////////
    // 현재 사용중인 블럭 개수를 얻는다.
    //
    // Parameters: 없음.
    // Return: (int) 사용중인 블럭 개수.
    //////////////////////////////////////////////////////////////////////////
	inline int GetUseCnt()
	{
		return m_iUseCnt;
	}


	//////////////////////////////////////////////////////////////////////////
	// 블럭 하나를 할당받는다.  
	//
	// Parameters: 없음.
	// Return: (DATA *) 데이타 블럭 포인터.
	//////////////////////////////////////////////////////////////////////////
	T* Alloc()
	{
		DWORD curID = GetCurrentThreadId();
		UINT64 retCnt;
		Node* t;
		Node* real = nullptr;
		Node* newTopNode;
		UINT64 useCnt = m_iUseCnt;
		UINT64 Capacity = m_iCapacity;

		if (useCnt == Capacity)
		{
			CPushBack(&real);

			if (real == nullptr)
				__debugbreak();


			return &real->s_data;
		}

		retCnt = InterlockedIncrement(&m_iTopCnt);

		do{
			t = m_pTopNode;

			real = (Node*)((UINT64)t & BITMASK);
			if (real == nullptr)
			{
				CPushBack(&real);

				return &real->s_data;
			}

			newTopNode = real->s_pNext;
			newTopNode = (Node*)((UINT64)newTopNode | (retCnt << 47));

		}while(InterlockedCompareExchange64((volatile __int64*) & m_pTopNode, (__int64)newTopNode, (__int64)t) != (__int64)t);



		//기존 Top노드 메모리 풀과 분리했으니 추가 작업하던지 바로 반환
		if (m_bPlacementNew == true)
		{
			new(&(t->s_data)) T;
		}

		//어차피 노드 생성할 때 생성자 이미 1번 호출해서 false일때 생각할 필요 없음.

		InterlockedIncrement(&m_iUseCnt);
		
		if (&real->s_data == nullptr)
			__debugbreak();

		return &(real->s_data);
	}


	//////////////////////////////////////////////////////////////////////////
	// 사용중이던 블럭을 해제한다.
	//
	// Parameters: (DATA *) 블럭 포인터.
	// Return: (BOOL) TRUE, FALSE.
	//////////////////////////////////////////////////////////////////////////
	bool Free(T* pData)
	{
		//반환받은 객체 주소를 통해 실제 메모리 풀 노드 주소 구하기
		Node* newNode = (Node*)pData;

		//메모리 풀에 붙이기 전에 검사

		//todo : 가드 건드렸으면 메모리 누가 오염시키고 있는 것이니 바로 Crash

		//다른 메모리풀의 노드를 반환했을 때 그냥 false return하기
		if (newNode->s_poolD != m_iOriginID)
			return false;

		//bPlacementNew 체크해서 true 면 소멸자 호출해주기
		if (m_bPlacementNew == true)
			pData->~T();

		Node* t;
		Node* real;
		UINT64 retCnt = InterlockedIncrement(&m_iTopCnt);

		do {
			//CAS 실패하면 newNode에 붙인 tag때기
			newNode = (Node*)(((UINT64)newNode << 17) >> 17);

			t = m_pTopNode;
			real = (Node*)((UINT64)t & BITMASK);
			newNode->s_pNext = real;

			newNode = (Node*)((UINT64)newNode | (retCnt << 47));

		} while (InterlockedCompareExchange64((volatile __int64*) & m_pTopNode, (__int64)newNode, (__int64)t) != (__int64)t);


		InterlockedDecrement(&m_iUseCnt);

		return true;
	}



};

#endif
 