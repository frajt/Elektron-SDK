/*
 * This source code is provided under the Apache 2.0 license and is provided
 * AS IS with no warranty or guarantee of fit for purpose.  See the project's 
 * LICENSE.md for details. 
 * Copyright Thomson Reuters 2015. All rights reserved.
*/

#ifndef _RTR_RSSL_CONSUMER_H
#define _RTR_RSSL_CONSUMER_H

#include "rtr/rsslReactor.h"
#include "rsslChannelCommand.h"

#ifdef __cplusplus
extern "C" {
#endif

/* defines maximum allowed name length for this application */
#define MAX_ITEM_NAME_STRLEN 128

static void initRuntime();
static void handleRuntime();
void cleanUpAndExit(int code);
void exitApp(int code);

static RsslReactorCallbackRet defaultMsgCallback(RsslReactor *pReactor, RsslReactorChannel *pChannel, RsslMsgEvent* pMsgEvent);

RsslReactorCallbackRet channelEventCallback(RsslReactor *pReactor, RsslReactorChannel *pReactorChannel, RsslReactorChannelEvent *pConnEvent); 

void closeConnection(RsslReactor *pReactor, RsslReactorChannel *pChannel, ChannelCommand *pCommand);

void dumpHexBuffer(const RsslBuffer * buffer);

#ifdef __cplusplus
};
#endif

#endif
