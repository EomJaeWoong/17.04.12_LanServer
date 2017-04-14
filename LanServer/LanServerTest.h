#ifndef __LANSERVERTEST__H__
#define __LANSERVERTEST__H__

class CLanServerTest : public CLanServer
{
public:
	CLanServerTest();
	virtual ~CLanServerTest();

public:
	virtual void OnClientJoin(SESSION_INFO *pSessionInfo, __int64 iSessionID);   // Accept 후 접속처리 완료 후 호출.
	virtual void OnClientLeave(__int64 iSessionID);   					// Disconnect 후 호출
	virtual bool OnConnectionRequest(WCHAR *SessionIP, int Port);		// accept 직후

	virtual void OnRecv(__int64 iSessionID, CNPacket *pPacket);			// 패킷 수신 완료 후
	virtual void OnSend(__int64 iSessionID, int sendsize);				// 패킷 송신 완료 후

	virtual void OnWorkerThreadBegin();								// 워커스레드 GQCS 바로 하단에서 호출
	virtual void OnWorkerThreadEnd();								// 워커스레드 1루프 종료 후

	virtual void OnError(int errorCode, WCHAR *errorString);

};

#endif