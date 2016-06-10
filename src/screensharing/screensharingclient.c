/*
mediastreamer2 library - modular sound and video processing and streaming
Copyright (C) 2016  Simon MORLAT (simon.morlat@linphone.org)

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

#include "mediastreamer2/mediastream.h"
#include "private.h"
#include "screensharingclient.h"

#include <freerdp/freerdp.h>
#include <freerdp/client.h>
#include <winpr/wlog.h>

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <sys/types.h>
#include <sys/socket.h>
#ifndef _WIN32
	#include <netdb.h>
#endif

bool_t screensharing_client_test_server(ScreenStream *stream) {

	int sock;
	struct sockaddr_in *serverSockAddr;
	struct hostent *serverHostEnt = NULL;
	long hostAddr;
	int test = 0;

	if(stream->sockAddr == NULL) {
		serverSockAddr = malloc(sizeof(struct sockaddr_in));
		ZeroMemory(serverSockAddr,sizeof(*serverSockAddr));
		hostAddr = inet_addr(stream->addr_ip);

		if ((long)hostAddr != (long)-1)
			bcopy(&hostAddr,&(serverSockAddr->sin_addr),sizeof(hostAddr));
		else {
			serverHostEnt = gethostbyname(stream->addr_ip);

			if (serverHostEnt == NULL)
				return FALSE;

			bcopy(serverHostEnt->h_addr,&(serverSockAddr->sin_addr),serverHostEnt->h_length);
		}

		serverSockAddr->sin_port = htons(stream->tcp_port);
		serverSockAddr->sin_family = AF_INET;
		stream->sockAddr = serverSockAddr;
	}

	if ((sock = socket(AF_INET,SOCK_STREAM,0)) < 0)
		return FALSE;

	test=connect(sock,(struct sockaddr *)stream->sockAddr,sizeof(*serverSockAddr));

	close(sock);

	return (test != -1);
}

void screensharing_client_iterate(ScreenStream* stream) {
	switch(stream->state){
		case MSScreenSharingConnecting:
			ms_message("Screensharing Client: Test server connection");
			if (stream->timer == NULL) {
				stream->timer = malloc(sizeof(MSTimeSpec));
				clock_start(stream->timer);
			}
			// Time out verification
			if (!clock_elapsed(stream->timer, stream->time_out)) {
				if (screensharing_client_test_server(stream))
					screensharing_client_start(stream);
			} else
				stream->state = MSScreenSharingError;
			break;
		case MSScreenSharingStreamRunning:
			//TODO handle error
			break;
		case MSScreenSharingInactive:
		case MSScreenSharingWaiting:
		case MSScreenSharingError:
		default:
			break;
	}
}

void screensharing_client_free(ScreenStream *stream) {
	ms_message("Screensharing Client: Free client");
	if(stream->client != NULL)
		freerdp_client_context_free(stream->client);
	if(stream->sockAddr != NULL) {
		free(stream->sockAddr);
		stream->sockAddr = NULL;
	}
	if(stream->timer != NULL) {
		free(stream->timer);
		stream->timer = NULL;
	}
}

extern int RdpClientEntry(RDP_CLIENT_ENTRY_POINTS* pEntryPoints);

ScreenStream* screensharing_client_start(ScreenStream *stream) {
	RDP_CLIENT_ENTRY_POINTS clientEntryPoints;
	rdpContext* client;
	wLog* root;
	ms_message("Screensharing Client: Connecting on = %s; Port = %d",stream->addr_ip,stream->tcp_port);

	ZeroMemory(&clientEntryPoints, sizeof(RDP_CLIENT_ENTRY_POINTS));
	clientEntryPoints.Size = sizeof(RDP_CLIENT_ENTRY_POINTS);
	clientEntryPoints.Version = RDP_CLIENT_INTERFACE_VERSION;

	RdpClientEntry(&clientEntryPoints);

	client = freerdp_client_context_new(&clientEntryPoints);
	if (!client) {
		ms_message("Screensharing Client: Fail new client");
		return stream;
	}
	
	ms_message("Screensharing Client: New client");

	stream->client = client;

	//TODO OPTIONS ?
	client->settings->ServerHostname = malloc(sizeof(char)*sizeof(stream->addr_ip));
	strncpy(client->settings->ServerHostname,stream->addr_ip,sizeof(stream->addr_ip));
	client->settings->ServerPort = stream->tcp_port;
	client->settings->SmartSizing = TRUE;
	client->settings->TlsSecurity = FALSE;
	client->settings->NlaSecurity = TRUE;
	client->settings->RdpSecurity = TRUE;
	client->settings->ExtSecurity = FALSE;
	client->settings->Authentication = FALSE;
	client->settings->AudioPlayback = FALSE;
	client->settings->RemoteConsoleAudio = FALSE;
	
	//Remove output log
	root = WLog_GetRoot();
	WLog_SetStringLogLevel(root, "OFF");

	if (freerdp_client_start(client) < 0) {
		ms_message("Screensharing Client: Fail to start");
		freerdp_client_context_free(client);
	}
	else stream->state = MSScreenSharingStreamRunning;

	ms_message("Screensharing Client: State = %d",stream->state);
	return stream;
}

void screensharing_client_stop(ScreenStream *stream) {
	ms_message("Screensharing Client: Stop client");
	if(stream->client != NULL)
		freerdp_client_stop(stream->client);
}