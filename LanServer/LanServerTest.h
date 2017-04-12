#ifndef __LANSERVERTEST__H__
#define __LANSERVERTEST__H__

class CLanServerTest : public CLanServer
{
public:
	CLanServerTest();
	virtual ~CLanServerTest();

public:
	virtual void OnClientJoin(SESSION_INFO *pSessionInfo, __int64 ClientID);   // Accept �� ����ó�� �Ϸ� �� ȣ��.
	virtual void OnClientLeave(__int64 ClientID);   					// Disconnect �� ȣ��
	virtual bool OnConnectionRequest(WCHAR *ClientIP, int Port);		// accept ����

	virtual void OnRecv(__int64 ClientID, CNPacket *pPacket);			// ��Ŷ ���� �Ϸ� ��
	virtual void OnSend(__int64 ClientID, int sendsize);				// ��Ŷ �۽� �Ϸ� ��

	virtual void OnWorkerThreadBegin();								// ��Ŀ������ GQCS �ٷ� �ϴܿ��� ȣ��
	virtual void OnWorkerThreadEnd();								// ��Ŀ������ 1���� ���� ��

	virtual void OnError(int errorCode, WCHAR *errorString);
};

#endif