#pragma once

template<typename T>
struct Node
{

	T _data;
	Node* _Next;
	bool m_CallCD;
	unsigned long _PoolID;
};


template <typename T>
class iterator
{
public:
	iterator()
	{
		_node = nullptr;
	}

	iterator(const iterator& ref)
	{
		_node = ref._node;
	}

	iterator(Node<T>* ref)
	{
		_node = ref;
	}

	const iterator operator++(int)
	{
		iterator<T> temp = *this;
		_node = _node->_Next;
		return temp;
	}

	iterator& operator++()
	{
		_node = _node->_Next;

		return *this;
	}

	const iterator operator--(int)
	{
		iterator<T> temp = *this;
		_node = _node->_Prev;
		return temp;
	}

	iterator& operator--()
	{
		_node = _node->_Prev;

		return *this;
	}

	T& operator*()
	{
		return _node->_data;
	}

	void operator=(const iterator& it)
	{
		_node = it._node;
	}


	bool operator==(const iterator& other)
	{
		return _node == other._node;
	}
	bool operator!=(const iterator& other)
	{
		return _node != other._node;
	}
public:
	Node<T>* _node;
};


template<typename T>
class List
{
public:
public:
	using iterator = iterator<T>;
	using Node = Node<T>;
	iterator begin()
	{
		iterator it = _head->_Next;
		return it;
	}

	iterator end()
	{
		iterator it = _tail;

		return it;
	}

	iterator erase(iterator it)
	{
		Node* prevNode = it._node->_Prev;

		Node* nextNode = it._node->_Next;

		prevNode->_Next = nextNode;
		nextNode->_Prev = prevNode;

		//노드 지우기
		delete it._node;

		//다음 노드 가리키는 iterator 반환
		iterator ret = nextNode;
		_size--;

		return ret;
	}
	List()
	{
		//Dummy 노드 생성
		_head = new Node;
		_tail = new Node;
		_head->_Prev = nullptr;
		_tail->_Next = nullptr;

		//헤드, 테일 노드 연결
		_head->_Next = _tail;
		_tail->_Prev = _head;


	}
	~List()
	{
		if (_size > 0)
		{
			//리스트에 남아있는 원소 삭제
			clear();

		}

		//헤드, 테일 노드 삭제
		delete _head;
		delete _tail;
	}
	
	void push_back(T data)
	{
		Node* newNode = new Node;
		newNode->_data = data;

		Node* prevNode = _tail->_Prev;

		_tail->_Prev = newNode;
		newNode->_Next = _tail;

		newNode->_Prev = prevNode;
		prevNode->_Next = newNode;

		_size++;
	}

	void push_front(T data)
	{
		Node* newNode = new Node;
		newNode->_data = data;

		Node* nextNode = _head->_Next;

		_head->_Next = newNode;
		newNode->_Prev = _head;

		newNode->_Next = nextNode;
		nextNode->_Prev = newNode;

		_size++;
	}

	void pop_front()
	{
		erase(begin());
	}

	void pop_back()
	{
		iterator it = end();
		--it;
		erase(it);
	}

	void remove(T Data)
	{
		for (iterator it = begin(); it != end();++it)
		{
			//iterator로 리스트 순회하면서 인자로 받은 Data 찾기
			if (*it == Data)
			{
				erase(it);
				break;
			}

		}
	}

	iterator search(T Data)
	{
		for (iterator it = begin(); it != end(); ++it)
		{
			//iterator로 리스트 순회하면서 인자로 받은 Data 찾기
			if (*it == Data)
			{
				return it;
			}

		}
	}

	void clear()
	{
		iterator it = begin();
		while(_size > 0)
		{
			it = erase(it);
		}
	}
	

	int size() { return _size; }

	bool empty() { return _size == 0; }


private:
	
	int _size = 0;
	Node* _head;
	Node* _tail;
};