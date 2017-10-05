/*
 * This source code is provided under the Apache 2.0 license and is provided
 * AS IS with no warranty or guarantee of fit for purpose.  See the project's 
 * LICENSE.md for details. 
 * Copyright Thomson Reuters 2015. All rights reserved.
*/


/*
 * This is the main file for the rsslVAProvider application.  It is a
 * single-threaded server application.  
 *
 * The main provider file provides the callback for channel events and 
 * the default callback for processing RsslMsgs. The main function
 * initializes the UPA Reactor, accepts incoming connections, and
 * dispatches for events.
 *
 * This application is intended as a basic usage example.  Some of the design choices
 * were made to favor simplicity and readability over performance.  This application 
 * is not intended to be used for measuring performance.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#include "rsslProvider.h"
#include "rsslLoginHandler.h"
#include "rsslDirectoryHandler.h"
#include "rsslDictionaryProvider.h"
#include "rsslItemHandler.h"
#include "rsslVASendMessage.h"

#include "rtr/rsslReactor.h"

static fd_set	readFds;
static fd_set	exceptFds;
RsslServer *rsslSrvr;
static char portNo[128];
static char serviceName[128];
static char traceOutputFile[128];
static RsslInt32 timeToRun = 300;
static time_t rsslProviderRuntime = 0;
static RsslBool runTimeExpired = RSSL_FALSE;
static RsslBool xmlTrace = RSSL_FALSE;

static RsslClientSessionInfo clientSessions[MAX_CLIENT_SESSIONS];

static RsslVACacheInfo cacheInfo;
static void initializeCache(RsslBool cacheOption);
static void uninitializeCache();
static void initializeCacheDictionary();

/* default port number */
static const char *defaultPortNo = "14002";
/* default service name */
static const char *defaultServiceName = "DIRECT_FEED";

void exitWithUsage()
{
	printf(	"Usage: -p <port number> -s <service name> -id <service ID> -runtime <seconds> [-cache]\n");
#ifdef _WIN32
		printf("\nPress Enter or Return key to exit application:");
		getchar();
#endif
	exit(-1);
}

RsslReactorCallbackRet channelEventCallback(RsslReactor *pReactor, RsslReactorChannel *pReactorChannel, RsslReactorChannelEvent *pChannelEvent)
{
	RsslBool clientSessionFound = RSSL_FALSE;
	RsslClientSessionInfo *pClientSessionInfo = (RsslClientSessionInfo*)pReactorChannel->userSpecPtr;
	

	switch(pChannelEvent->channelEventType)
	{
		case RSSL_RC_CET_CHANNEL_UP:
		{
			/* A channel that we have requested via rsslReactorAccept() has come up.  Set our
			 * file descriptor sets so we can be notified to start calling rsslReactorDispatch() for
			 * this channel. */
#ifdef _WIN32
			int rcvBfrSize = 65535;
			int sendBfrSize = 65535;
			RsslErrorInfo rsslErrorInfo;
#endif
			printf("Connection up!\n");
			pClientSessionInfo->clientChannel = pReactorChannel;
			printf("\nServer fd=%d: New client on Channel fd=%d\n",
					pReactorChannel->pRsslServer->socketId, pReactorChannel->socketId);
			FD_SET(pReactorChannel->socketId, &readFds);
			FD_SET(pReactorChannel->socketId, &exceptFds);

#ifdef _WIN32
			/* WINDOWS: change size of send/receive buffer since it's small by default */
			if (rsslReactorChannelIoctl(pReactorChannel, RSSL_SYSTEM_WRITE_BUFFERS, &sendBfrSize, &rsslErrorInfo) != RSSL_RET_SUCCESS)
			{
				printf("rsslReactorChannelIoctl(): failed <%s>\n", rsslErrorInfo.rsslError.text);
			}
			if (rsslReactorChannelIoctl(pReactorChannel, RSSL_SYSTEM_READ_BUFFERS, &rcvBfrSize, &rsslErrorInfo) != RSSL_RET_SUCCESS)
			{
				printf("rsslReactorChannelIoctl(): failed <%s>\n", rsslErrorInfo.rsslError.text);
			}
#endif
			if (xmlTrace) 
			{
				RsslErrorInfo rsslErrorInfo;
				RsslTraceOptions traceOptions;

				rsslClearTraceOptions(&traceOptions);
				traceOptions.traceMsgFileName = traceOutputFile;
				traceOptions.traceFlags |= RSSL_TRACE_TO_FILE_ENABLE | RSSL_TRACE_TO_STDOUT | RSSL_TRACE_TO_MULTIPLE_FILES | RSSL_TRACE_WRITE | RSSL_TRACE_READ;
				traceOptions.traceMsgMaxFileSize = 100000000;

				rsslReactorChannelIoctl(pReactorChannel, (RsslIoctlCodes)RSSL_TRACE, (void *)&traceOptions, &rsslErrorInfo);
			}
	
			return RSSL_RC_CRET_SUCCESS;
		}
		case RSSL_RC_CET_CHANNEL_READY:
			/* The channel has exchanged the messages necessary to setup the connection
			 * and is now ready for general use. For an RDM Provider, this normally immediately
			 * follows the CHANNEL_UP event. */
			return RSSL_RC_CRET_SUCCESS;
		case RSSL_RC_CET_FD_CHANGE:
			/* The file descriptor representing the RsslReactorChannel has been changed.
			 * Update our file descriptor sets. */
			printf("Fd change: %d to %d\n", pReactorChannel->oldSocketId, pReactorChannel->socketId);
			FD_CLR(pReactorChannel->oldSocketId, &readFds);
			FD_CLR(pReactorChannel->oldSocketId, &exceptFds);
			FD_SET(pReactorChannel->socketId, &readFds);
			FD_SET(pReactorChannel->socketId, &exceptFds);
			return RSSL_RC_CRET_SUCCESS;
		case RSSL_RC_CET_WARNING:
			/* We have received a warning event for this channel. Print the information and continue. */
			printf("Received warning for Channel fd=%d.\n", pReactorChannel->socketId);
			printf("	Error text: %s\n", pChannelEvent->pError->rsslError.text);
			return RSSL_RC_CRET_SUCCESS;
		case RSSL_RC_CET_CHANNEL_DOWN:
		{
			/* The channel has failed and has gone down.  Print the error, close the channel, and reconnect later. */
			printf("Connection down!\n");

			if (pChannelEvent->pError)
				printf("	Error text: %s\n", pChannelEvent->pError->rsslError.text);

			closeItemChnlStreams(pReactor, pReactorChannel);
			closeDictionaryChnlStreams(pReactorChannel);
			closeDirectoryStreamForChannel(pReactorChannel);
			closeLoginStreamForChannel(pReactorChannel);

			if(pClientSessionInfo->clientChannel == NULL)
			{
				pClientSessionInfo->clientChannel = pReactorChannel;
			}

			/* It is important to make sure that no more interface calls are made using the channel after
			 * calling rsslReactorCloseChannel(). Because this application is single-threaded, it is safe 
			 * to call it inside callback functions. */
			removeClientSessionForChannel(pReactor, pReactorChannel);

			return RSSL_RC_CRET_SUCCESS;
		}
		default:
			printf("Unknown channel event!\n");
			cleanUpAndExit();
	}

	return RSSL_RC_CRET_SUCCESS;

}

RsslReactor *pReactor = 0;

int main(int argc, char **argv)
{
	int i, j;
	struct timeval time_interval;
	fd_set useRead;
	fd_set useExcept;
	int selRet;
	char errTxt[256];
	RsslBuffer errorText = {255, (char*)errTxt};
	RsslInProgInfo inProg = RSSL_INIT_IN_PROG_INFO;
	RsslRet	retval = 0;
	int iargs;
	RsslErrorInfo rsslErrorInfo;
	RsslBool cacheOption = RSSL_FALSE;

	RsslReactorOMMProviderRole providerRole;

	RsslBindOptions sopts = RSSL_INIT_BIND_OPTS;

	RsslCreateReactorOptions reactorOpts;

	RsslReactorDispatchOptions dispatchOpts;

	time_t nextSendTime;

	/* Initialize RSSL. The locking mode RSSL_LOCK_GLOBAL_AND_CHANNEL is required to use the RsslReactor. */
	if (rsslInitialize(RSSL_LOCK_GLOBAL_AND_CHANNEL, &rsslErrorInfo.rsslError) != RSSL_RET_SUCCESS)
	{
		printf("rsslInitialize(): failed <%s>\n",rsslErrorInfo.rsslError.text);
		/* WINDOWS: wait for user to enter something before exiting  */
#ifdef _WIN32
		printf("\nPress Enter or Return key to exit application:");
		getchar();
#endif
		exit(RSSL_RET_FAILURE);
	}

	rsslClearOMMProviderRole(&providerRole);

	providerRole.base.channelEventCallback = channelEventCallback;
	providerRole.base.defaultMsgCallback = defaultMsgCallback;
	providerRole.loginMsgCallback = loginMsgCallback;
	providerRole.directoryMsgCallback = directoryMsgCallback;
	providerRole.dictionaryMsgCallback = dictionaryMsgCallback;
	providerRole.tunnelStreamListenerCallback = tunnelStreamListenerCallback;

	rsslClearCreateReactorOptions(&reactorOpts);

	if (!(pReactor = rsslCreateReactor(&reactorOpts, &rsslErrorInfo)))
	{
		printf("Reactor creation failed: %s\n", rsslErrorInfo.rsslError.text);
		cleanUpAndExit();
	}

	snprintf(portNo, 128, "%s", defaultPortNo);
	snprintf(serviceName, 128, "%s", defaultServiceName);
	setServiceId(1);
	for(iargs = 1; iargs < argc; ++iargs)
	{
		if (0 == strcmp("-p", argv[iargs]))
		{
			++iargs; if (iargs == argc) exitWithUsage();
			snprintf(portNo, 128, "%s", argv[iargs]);
		}
		else if (0 == strcmp("-s", argv[iargs]))
		{
			++iargs; if (iargs == argc) exitWithUsage();
			snprintf(serviceName, 128, "%s", argv[iargs]);
		}
		else if (0 == strcmp("-id", argv[iargs]))
		{
			long tmpId = 0;
			++iargs; if (iargs == argc) exitWithUsage();
			tmpId = atol(argv[iargs]);
			if (tmpId < 0)
			{
				printf("ServiceId must be positive.\n");
				exitWithUsage();
			}
			setServiceId(tmpId);
		}
		else if (0 == strcmp("-x", argv[iargs]))
		{
			xmlTrace = RSSL_TRUE;
			snprintf(traceOutputFile, 128, "RsslVAProvider\0");
		}
		else if (0 == strcmp("-runtime", argv[iargs]))
		{
			++iargs; if (iargs == argc) exitWithUsage();
			timeToRun = atoi(argv[iargs]);
		}
		else if (0 == strcmp("-cache", argv[iargs]))
		{
			cacheOption = RSSL_TRUE;
		}
		else
		{
			printf("Error: Unrecognized option: %s\n\n", argv[iargs]);
			exitWithUsage();
		}
	}
	printf("portNo: %s\n", portNo);
	printf("serviceName: %s\n", serviceName);
	printf("serviceId: %llu\n", getServiceId());

	/* Initialiize client session information */
	for (i = 0; i < MAX_CLIENT_SESSIONS; i++)
	{
		clearClientSessionInfo(&clientSessions[i]);
	}

	initializeCache(cacheOption);

	/* Initialize login handler */
	initLoginHandler();

	/* Initialize source directory handler */
	initDirectoryHandler();

	/* Initialize dictionary provider */
	initDictionaryProvider();

	/* Initialize market price handler */
	initItemHandler();

	/* Initialize symbol list item list */
	initSymbolListItemList();

	/* Initialize market by order items */
	initMarketByOrderItems();


	/* set service name in directory handler */
	setServiceName(serviceName);

	/* load dictionary */
	if (loadDictionary() != RSSL_RET_SUCCESS)
	{
		/* exit if dictionary cannot be loaded */
		/* WINDOWS: wait for user to enter something before exiting  */
#ifdef _WIN32
		printf("\nPress Enter or Return key to exit application:");
		getchar();
#endif
		exit(RSSL_RET_FAILURE);
	}

	initializeCacheDictionary();

	/* Initialize run-time */
	initRuntime();

	FD_ZERO(&readFds);
	FD_ZERO(&exceptFds);
	
	sopts.guaranteedOutputBuffers = 500;
	sopts.serviceName = portNo;
	sopts.majorVersion = RSSL_RWF_MAJOR_VERSION;
	sopts.minorVersion = RSSL_RWF_MINOR_VERSION;
	sopts.protocolType = RSSL_RWF_PROTOCOL_TYPE;

	/* Create the server. */
	if (!(rsslSrvr = rsslBind(&sopts, &rsslErrorInfo.rsslError)))
	{
		printf("Unable to bind RSSL server: <%s>\n",rsslErrorInfo.rsslError.text);
		/* WINDOWS: wait for user to enter something before exiting  */
#ifdef _WIN32
		printf("\nPress Enter or Return key to exit application:");
		getchar();
#endif
		exit(RSSL_RET_FAILURE);
	}

	FD_SET(rsslSrvr->socketId, &readFds);
	FD_SET(pReactor->eventFd, &readFds);
	
	rsslClearReactorDispatchOptions(&dispatchOpts);
	dispatchOpts.maxMessages = MAX_CLIENT_SESSIONS;

	// initialize next send time
	nextSendTime = time(NULL) + UPDATE_INTERVAL;

	/* this is the main loop */
	while(RSSL_TRUE)
	{
		useRead = readFds;
		useExcept = exceptFds;
		time_interval.tv_sec = UPDATE_INTERVAL;
		time_interval.tv_usec = 0;

		/* Call select() to check for any messages */
		selRet = select(FD_SETSIZE,&useRead,
		    NULL,&useExcept,&time_interval);

		if (selRet > 0)
		{
			RsslRet ret;

			/* Accept connection, if one is waiting */
			if (FD_ISSET(rsslSrvr->socketId, &useRead))
			{
				RsslClientSessionInfo *pClientSessionInfo = NULL;
				RsslReactorAcceptOptions aopts;

				rsslClearReactorAcceptOptions(&aopts);

				/* find an available client session */
				for (i = 0; i < MAX_CLIENT_SESSIONS; i++)
				{
					if (!clientSessions[i].isInUse)
					{
						pClientSessionInfo = &clientSessions[i];
						pClientSessionInfo->isInUse = RSSL_TRUE;
						break;
					}
				}

				/* Reject the channel if we are out of client sessions */
				if (!pClientSessionInfo)
					aopts.rsslAcceptOptions.nakMount = RSSL_TRUE;
				else
					aopts.rsslAcceptOptions.userSpecPtr = pClientSessionInfo;


				printf("Accepting new connection...\n");
				if (rsslReactorAccept(pReactor, rsslSrvr, &aopts, (RsslReactorChannelRole*)&providerRole, &rsslErrorInfo) != RSSL_RET_SUCCESS)
				{
					printf("rsslReactorAccept() failed: %s(%s)\n", rsslErrorInfo.rsslError.text, rsslErrorInfo.errorLocation);
					cleanUpAndExit();
				}
			}

			/* Call rsslReactorDispatch().  This will handle any events that have occurred on its channels.
			 * If there are events or messages for the application to process, they will be delivered
			 * through the callback functions given by the providerRole object. 
			 * A return value greater than RSSL_RET_SUCCESS indicates there may be more to process. */
			while ((ret = rsslReactorDispatch(pReactor, &dispatchOpts, &rsslErrorInfo)) > RSSL_RET_SUCCESS)
				;

			if (ret < RSSL_RET_SUCCESS)
			{
				printf("rsslReactorDispatch() failed: %s\n", rsslErrorInfo.rsslError.text);
				cleanUpAndExit();
			}
		}
		else if (selRet < 0)
		{
#ifdef _WIN32
			if (WSAGetLastError() == WSAEINTR)
				continue;
			printf("Error: select: %d\n", WSAGetLastError());
#else
			if (errno == EINTR)
				continue;
			perror("select");
#endif
			cleanUpAndExit();
		}

		// send any updates at next send time
		if (time(NULL) >= nextSendTime)
		{
			/* Send market price updates for each connected channel */
			updateItemInfo();
			for (i = 0; i < MAX_CLIENT_SESSIONS; i++)
			{
				if (clientSessions[i].clientChannel != NULL)
				{
					if (sendItemUpdates(pReactor, clientSessions[i].clientChannel) != RSSL_RET_SUCCESS)
					{
						removeClientSessionForChannel(pReactor, clientSessions[i].clientChannel);
					}

					// send any tunnel stream messages
					for (j = 0; j < MAX_TUNNEL_STREAMS; j++)
					{
						if (clientSessions[i].simpleTunnelMsgHandler[j].tunnelStreamHandler.pTunnelStream != NULL)
						{
							handleSimpleTunnelMsgHandler(pReactor, clientSessions[i].clientChannel, &clientSessions[i].simpleTunnelMsgHandler[j]);
						}
					}
				}
			}

			nextSendTime += UPDATE_INTERVAL;
		}

		/* Handle run-time */
		handleRuntime();
	}
}

/*
 * Initializes the run-time for the rsslProvider.
 */
static void initRuntime()
{
	time_t currentTime = 0;

	/* get current time */
	time(&currentTime);
	
	rsslProviderRuntime = currentTime + (time_t)timeToRun;
}

/*
 * Handles the run-time for the rsslProvider.  Sends close status
 * messages to all streams on all channels after run-time has elapsed.
 */
static void handleRuntime()
{
	int i, j;
	time_t currentTime = 0;
	RsslRet	retval = 0;

	/* get current time */
	time(&currentTime);

	if (currentTime >= rsslProviderRuntime)
	{
		RsslBool tunnelStreamsOpen = RSSL_FALSE;

		for (i = 0; i < MAX_CLIENT_SESSIONS; i++)
		{
			if (clientSessions[i].clientChannel != NULL)
			{
				/* If any tunnel streams are still open, wait for them to close before quitting. */
				for (j = 0; j < MAX_CLIENT_SESSIONS; j++)
				{
					if (clientSessions[i].simpleTunnelMsgHandler[j].tunnelStreamHandler.pTunnelStream != NULL)
				{
					tunnelStreamsOpen = RSSL_TRUE;
						
					if (!runTimeExpired)
							simpleTunnelMsgHandlerCloseStreams(&clientSessions[i].simpleTunnelMsgHandler[j]);
					else
						break;
				}
			}
		}
		}

		if (!runTimeExpired)
		{
			runTimeExpired = RSSL_TRUE;
			printf("\nrsslVAProvider run-time expired...\n");

			if (tunnelStreamsOpen)
				printf("Waiting for tunnel streams to close...\n");
		}

		if (!tunnelStreamsOpen || currentTime >= rsslProviderRuntime + 10)
		{
			if (currentTime >= rsslProviderRuntime + 10)
				printf("Tunnel streams still open after ten seconds, giving up.\n");

			/* send close status messages to all streams on all channels */
			for (i = 0; i < MAX_CLIENT_SESSIONS; i++)
			{
				if (clientSessions[i].clientChannel != NULL)
				{
					/* send close status messages to all item streams */
					sendItemCloseStatusMsgs(pReactor, clientSessions[i].clientChannel);

					/* send close status messages to dictionary streams */
					sendDictionaryCloseStatusMsgs(pReactor, clientSessions[i].clientChannel);

				}
			}

			cleanUpAndExit();
		}
	}
}

/*
 * Processes all RSSL messages that aren't processed by 
 * any domain-specific callback functions.  
 * Item requests are handled here.
 */
RsslReactorCallbackRet defaultMsgCallback(RsslReactor *pReactor, RsslReactorChannel *pReactorChannel, RsslMsgEvent *pRsslMsgEvent)
{
	RsslDecodeIterator dIter;
	RsslMsg *pRsslMsg = pRsslMsgEvent->pRsslMsg;

	if (!pRsslMsg)
	{
		RsslErrorInfo *pError = pRsslMsgEvent->pErrorInfo;
		printf("defaultMsgCallback() received error: %s(%s)\n", pError->rsslError.text, pError->errorLocation);
		removeClientSessionForChannel(pReactor, pReactorChannel);
		return RSSL_RC_CRET_SUCCESS;
	}
	
	/* clear decode iterator */
	rsslClearDecodeIterator(&dIter);
	rsslSetDecodeIteratorRWFVersion(&dIter, pReactorChannel->majorVersion, pReactorChannel->minorVersion);
	rsslSetDecodeIteratorBuffer(&dIter, &pRsslMsg->msgBase.encDataBody);

	switch ( pRsslMsg->msgBase.domainType )
	{
		case RSSL_DMT_MARKET_PRICE:
		case RSSL_DMT_MARKET_BY_ORDER:
		case RSSL_DMT_MARKET_BY_PRICE:
		case RSSL_DMT_YIELD_CURVE:
		case RSSL_DMT_SYMBOL_LIST:
			if (processItemRequest(pReactor, pReactorChannel, pRsslMsg, &dIter) != RSSL_RET_SUCCESS)
			{
				removeClientSessionForChannel(pReactor, pReactorChannel);
				return RSSL_RC_CRET_SUCCESS;
			}
			break;
		default:
			switch(pRsslMsg->msgBase.msgClass)
			{
				case RSSL_MC_REQUEST:
					if (sendNotSupportedStatus(pReactor, pReactorChannel, pRsslMsg) != RSSL_RET_SUCCESS)
					{
						removeClientSessionForChannel(pReactor, pReactorChannel);
						return RSSL_RC_CRET_SUCCESS;
					}
					break;

				case RSSL_MC_CLOSE:
					printf("Received Close message with StreamId %d and unsupported domain %u\n\n",
							pRsslMsg->msgBase.streamId, pRsslMsg->msgBase.domainType);
					break;

				default:
					printf("Received unhandled message with MsgClass %u, StreamId %d and unsupported domain %u\n\n",
							pRsslMsg->msgBase.msgClass, pRsslMsg->msgBase.streamId, pRsslMsg->msgBase.domainType);
					break;
			}
			break;
	}

	return RSSL_RC_CRET_SUCCESS;
}

RsslReactorCallbackRet tunnelStreamListenerCallback(RsslTunnelStreamRequestEvent *pEvent, RsslErrorInfo *pErrorInfo)
{
	RsslClientSessionInfo *pClientSessionInfo = (RsslClientSessionInfo*)pEvent->pReactorChannel->userSpecPtr;
	SimpleTunnelMsgHandler *pSimpleTunnelMsgHandler = NULL;
	int i;

	/* find simpleTunnelMsgHandler that's not in use */
	for (i = 0; i < MAX_TUNNEL_STREAMS; i++)
	{
		if (pClientSessionInfo->simpleTunnelMsgHandler[i].tunnelStreamHandler.tunnelStreamOpenRequested == RSSL_FALSE)
		{
			pSimpleTunnelMsgHandler = &pClientSessionInfo->simpleTunnelMsgHandler[i];
			break;
		}
	}

	simpleTunnelMsgHandlerProcessNewStream(pSimpleTunnelMsgHandler, pEvent);
	
	return RSSL_RC_CRET_SUCCESS;
}

/*
 * Removes a client session.
 * clientSessionInfo - The client session to be removed
 */
static void removeClientSession(RsslClientSessionInfo* clientSessionInfo)
{
	clearClientSessionInfo(clientSessionInfo);
}

/*
 * Removes a client session for a channel.
 * pReactorChannel - The channel to remove the client session for
 */
void removeClientSessionForChannel(RsslReactor *pReactor, RsslReactorChannel *pReactorChannel)
{
	RsslErrorInfo rsslErrorInfo;
	int i;

	for (i = 0; i < MAX_CLIENT_SESSIONS; i++)
	{
		if (clientSessions[i].clientChannel == pReactorChannel)
		{
			removeClientSession(&clientSessions[i]);
			break;
		}
	}

	FD_CLR(pReactorChannel->socketId, &readFds);
	FD_CLR(pReactorChannel->socketId, &exceptFds);


	if (rsslReactorCloseChannel(pReactor, pReactorChannel, &rsslErrorInfo) != RSSL_RET_SUCCESS)
	{
		printf("rsslReactorCloseChannel() failed: %s\n", rsslErrorInfo.rsslError.text);
		cleanUpAndExit();
	}
}

/*
 * Initialization for cache use
 */
static void initializeCache(RsslBool cacheOption)
{
	RsslRet ret;
	if (cacheOption)
	{
		if ((ret = rsslPayloadCacheInitialize()) != RSSL_RET_SUCCESS)
		{
			printf("rsslPayloadCacheInitialize() failed: %d (%s). Cache will be disabled.", ret, rsslRetCodeToString(ret));
			cacheInfo.useCache = RSSL_FALSE;
			return;
		}

		cacheInfo.useCache = RSSL_TRUE;
		cacheInfo.cacheOptions.maxItems = 10000;
		rsslCacheErrorClear(&cacheInfo.cacheErrorInfo);
		snprintf(cacheInfo.cacheDictionaryKey, sizeof(cacheInfo.cacheDictionaryKey), "cacheDictionary%d", 1);

		cacheInfo.cacheHandle = rsslPayloadCacheCreate(&cacheInfo.cacheOptions,
				&cacheInfo.cacheErrorInfo);
		if (cacheInfo.cacheHandle == 0)
		{
			printf("Error: Failed to create cache.\n\tError (%d): %s\n",
					cacheInfo.cacheErrorInfo.rsslErrorId, cacheInfo.cacheErrorInfo.text);
			cacheInfo.useCache = RSSL_FALSE;
		}

		cacheInfo.cursorHandle = rsslPayloadCursorCreate();
		if (cacheInfo.cursorHandle == 0)
		{
			printf("Error: Failed to create cache cursor.\n");
			cacheInfo.useCache = RSSL_FALSE;
		}
	}
	else
	{
		cacheInfo.useCache = RSSL_FALSE;
		cacheInfo.cacheHandle = 0;
		cacheInfo.cursorHandle = 0;
	}
}

/*
 * Bind the application dictionary to the cache
 */
static void initializeCacheDictionary()
{
	RsslDataDictionary *dictionary;
	if (cacheInfo.useCache && cacheInfo.cacheHandle)
	{
		dictionary = getDictionary();
		if (dictionary)
		{
			if (rsslPayloadCacheSetDictionary(cacheInfo.cacheHandle, dictionary, cacheInfo.cacheDictionaryKey, &cacheInfo.cacheErrorInfo)
					!= RSSL_RET_SUCCESS)
			{
				printf("Error: Failed to bind RDM Field dictionary to cache.\n\tError (%d): %s\n",
						cacheInfo.cacheErrorInfo.rsslErrorId, cacheInfo.cacheErrorInfo.text);
				cacheInfo.useCache = RSSL_FALSE;
			}
		}
		else
		{
			printf("Error: No RDM Field dictionary for cache.\n");
			cacheInfo.useCache = RSSL_FALSE;
		}
	}
}

/*
 * cleanup cache prior to shutdown
 */
static void uninitializeCache()
{

	if (cacheInfo.cacheHandle)
		rsslPayloadCacheDestroy(cacheInfo.cacheHandle);
	cacheInfo.cacheHandle = 0;

	if (cacheInfo.cursorHandle)
		rsslPayloadCursorDestroy(cacheInfo.cursorHandle);
	cacheInfo.cursorHandle = 0;

	rsslPayloadCacheUninitialize();
}

/*
 * Access to the cache info for the provider application
 */
RsslVACacheInfo* getCacheInfo()
{
	return &cacheInfo;
}

/*
 * Cleans up and exits the application.
 */
void cleanUpAndExit()
{
	int i;
	RsslErrorInfo rsslErrorInfo;


	if (pReactor)
	{
		/* clean up client sessions */
		for (i = 0; i < MAX_CLIENT_SESSIONS; i++)
		{
			if ((clientSessions[i].clientChannel != NULL) && (clientSessions[i].clientChannel->socketId != -1))
			{
				removeClientSessionForChannel(pReactor, clientSessions[i].clientChannel);
			}
		}

		if (rsslDestroyReactor(pReactor, &rsslErrorInfo) != RSSL_RET_SUCCESS)
			printf("Error cleaning up reactor: %s\n", &rsslErrorInfo.rsslError.text);

		/* clean up server */
		FD_CLR(rsslSrvr->socketId, &readFds);
		FD_CLR(rsslSrvr->socketId, &exceptFds);
		rsslCloseServer(rsslSrvr, &rsslErrorInfo.rsslError);
	}

	uninitializeCache();

	rsslUninitialize();
	/* free memory for dictionary */
	freeDictionary();

	printf("Exiting.\n");

	/* WINDOWS: wait for user to enter something before exiting  */
#ifdef _WIN32
	printf("\nPress Enter or Return key to exit application:");
	getchar();
#endif
	exit(RSSL_RET_FAILURE);
}
