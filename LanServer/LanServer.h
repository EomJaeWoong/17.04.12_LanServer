#ifndef __LANSERVER__H__
#define __LANSERVER__H__

//-------------------------------------------------------------------------------------
// 세션 정보 구조체
//-------------------------------------------------------------------------------------
typedef struct stSESSION_INFO
{
	SOCKET _socket;
	WCHAR _IP[16];
	int _iPort;

} SESSION_INFO;

//-------------------------------------------------------------------------------------
// 세션
//-------------------------------------------------------------------------------------
typedef struct stSESSION
{
	bool _bUsed;

	SESSION_INFO _SessionInfo;
	__int64 _iSessionID;

	OVERLAPPED _SendOverlapped;
	OVERLAPPED _RecvOverlapped;

	CAyaStreamSQ SendQ;
	CAyaStreamSQ RecvQ;

	BOOL _bSendFlag;
	LONG _lIOCount;
} SESSION;



class CLanServer
{
	enum
	{
		// 최대 쓰레드 수
		MAX_THREAD = 50,

		// 최대 접속자 수
		MAX_SESSION = 200,

		// WSABUF 갯수
		MAX_WSABUF = 100
	};
public :
	CLanServer();
	virtual ~CLanServer();

	bool Start(WCHAR *wOpenIP, int iPort, int iWorkerThdNum, BOOL bNagle, int iMaxConnection);
	void Stop();

	int GetClientCount();

protected :
	bool SendPacket(__int64 iSessionID, CNPacket *pPacket);

	//---------------------------------------------------------------------------------
	// OnClientJoin			-> 접속처리 완료 후 호출
	// OnConnectionLeave		-> Disconnect후 호출
	// OnConnectionRequest		-> Accept 직후 호출
	//		return false; 시 클라이언트 거부
	//		return true; 시 접속 허용
	// OnRecv				-> 패킷 수신 완료 후
	// OnSend				-> 패킷 송신 완료 후
	// OnWorkerThreadBegin		-> 워커스레드 GQCS 바로 하단에서 호출
	// OnWorkerThreadEnd		-> 워커스레드 1루프 끝
	// OnError				-> 에러 메시지
	//---------------------------------------------------------------------------------
	virtual void OnClientJoin(SESSION_INFO* pSessionInfo, __int64 iSessionID) = 0;
	virtual void OnClientLeave(__int64 iSessionID) = 0;
	virtual bool OnConnectionRequest(WCHAR *ClientIP, int Port) = 0;
		
	virtual void OnRecv(__int64 iSessionID, CNPacket *pPacket) = 0;
	virtual void OnSend(__int64 iSessionID, int sendsize) = 0;

	virtual void OnWorkerThreadBegin() = 0;
	virtual void OnWorkerThreadEnd() = 0;

	virtual void OnError(int errorcode, WCHAR* errorMsg) = 0;

private :
	static unsigned __stdcall WorkerThread(LPVOID workerArg);
	static unsigned __stdcall AcceptThread(LPVOID acceptArg);
	static unsigned __stdcall MonitorThread(LPVOID monitorArg);

	int WorkerThread_Update();
	int AcceptThread_Update();
	int MonitorThread_Update();

	void RecvPost(SESSION *pSession);
	BOOL SendPost(SESSION *pSession);

	void DisconnectSession(SOCKET socket);
	void DisconnectSession(SESSION *pSession);
	void DisconnectSession(__int64 iSessionID);

	void ReleaseSession(SESSION *pSession);
	void ReleaseSession(__int64 iSessionID);

public:
	//---------------------------------------------------------------------------------
	// 모니터링 변수들
	//---------------------------------------------------------------------------------
	int _AcceptCounter;
	int _AcceptTotalCounter;
	int _RecvPacketCounter;
	int _SendPacketCounter;

	int _AcceptTPS;
	int _AcceptTotalTPS;
	int _RecvPacketTPS;
	int _SendPacketTPS;

	int _iSessionCount;

protected:
	////////////////////////////////////////////////////////////////////////
	// IOCP Handle
	////////////////////////////////////////////////////////////////////////
	HANDLE hIOCP;

	////////////////////////////////////////////////////////////////////////
	// Thread Handle
	////////////////////////////////////////////////////////////////////////
	HANDLE hAcceptThread;
	HANDLE hWorkerThread[MAX_THREAD];
	HANDLE hMonitorThread;

	////////////////////////////////////////////////////////////////////////
	// listen socket
	////////////////////////////////////////////////////////////////////////
	SOCKET listen_sock;

	SESSION Session[MAX_SESSION];

	int _iWorkerThdNum;
	__int64 _iSessionID;

	bool _bShutdown;
};


#endif