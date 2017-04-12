#include <Windows.h>
#include <stdio.h>
#include <conio.h>

#include "Config.h"
#include "StreamQueue.h"
#include "NPacket.h"
#include "LanServer.h"
#include "LanServerTest.h"

CLanServerTest LanServer;

void main()
{
	char chControlKey;

	if (!LanServer.Start(SERVER_IP, SERVER_PORT, 1, false, 100))
		return;

	while (1)
	{
		chControlKey = _getch();
		if (chControlKey == 'q' || chControlKey == 'Q')
		{
			//------------------------------------------------
			// 종료처리
			//------------------------------------------------
			break;
		}
	}
}