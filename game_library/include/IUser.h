#pragma once

class IUser
{
public:
	virtual ~IUser() = default;  // �����Ϸ��� ������ �Ҹ��� ���

public:
	UINT64 m_uniqID;             // ���� ���̺귯���� �����ϴ� ����key

};