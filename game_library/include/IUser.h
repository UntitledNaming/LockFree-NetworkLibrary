#pragma once

class IUser
{
public:
	virtual ~IUser() = default;  // 컴파일러가 생성한 소멸자 사용

public:
	UINT64 m_uniqID;             // 게임 라이브러리가 전달하는 세션key

};