# 코드 의도 문서 (Code Intent) — P1 IOCP 네트워크 라이브러리

> 이 문서는 저장소의 코드 파일(.h/.cpp)을 "무엇을 하는가"가 아니라 **"왜 이렇게 작성했는가"** 중심으로 설명한다.
> 동작 흐름은 `docs/Code_Flow.md`, 설계 의도는 `docs/Design.md`, 문제 추적은 `docs/Troubleshooting.md` 참고.
> 서드파티(cpp_redis/tacopie/mysql)와 Redis 실행 파일은 설명 대상에서 제외한다.

---

## 0. 저장소 구조 자체의 의도

| 폴더 | 의도 |
|---|---|
| `network_library/` | 코어. `CLanServer`(내부망)/`CNetServer`(외부망)/`CLanClient`(내부망 클라)로, 신뢰 경계에 따라 헤더·암호화가 다른 판을 분리 |
| `game_library/` | 코어 위에 "컨텐츠 직렬 처리 보장"을 얹은 확장판. P4 MMORPG 서버가 이걸 상속 |
| `common_files/` | 버퍼·풀·락프리·유틸 등 모든 서버가 공유하는 부품. P2에서 검증한 자료구조가 여기로 들어옴 |
| `servers/` | 검증용 서버 5종(채팅 싱글/멀티/인증, 로그인, 모니터링, 게임(에코)). 라이브러리를 서로 다른 부하 패턴으로 검증하기 위한 구성 |
| `clients/`, `dummy/` | 모니터링 클라이언트와 부하 더미(더미는 EXE·로그만 보존, 소스 미포함) |
| `protocol/` | 서버 간 공용 프로토콜 정의를 한 곳으로 |

---

## 1. network_library — 코어

### 1.1 `CNetServer.h/.cpp` (외부망 서버 코어)

시작/구동:

| 함수 | 하는 일 | 이렇게 작성한 의도 |
|---|---|---|
| `Start` | Mem_Init → Net_Init → Thread_Create | 메모리·자료구조가 준비되기 전에 네트워크가 열리면 안 되므로 순서를 함수 이름으로 고정 |
| `Mem_Init` | IOCP 생성(concurrent=러닝 스레드 수), 세션 배열/인덱스 LFStack 초기화 | concurrent 값을 워커 수와 분리 지정 — 블로킹 시 대기 스레드가 이어받는 IOCP의 이점을 살리기 위한 명시적 튜닝 |
| `Net_Init` | WSAStartup/socket/bind/listen(backlog=`SOMAXCONN`) | 소켓 준비만 담당. listen backlog는 접속 폭주 대응 |
| `Thread_Create` / `Thread_Destroy` | 워커 N + Accept 1 + (옵션) Send 1 생성/join | Send 스레드를 플래그로 옵션화 — 즉시 송신판과 프레임 송신판을 같은 코어에서 선택 가능하게(`Design.md`) |
| `Stop` | 리슨 소켓부터 닫고 전 세션 Disconnect → IOCP 닫기 → 스레드 정리 | 새 유입 차단 → 기존 정리 → 인프라 해체의 종료 순서를 강제 |

Accept/수신:

| 함수 | 하는 일 | 이렇게 작성한 의도 |
|---|---|---|
| `AcceptThread` | accept → max 검사 → `OnConnectionRequest` → 소켓 옵션(SNDBUF=0/nagle/linger) → 인덱스 Pop → 세션ID 발급 → Init → IOCP 등록 → `OnClientJoin` → RecvPost → Release | SNDBUF=0은 커널 송신 버퍼를 거치지 않는 중첩 송신 설계, linger(RST)는 TIME_WAIT 누적을 피하려는 서버 측 종료 정책. 마지막 Release는 "세션을 쓰면 반드시 반납"의 규칙을 Accept 스레드에도 동일 적용 |
| `WorkerThread` | GQCS 루프. pOverlapped 값으로 RELEASE(PQCS)/Recv 완료/Send 완료 분기 | PQCS의 통지 값(작은 정수)은 주소 대역과 겹치지 않는다는 사실을 이용해, 별도 타입 필드 없이 Overlapped 포인터 하나로 분기 |
| `RecvIOProc` | writePos 이동 → 루프: 헤더 Peek → 패킷 코드/길이 검증 → 체크섬+페이로드를 CMessage로 복사 → 디코딩 → 체크섬 검증 → `OnRecv` → 반납 → RecvPost 재등록 | 검증 실패를 두 종류로 구분: 상대방 이상(연결 끊기) vs 서버 로직 모순(크래시로 격리). "조용히 넘어가는 서버"를 만들지 않으려는 방침 |
| `RecvPost` | 수신 링버퍼의 write 구간을 WSABUF 2개(wrap 대응)로 세팅해 WSARecv 1건 등록 | 링버퍼 경계에서 복사 없이 받기 위해 두 구간을 그대로 커널에 넘긴다. Recv 1건 제한은 메시지 처리 순서 보장 때문 |

송신:

| 함수 | 하는 일 | 이렇게 작성한 의도 |
|---|---|---|
| `SendPacket` | `SessionInvalid`로 세션 확보 → SendQ 상한 검사(초과 시 끊기) → 헤더 작성+인코딩(미인코딩 시) → AddRef 후 SendQ Enqueue → (즉시판) SendPost → Release | SendQ 상한 초과 = 상대가 안 받는 상태이므로 버퍼링을 계속하지 않고 끊는다. AddRef는 같은 메시지를 여러 세션에 보낼 때 버퍼 1개를 공유하기 위한 수명 규칙 |
| `SendPost` | SendFlag 획득 경쟁 → size 0이면 플래그 반납 후 **재확인** → SendQ에서 꺼내 WSABUF 배열 → WSASend 1건 | 세션당 WSASend 1건 보장 장치. 1차 size 확인과 플래그 반납 사이의 틈에 다른 스레드가 Enqueue만 하고 나갈 수 있어, 반납 후 2차 확인 → 있으면 플래그 재획득 경쟁으로 되돌아간다 |
| `SendIOProc` | 보낸 메시지 SubRef 반납 → SendFlag 해제 → 남은 게 있으면 SendPost | 완료 통지 시점이 버퍼 수명의 끝 — RefCnt 기반 수명의 반환 지점 |
| `SendThread` (옵션) | Sleep(SendFrame)마다 전 세션 순회, 세션 확보 후 SendPost | TCP 재전송 폭증을 프레임 버퍼링으로 누른 버전. 세션 확보(SessionInvalid)를 생략하면 확보 전 세션 교체로 빈 송신(ZeroByteSend)이 나올 수 있어 생략하지 않는다 |
| `SendPacketAll` | 전 세션 SendPacket | 브로드캐스트 편의 API |

세션 수명:

| 함수 | 하는 일 | 이렇게 작성한 의도 |
|---|---|---|
| `MakeSessionID` / `FindSession` | index(상위 16bit)<<48 \| allocID / ID>>48로 배열 인덱싱 | 조회 O(1) + 재활용 판별을 ID 하나에 담는다. 주소·핸들은 재사용되므로 KEY 부적격 |
| `SessionInvalid` | RefCnt 증가 → RelFlag 검사 → 세션ID 재검사, 실패 시 감소+Release | "세션을 쓰기 전 반드시 통과하는 관문"을 함수 하나로 묶어, 수명 규칙이 코드 전체에 흩어지지 않게 함 |
| `Release` | `InterlockedCompareExchange128`로 (RefCnt==0 && RelFlag==0) 확인과 RelFlag 세팅을 원자화 → 성공 시 PQCS로 RELEASE 통지 | 카운트 확인과 플래그 세팅 사이의 틈이 있으면 문제가 발생하므로 한 번의 CAS로 접합. 실제 정리를 PQCS로 워커에 위임하는 이유는 컨텐츠 락과의 데드락 차단. (시그니처의 `retIOCount` 인자는 과거 IO 카운트 기준의 잔재로 미사용 — 판정은 128bit CAS) |
| `ReleaseProc` | 미송신 메시지 반납 → `OnClientLeave` → closesocket → 인덱스 Push | closesocket을 이 시점까지 미루어서 핸들 재사용 오염 방지 |
| `Disconnect` | DCFlag 1회 설정 → CancelIoEx → Release | 걸린 IO를 기다리지 않고 취소해 실패 통지를 앞당김 — 빠른 정리 경로 |
| `Encoding` / `Decoding` | 고정키+랜덤키+위치 기반 XOR 계열 변환 | 민감정보 보호가 아니라 패킷 노출 방지 목적임을 구분(체크섬은 복호화 검증용) |
| `FindIP` | getpeername/InetNtop | 컨텐츠(로그인 라우팅)가 세션 IP를 얻는 통로 |

### 1.2 `CLanServer.h/.cpp`

CNetServer와 같은 골격에서 헤더가 `LANHEADER`(len만)이고 암호화/체크섬이 없다. 내부망은 신뢰 경계 안이므로 검증 비용을 지불하지 않는다는 판단. `SendPost`에서 `GetRealDataSize(1)`(LAN 헤더 크기 기준)를 호출하는 것이 두 판의 실질 차이가 드러나는 지점이다.

### 1.3 `CLanClient.h/.cpp`

내부망 클라이언트 코어(모니터링 클라이언트의 부모). 소켓 생성 → connect → IOCP 등록 → recv 등록.

### 1.4 `CSession.h/.cpp`

| 항목 | 의도 |
|---|---|
| `alignas(16) m_RefCnt + m_RelFlag` | 128bit CAS의 대상이 되는 두 값을 16바이트 경계의 연속 메모리에 배치 |
| `m_RecvQ`(링버퍼)/`m_SendQ`(LFQueueMul)/`m_SendArray` | 수신은 스트림 조립이라 짤린 메세지 담을 링버퍼, 송신은 다중 생산자(여러 스레드의 SendPacket)라 락프리 큐. SendArray는 WSASend 1회에 묶을 스냅샷 |
| `Init` | RecvQ/SendQ Clear, RefCnt=1, 플래그 0. **RelFlag 해제는 마지막** | 초기화 순서가 곧 안전성 — ID 변경 전에 플래그를 풀면 이전 ID로 접근하던 스레드가 관문을 통과한다 |

---


## 2. game_library — 컨텐츠 직렬 처리 확장

| 파일/함수 | 하는 일 | 이렇게 작성한 의도 |
|---|---|---|
| `CGameLibrary::Attach` | 그룹 객체 등록(배열 index=GroupID), 이름→ID 맵 구성, 프레임 시간/shared 플래그 세팅 | 컨텐츠 등록을 main 스레드 1회로 제한(멀티스레드 비안전을 문서화된 규약으로). 이름 맵은 사용자가 정수 ID를 관리하지 않게 하려는 편의 — 이후 조회는 insert 없는 find뿐이라 락 없이 접근(락의 내부 interlock 비용까지 회피) |
| `CGameLibrary::WorkerThread` | GQCS 분기: RELEASE / FRAME / **GROUPMOVE(en_GROUPMOVE, key=CMessage\*)** / Recv / Send | 코어의 PQCS 분기 체계를 그대로 확장. GroupMove 인자를 CMessage에 담아 key로 넘기는 것은 기존 직렬화 버퍼를 재사용 |
| `CGameLibrary::GroupMove` / `GroupMoveProc` | 세션 확보 → 그룹ID 조회 → PQCS 발사 / 워커에서 대상 그룹 `OnIUserMove` 호출 후 세션의 GroupID 변경 | 그 자리에서 콜백하면 그룹 락 중첩 데드락. 세션 GroupID 변경을 OnIUserMove **이후**로 미루는 이유: 먼저 바꾸면 새 그룹이 아직 안 온 유저의 메시지를 받는다 |
| `CGameLibrary::FrameThread` / `FrameProc` | 그룹별 (현재시간-OldTime)/프레임시간 몫만큼 PQCS 발사 / 워커가 그룹 락 걸고 `OnUpdate` | 프레임 스레드가 밀린 몫을 계산해 쏘고 OldTime 갱신도 프레임 스레드가 담당 |
| `CGameLibrary::RecvIOProc` | 메시지 추출 후 세션의 GroupID를 **지역에 복사**해 그 값으로 락/언락, shared 플래그에 따라 Shared/Exclusive | OnRecv 안에서 GroupMove가 일어나면 언락 대상이 바뀌는 버그 — 지역 복사로 차단 |
| `CGroup` | 순수가상 콜백(OnClientJoin/Leave/OnRecv/OnIUserMove/OnUpdate) + SRWLOCK + SendPacket 등 래핑 | 컨텐츠 제작자의 계약을 콜백 5개로 고정. 라이브러리 함수를 인라인 래핑해 사용자가 라이브러리 객체를 직접 만지지 않게 |
| `IUser` | m_uniqID(세션 key)만 가진 기반 인터페이스 | 라이브러리는 컨텐츠의 유저 타입을 모른 채 포인터만 그룹 간 운반 — 결합도 최소화 |
| `GameSession.h/.cpp` | 코어 CSession + `m_GroupID` | 세션이 자기 소속 그룹을 알아야 OnRecv 라우팅이 O(1) |

---

## 3. servers — 검증용 서버 5종

### 3.1 채팅 서버 3판 (`chatserver`, `chatserver_multi_ver`, `chatserver_auth_ver`)

같은 컨텐츠를 세 판으로 만든 것 자체가 의도다: **직렬화 전략의 비교 실험체.**

| 버전 | 의도 |
|---|---|
| 싱글버전 (`UpdateThread`+JobQ) | 컨텐츠 로직에서 동기화를 완전히 제거하는 구조의 기준판. OnRecv/OnClientJoin 등은 Job만 만들어 락프리 큐에 넣고, Update 스레드 1개가 순차 소비 — 컨텐츠 코드에 락이 하나도 없음을 보장 |
| 멀티버전 (SRWLock) | 유저/섹터 자료구조에 SRWLock(조회 shared/변형 exclusive)을 걸고 워커가 직접 처리 — 직렬화 비용 vs 락 비용을 같은 컨텐츠로 비교 |
| 인증버전 (Redis) | 로그인 서버가 저장한 토큰을 `get`→비교→`del`로 검증하는 판. **일회용 토큰**(사용 즉시 삭제)이라 토큰 탈취 재사용을 차단. 인증을 별도 스레드로 뺀 이유는 Redis 왕복이 Update 스레드를 막지 않게 하기 위해 |

공통 구현 의도: `NonUserMap`/`UserMap` 분리(미인증 세션을 3초 타임아웃으로 격리), 유저 조회는 세션ID 해시(find 지배 부하 조건), `SendPakcet_SectorAround`는 주변 9섹터 전파(P4 AOI의 원형).

### 3.2 로그인 서버 (`loginserver/LoginServerLive`)

| 함수 | 의도 |
|---|---|
| `LoginRequest` | accountNo/sessionKey 추출 → 형식 검증 → `FindIP`+`FindServerInfo`로 응답할 서버 주소 결정 → `GetDBData`(MySQL에서 ID/닉 조회) → `SetRedisToken` → 응답. 처리 시간을 측정해 임계 초과 로그 | stateless 처리의 전형을 그대로 코드로: 요청 안에서 조회→저장→응답을 끝낸다. 처리 시간 로그는 "로그인이 느려지면 DB부터 본다"는 운영 관점의 계측 |
| `SetRedisToken` | `set` + `sync_commit`(+만료) | 동기 커밋으로 "저장 확인 후 클라이언트에게 응답" 보장 |
| `GetDBData` | DBTLS 경유 | 워커별 커넥션으로 병렬 조회(플랫폼 통신을 DB 조회로 대체한 테스트 구성) |
| `ServerRoutInfo_Init` / `FindServerInfo` | Config의 더미 IP 대역→응답할 채팅/게임 서버 주소 매핑, 조회는 shared 락 | 더미(사설망)와 실유저(공인망)에게 다른 주소를 줘야 하는 문제를 Config 기반 라우팅 테이블로 해결. 못 찾으면 끊기(서버 다운 아님) |
| `NonUserTimeOut`/`UserTimeOut` | 미요청/미종료 세션 정리 | 클라이언트가 먼저 끊는 정책(RST 유실 대비)의 보완 장치 |

### 3.3 게임(에코) 서버 (`gameserver/GameLibraryTestServer_Live`)

게임 라이브러리의 검증체. `Main.cpp`가 CAuth/CEcho/CMonitor 3그룹을 Attach — 그룹 이동·프레임·직렬 처리가 모두 동원되는 최소 구성.

| 파일 | 의도 |
|---|---|
| `CAuth.cpp` | 0번 그룹(모든 접속의 시작). `LoginRequsetProc`이 형식 검증 → NonUser 제거 → 유저 생성 → `GroupMove("Echo")`. 이동 실패(그 사이 세션 무효화) 시 유저 반납 — 그룹 이동의 실패 경로까지 검증. `OnIUserMove`에 `__debugbreak`: 인증 그룹으로 유저가 "이동해 오는" 것은 서버 로직 모순이므로 즉사시킴. **주의: 이 판의 인증은 형식 검증까지이며 Redis 토큰 대조는 없음(토큰 검증은 인증 채팅 서버가 담당)** |
| `CEcho.cpp` | 이동해 온 유저를 Map에 넣고 그 시점에 로그인 응답 전송 — 응답을 받은 더미가 즉시 에코를 쏘기 때문에, 에코 유저 자료구조에 삽입 전에 응답이 나가면 에코에 "없는 유저의 메시지"가 오게 됨 |
| `CMonitor.cpp` | 1초 프레임 그룹으로 구현한 모니터링 — 모니터링을 특별한 전역 장치가 아니라 "1초짜리 컨텐츠"로 취급해 구조를 단순화. `GetGroupPtr`로 타 그룹 지표를 모아 전송 |
| `CUser.cpp` | IUser 상속 + static 풀 | 유저 생성 그룹(인증)과 반납 그룹(에코 등)이 달라 풀을 전역(static)으로 |

### 3.4 모니터링 서버 (`monitoringserver`) + `clients/CMonitorClient`

| 파일 | 의도 |
|---|---|
| `CMonitor(서버)` | CLanServer 기반 — 내부망 전제라 암호화 없는 판을 쓰는 것 자체가 Lan/Net 분리의 실사용 |
| `CAgent`/`CMonAgentsMgr` | 모니터링 서버가 집계하는 데이터를 화면에 출력하는 에이전트 클라이언트와 그 에이전트 클라이언트를 관리하는 클래스 |
| `CMonitorClient` | `SendMonitorData(dataType, value)` 하나로 통일 |

### 3.5 `protocol/CommonProtocol.h`, `dummy/`

프로토콜 상수·패킷 타입을 서버·더미가 같은 헤더로 공유 — 불일치로 인한 버그를 컴파일 타임에 제거. 더미는 EXE·로그만 보존(소스 별도 — 확인 필요). 프로토콜의 AccountNo은 "서버가 올바른 유저에게 응답했는지"를 더미가 판정하기 위한 검증 필드다.
