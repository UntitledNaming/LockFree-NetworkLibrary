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
	// �޸� Ǯ���� ����� ��� ����ü
	// s_guard  : ��� �տ� �޸� ħ�� üũ�� ����, Free�Ҷ� üũ
	// s_data   : ��忡 ��ü ��ü�� ����
	// s_pNext  : ����� ���� �ּ�
	// s_poolID : Ÿ�Ժ� �޸� Ǯ ���� ID �ο��Ҷ� üũ�ϱ� ���� ����
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
    // ������, �ı���.
    //
    // Parameters:	(int) �ʱ� �� ����.
    //				(bool) Alloc �� ������ / Free �� �ı��� ȣ�� ����
    // Return:
    //////////////////////////////////////////////////////////////////////////

	CMemoryPool(int iBlockNum = 0, bool bPlacementNew = false) : m_bPlacementNew(bPlacementNew)
	{
		//�޸� Ǯ ID ����. 
		m_iOriginID = (UINT64)& m_pTopNode;

		//��� ���� �ʱ�ȭ
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
		// Free���� �Ҹ��� ȣ�� �������� �Ҹ��� ȣ�����ְ� �޸� Ǯ ��� �����
		Node* newTop;
		Node* realTop = (Node*)((UINT64)m_pTopNode & BITMASK);

		while (realTop != nullptr)
		{

			newTop = realTop->s_pNext;

			if (m_bPlacementNew == false)
			{
				//�Ҹ��� ȣ��
				(realTop->s_data).~T();
			}

			free(realTop);

			realTop = newTop;
		}
	}

	void PushBack()
	{
		//��� ���� �� �ʱ�ȭ
		Node* newNode = (Node*)malloc(sizeof(Node));
		newNode->s_pNext = nullptr;
		newNode->s_poolD = m_iOriginID;

		//��ü ������ ȣ��
		new(&newNode->s_data) T;

		//���� TopNode�� ����
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
			//��� ���� �� �ʱ�ȭ
			Node* newNode = (Node*)malloc(sizeof(Node));
			newNode->s_poolD = m_iOriginID;

			//��ü ������ ȣ��
			new(&newNode->s_data) T;

			retCnt = InterlockedIncrement(&m_iTopCnt);

			do {
				//CAS �����ϸ� newNode�� ���� tag����
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

	//Alloc���� �ƿ��Ķ���ͷ� ��� ������ �� ����
	void CPushBack(Node** ppNode)
	{
		//��� ���� �� �ʱ�ȭ
		Node* newNode = (Node*)malloc(sizeof(Node));
		newNode->s_pNext = nullptr;
		newNode->s_poolD = m_iOriginID;

		//��ü ������ ȣ��
		new(&newNode->s_data) T;

		InterlockedIncrement(&m_iCapacity);
		InterlockedIncrement(&m_iUseCnt);

		if (newNode == nullptr)
			__debugbreak();

		//�ƿ� �Ķ���ͷ� �� ������ �����Ҵ��� ��� �ֱ�
		*ppNode = newNode;

	}


	//////////////////////////////////////////////////////////////////////////
    // ���� Ȯ�� �� �� ������ ��´�. (�޸�Ǯ ������ ��ü ����)
    //
    // Parameters: ����.
    // Return: (int) �޸� Ǯ ���� ��ü ����
    //////////////////////////////////////////////////////////////////////////
	inline int GetCapacityCnt()
	{
		return m_iCapacity;
	}

	//////////////////////////////////////////////////////////////////////////
    // ���� ������� �� ������ ��´�.
    //
    // Parameters: ����.
    // Return: (int) ������� �� ����.
    //////////////////////////////////////////////////////////////////////////
	inline int GetUseCnt()
	{
		return m_iUseCnt;
	}


	//////////////////////////////////////////////////////////////////////////
	// �� �ϳ��� �Ҵ�޴´�.  
	//
	// Parameters: ����.
	// Return: (DATA *) ����Ÿ �� ������.
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



		//���� Top��� �޸� Ǯ�� �и������� �߰� �۾��ϴ��� �ٷ� ��ȯ
		if (m_bPlacementNew == true)
		{
			new(&(t->s_data)) T;
		}

		//������ ��� ������ �� ������ �̹� 1�� ȣ���ؼ� false�϶� ������ �ʿ� ����.

		InterlockedIncrement(&m_iUseCnt);
		
		return &(real->s_data);
	}


	//////////////////////////////////////////////////////////////////////////
	// ������̴� ���� �����Ѵ�.
	//
	// Parameters: (DATA *) �� ������.
	// Return: (BOOL) TRUE, FALSE.
	//////////////////////////////////////////////////////////////////////////
	bool Free(T* pData)
	{
		//��ȯ���� ��ü �ּҸ� ���� ���� �޸� Ǯ ��� �ּ� ���ϱ�
		Node* newNode = (Node*)pData;

		//�޸� Ǯ�� ���̱� ���� �˻�

		//todo : ���� �ǵ������ �޸� ���� ������Ű�� �ִ� ���̴� �ٷ� Crash

		//�ٸ� �޸�Ǯ�� ��带 ��ȯ���� �� �׳� false return�ϱ�
		if (newNode->s_poolD != m_iOriginID)
			return false;

		//bPlacementNew üũ�ؼ� true �� �Ҹ��� ȣ�����ֱ�
		if (m_bPlacementNew == true)
			pData->~T();

		Node* t;
		Node* real;
		UINT64 retCnt = InterlockedIncrement(&m_iTopCnt);

		do {
			//CAS �����ϸ� newNode�� ���� tag����
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
 