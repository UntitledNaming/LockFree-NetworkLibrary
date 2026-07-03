#pragma once

class IUser
{
public:
	virtual ~IUser() = default;  

public:
	UINT64 m_uniqID;             // 게임 라이브러리가 전달하는 세션key

};