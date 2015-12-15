/*|-----------------------------------------------------------------------------
 *|            This source code is provided under the Apache 2.0 license      --
 *|  and is provided AS IS with no warranty or guarantee of fit for purpose.  --
 *|                See the project's LICENSE.md for details.                  --
 *|           Copyright Thomson Reuters 2015. All rights reserved.            --
 *|-----------------------------------------------------------------------------
 */

#ifdef WIN32
#define EMA_COMPONENT_VER_PLATFORM ".win "
#else
#define EMA_COMPONENT_VER_PLATFORM ".linux "
#endif

#define EMA "ema"
#define COMPILE_BITS_STR "64-bit "

#include "ChannelCallbackClient.h"
#include "LoginCallbackClient.h"
#include "DirectoryCallbackClient.h"
#include "DictionaryCallbackClient.h"
#include "OmmConsumerImpl.h"
#include "OmmConsumerErrorClient.h"
#include "StreamId.h"
#include "../EmaVersion.h"

using namespace thomsonreuters::ema::access;

const EmaString ChannelCallbackClient::_clientName( "ChannelCallbackClient" );

Channel* Channel::create( OmmConsumerImpl& ommConsImpl, const EmaString& name , RsslReactor* pRsslReactor )
{
	Channel* pChannel = 0;

	try {
		pChannel = new Channel( name, pRsslReactor );
	}
	catch( std::bad_alloc ) {}

	if ( !pChannel )
	{
		const char* temp = "Failed to create Channel.";
		if ( OmmLoggerClient::ErrorEnum >= ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
			ommConsImpl.getOmmLoggerClient().log( "Channel", OmmLoggerClient::ErrorEnum, temp );

		throwMeeException( temp );
	}

	return pChannel;
}

void Channel::destroy( Channel*& pChannel )
{
	if ( pChannel )
	{
		delete pChannel;
		pChannel = 0;
	}
}

Channel::Channel( const EmaString& name, RsslReactor* pRsslReactor ) :
 _name( name ),
 _pRsslReactor( pRsslReactor ),
 _toString(),
 _rsslSocket( 0 ),
 _pRsslChannel( 0 ),
 _state( ChannelDownEnum ),
 _pLogin( 0 ),
 _pDictionary( 0 ),
 _directoryList(),
 _toStringSet( false ),
 nextStreamId(4)
{
}

Channel::~Channel()
{
}

Channel& Channel::setRsslChannel( RsslReactorChannel* pRsslChannel )
{
	_pRsslChannel = pRsslChannel;
	return *this;
}

Channel& Channel::setRsslSocket( RsslSocket rsslSocket )
{
	_toStringSet = false;
	_rsslSocket = rsslSocket;
	return *this;
}

Channel& Channel::setChannelState( ChannelState state )
{
	_toStringSet = false;
	_state = state;
	return *this;
}

Channel::ChannelState Channel::getChannelState() const
{
	return _state;
}

RsslSocket Channel::getRsslSocket() const
{
	return _rsslSocket; 
}

RsslReactorChannel* Channel::getRsslChannel() const
{
	return _pRsslChannel;
}

RsslReactor* Channel::getRsslReactor() const
{
	return _pRsslReactor;
}

const EmaString& Channel::getName() const
{
	return _name;
}

Channel& Channel::setLogin( Login* pLogin )
{
	_pLogin = pLogin;
	return *this;
}

const EmaList< Directory* >& Channel::getDirectoryList() const
{
	return _directoryList;
}

Channel& Channel::addDirectory( Directory* pDirectory )
{
	_toStringSet = false;
	_directoryList.push_back( pDirectory );
	return *this;
}

Channel& Channel::removeDirectory( Directory* pDirectory )
{
	_toStringSet = false;
	_directoryList.remove( pDirectory );
	return *this;
}

Login* Channel::getLogin() const
{
	return _pLogin;
}

Dictionary* Channel::getDictionary() const
{
	return _pDictionary;
}

Channel& Channel::setDictionary( Dictionary* pDictionary )
{
	_pDictionary = pDictionary;
	return *this;
}

Int32 Channel::getNextStreamId( UInt32 numberOfBatchItems )
{
	Int32 retVal;

	if ( numberOfBatchItems )
	{
		streamIdMutex.lock();
		retVal = ++nextStreamId;
		nextStreamId += numberOfBatchItems;
		streamIdMutex.unlock();
	}
	else {
		streamIdMutex.lock();
		if ( recoveredStreamIds.empty() ) {
			retVal = ++nextStreamId;
			streamIdMutex.unlock();
		}
		else
		{
			StreamId* tmp( recoveredStreamIds.pop_back() );
			streamIdMutex.unlock();
			retVal = (*tmp)();
			delete tmp;
		}
	}
	return retVal;
}

void Channel::returnStreamId( Int32 streamId )
{
	try {
		StreamId* sId = new StreamId( streamId );
		streamIdMutex.lock();
		recoveredStreamIds.push_back( sId );
		streamIdMutex.unlock();
	}
	catch ( std::bad_alloc ) {}
}

const EmaString& Channel::toString() const
{
	if ( !_toStringSet )
	{
		_toStringSet = true;
		_toString.set( "\tRsslReactorChannel name " ).append( _name ).append( CR )
			.append( "\tRsslReactor " ).append( ptrToStringAsHex( _pRsslReactor ) ).append( CR )
			.append( "\tRsslReactorChannel " ).append( ptrToStringAsHex( _pRsslChannel ) ).append( CR )
			.append( "\tRsslSocket " ).append( (UInt64)_rsslSocket );

		if ( ! _directoryList.empty() )
		{
			_toString.append( CR ).append( "\tDirectory " );
			Directory* directory = _directoryList.front();
			while ( directory )
			{
                _toString.append( directory->getName() ).append( " " );
				directory = directory->next();
			}
            _toString.append( CR );
		}
	}
	return _toString;
}

bool Channel::operator==( const Channel& other )
{
	if ( this == &other ) return true;

	if ( _pRsslChannel != other._pRsslChannel ) return false;

	if ( _rsslSocket != other._rsslSocket ) return false;

	if ( _pRsslReactor != other._pRsslReactor ) return false;

	if ( _name != other._name ) return false;

	return true;
}

ChannelList::ChannelList()
{
}

ChannelList::~ChannelList()
{
	Channel* channel = _list.pop_back();
	while ( channel )
	{
		Channel::destroy( channel );
		channel = _list.pop_back();
	}
}

void ChannelList::addChannel( Channel* pChannel )
{
	_list.push_back( pChannel );
}

void ChannelList::removeChannel( Channel* pChannel )
{
	_list.remove( pChannel );
}

UInt32 ChannelList::size() const
{
	return _list.size();
}

RsslReactorChannel* ChannelList::operator[]( UInt32 idx )
{
	UInt32 index = 0;
	Channel* channel = _list.front();

	while ( channel )
	{
		if ( index == idx )
		{
			return channel->getRsslChannel();
		}
		else
		{
			index++;
			channel = channel->next();
		}
	}
	
	return 0;
}

Channel* ChannelList::getChannel( const EmaString& name ) const
{
	Channel* channel = _list.front();
	while ( channel )
	{
		if ( channel->getName() == name )
		{
			return channel;
		}
		else
			channel = channel->next();
	}
	
	return 0;
}

Channel* ChannelList::getChannel( const RsslReactorChannel* pRsslChannel ) const
{
	Channel* channel = _list.front();
	while ( channel )
	{
		if ( channel->getRsslChannel() == pRsslChannel )
		{
			return channel;
		}
		else
			channel = channel->next();
	}
	
	return 0;
}

Channel* ChannelList::getChannel( RsslSocket rsslsocket ) const
{
	Channel* channel = _list.front();
	while ( channel )
	{
		if ( channel->getRsslSocket() == rsslsocket )
		{
			return channel;
		}
		else
			channel = channel->next();
	}
	
	return 0;
}

void ChannelCallbackClient::closeChannels()
{
	UInt32 size = _channelList.size();

	for ( UInt32 idx = 0; idx < size; ++idx )
	{
		_ommConsImpl.closeChannel( _channelList[idx] );
	}
}

ChannelCallbackClient::ChannelCallbackClient( OmmConsumerImpl& ommConsImpl,
											 RsslReactor* pRsslReactor ) :
 _channelList(),
 _ommConsImpl( ommConsImpl ),
 _pRsslReactor( pRsslReactor )
{
 	if ( OmmLoggerClient::VerboseEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
	{
		_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::VerboseEnum, "Created ChannelCallbackClient" );
	}
 }

ChannelCallbackClient::~ChannelCallbackClient()
{
	if ( OmmLoggerClient::VerboseEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
	{
		_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::VerboseEnum, "Destroyed ChannelCallbackClient" );
	}
}

ChannelCallbackClient* ChannelCallbackClient::create( OmmConsumerImpl& ommConsImpl,
													 RsslReactor* pRsslReactor )
{
	ChannelCallbackClient* pClient = 0;

	try {
		pClient = new ChannelCallbackClient( ommConsImpl, pRsslReactor );
	}
	catch ( std::bad_alloc ) {}

	if ( !pClient )
	{
		const char* temp = "Failed to create ChannelCallbackClient";
		if ( OmmLoggerClient::ErrorEnum >= ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
			ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp );

		throwMeeException( temp );
	}

	return pClient;
}

void ChannelCallbackClient::destroy( ChannelCallbackClient*& pClient )
{
	if ( pClient )
	{
		delete pClient;
		pClient = 0;
	}
}
void  ChannelCallbackClient::channelParametersToString (ChannelConfig *pChannelCfg, EmaString& strChannelParams)
{
	bool bValidChType = true;
	EmaString strConnectionType;
	EmaString cfgParameters;
	EmaString compType;
	switch ( pChannelCfg->compressionType )
	{
	case RSSL_COMP_ZLIB :
		compType.set( "ZLib" );
		break;
	case RSSL_COMP_LZ4 :
		compType.set( "LZ4" );
		break;
	case RSSL_COMP_NONE :
		compType.set( "None" );
		break;
	default :
		compType.set( "Unknown Compression Type" );
		break;
	}
	switch(pChannelCfg->connectionType)
	{
		case RSSL_CONN_TYPE_SOCKET:
		{
			SocketChannelConfig  *pTempChannelCfg = static_cast<SocketChannelConfig*> (pChannelCfg);
			strConnectionType = "RSSL_CONN_TYPE_SOCKET";
			cfgParameters.append( "hostName " ).append( pTempChannelCfg->hostName ).append( CR )
				.append( "port " ).append( pTempChannelCfg->serviceName ).append( CR )
				.append( "CompressionType " ).append( compType ).append( CR )
				.append( "tcpNodelay " ).append( (pTempChannelCfg->tcpNodelay ? "true" : "false" ) ).append( CR );
			break;
		}
		case RSSL_CONN_TYPE_HTTP:
		{
			HttpChannelConfig  *pTempChannelCfg = static_cast<HttpChannelConfig*> (pChannelCfg);
			strConnectionType = "RSSL_CONN_TYPE_HTTP";
			cfgParameters.append( "hostName " ).append( pTempChannelCfg->hostName ).append( CR )
				.append( "port " ).append( pTempChannelCfg->serviceName ).append( CR )
				.append( "CompressionType " ).append( compType ).append( CR )
				.append( "tcpNodelay " ).append( (pTempChannelCfg->tcpNodelay ? "true" : "false" ) ).append( CR )
				.append("ObjectName ").append(pTempChannelCfg->objectName).append( CR );
			break;
		}
		case RSSL_CONN_TYPE_ENCRYPTED:
		{
			EncryptedChannelConfig  *pTempChannelCfg = static_cast<EncryptedChannelConfig*> (pChannelCfg);
			strConnectionType = "RSSL_CONN_TYPE_ENCRYPTED";
			cfgParameters.append( "hostName " ).append( pTempChannelCfg->hostName ).append( CR )
				.append( "port " ).append( pTempChannelCfg->serviceName ).append( CR )
				.append( "CompressionType " ).append( compType ).append( CR )
				.append( "tcpNodelay " ).append( (pTempChannelCfg->tcpNodelay ? "true" : "false" ) ).append( CR )
				.append("ObjectName ").append(pTempChannelCfg->objectName).append( CR );
			break;
		}
		case RSSL_CONN_TYPE_RELIABLE_MCAST:
		{
			ReliableMcastChannelConfig  *pTempChannelCfg = static_cast<ReliableMcastChannelConfig*> (pChannelCfg);
			strConnectionType = "RSSL_CONN_TYPE_MCAST";
			cfgParameters.append( "RecvAddress " ).append( pTempChannelCfg->recvAddress ).append( CR )
				.append( "RecvPort " ).append( pTempChannelCfg->recvServiceName ).append( CR )
				.append( "SendAddress " ).append( pTempChannelCfg->sendAddress ).append( CR )
				.append( "SendPort " ).append( pTempChannelCfg->sendServiceName ).append( CR )
				.append("UnicastPort ").append( pTempChannelCfg->unicastServiceName).append( CR )
				.append("HsmInterface ").append( pTempChannelCfg->hsmInterface).append( CR )
				.append("HsmMultAddress ").append( pTempChannelCfg->hsmMultAddress).append( CR )
				.append("HsmPort ").append( pTempChannelCfg->hsmPort).append( CR )
				.append("HsmInterval ").append( pTempChannelCfg->hsmInterval).append( CR )
				.append("tcpControlPort ").append( pTempChannelCfg->tcpControlPort).append( CR )
				.append( "DisconnectOnGap " ).append( ( pTempChannelCfg->disconnectOnGap ? "true" : "false" ) ).append( CR )
				.append("PacketTTL ").append( pTempChannelCfg->packetTTL).append( CR )
				.append( "ndata " ).append( pTempChannelCfg->ndata ).append( CR )
				.append( "nmissing " ).append( pTempChannelCfg->nmissing ).append( CR )
				.append( "nrreq " ).append( pTempChannelCfg->nrreq ).append( CR )
				.append( "tdata " ).append( pTempChannelCfg->tdata ).append( CR )
				.append( "trreq " ).append( pTempChannelCfg->trreq ).append( CR )
				.append( "twait " ).append( pTempChannelCfg->twait ).append( CR )
				.append( "tbchold " ).append( pTempChannelCfg->tbchold ).append( CR )
				.append( "tpphold " ).append( pTempChannelCfg->tpphold ).append( CR )
				.append( "pktPoolLimitHigh " ).append( pTempChannelCfg->pktPoolLimitHigh ).append( CR )
				.append( "pktPoolLimitLow " ).append( pTempChannelCfg->pktPoolLimitLow ).append( CR )
				.append( "userQLimit " ).append( pTempChannelCfg->userQLimit ).append( CR );

			break;
		}
		default:
		{
			strConnectionType = "Invalid ChannelType: ";
			strConnectionType.append(pChannelCfg->connectionType)
				.append(" ");
			bValidChType = false;
			break;
		}
	}
	
	strChannelParams.append( strConnectionType ).append( CR )
		.append( "Channel name ").append( pChannelCfg->name ).append( CR )
		.append( "Consumer Name " ).append( _ommConsImpl.getConsumerName() ).append( CR );

	if(bValidChType)
	{
		strChannelParams.append( "RsslReactor " ).append( ptrToStringAsHex( _pRsslReactor ) ).append( CR )
			.append( "InterfaceName " ).append( pChannelCfg->interfaceName ).append( CR )
			.append( cfgParameters)
			.append( "reconnectAttemptLimit " ).append( pChannelCfg->reconnectAttemptLimit ).append( CR )
			.append( "reconnectMinDelay " ).append( pChannelCfg->reconnectMinDelay ).append( " msec" ).append( CR )
			.append( "reconnectMaxDelay " ).append( pChannelCfg->reconnectMaxDelay).append( " msec" ).append( CR )					
			.append( "connectionPingTimeout " ).append( pChannelCfg->connectionPingTimeout ).append( " msec" ).append( CR );
	}
}
void ChannelCallbackClient::initialize( RsslRDMLoginRequest* loginRequest, RsslRDMDirectoryRequest* dirRequest )
{
	RsslReactorOMMConsumerRole consumerRole;
	rsslClearOMMConsumerRole( &consumerRole );
	
	EmaString componentVersionInfo(EMA);
	componentVersionInfo.append(NEWVERSTRING);
	componentVersionInfo.append(EMA_COMPONENT_VER_PLATFORM);
	componentVersionInfo.append(COMPILE_BITS_STR);
	componentVersionInfo.append(EMA_LINK_TYPE);
	componentVersionInfo.append("(");
	componentVersionInfo.append(BLDTYPE);
	componentVersionInfo.append(")");

	consumerRole.pLoginRequest = loginRequest;
	consumerRole.pDirectoryRequest = dirRequest;
	consumerRole.dictionaryDownloadMode = RSSL_RC_DICTIONARY_DOWNLOAD_NONE;
	consumerRole.loginMsgCallback = OmmConsumerImpl::loginCallback;
	consumerRole.directoryMsgCallback = OmmConsumerImpl::directoryCallback;
	consumerRole.dictionaryMsgCallback = OmmConsumerImpl::dictionaryCallback;
	consumerRole.base.channelEventCallback = OmmConsumerImpl::channelCallback;
	consumerRole.base.defaultMsgCallback = OmmConsumerImpl::itemCallback;
	consumerRole.watchlistOptions.channelOpenCallback = OmmConsumerImpl::channelOpenCallback;
	consumerRole.watchlistOptions.enableWatchlist = RSSL_TRUE;
	consumerRole.watchlistOptions.itemCountHint = _ommConsImpl.getActiveConfig().itemCountHint;
	consumerRole.watchlistOptions.obeyOpenWindow = _ommConsImpl.getActiveConfig().obeyOpenWindow > 0 ? RSSL_TRUE : RSSL_FALSE;
	consumerRole.watchlistOptions.postAckTimeout = _ommConsImpl.getActiveConfig().postAckTimeout;
	consumerRole.watchlistOptions.requestTimeout = _ommConsImpl.getActiveConfig().requestTimeout;
	consumerRole.watchlistOptions.maxOutstandingPosts = _ommConsImpl.getActiveConfig().maxOutstandingPosts;
		
	EmaVector< ChannelConfig* > &consumerActivecfgChannelSet = _ommConsImpl.getActiveConfig().configChannelSet;
	unsigned int channelCfgSetLastIndex = consumerActivecfgChannelSet.size() - 1;

	RsslReactorConnectInfo      *reactorConnectInfo = new RsslReactorConnectInfo[consumerActivecfgChannelSet.size()];
	RsslReactorConnectOptions connectOpt;
	rsslClearReactorConnectOptions( &connectOpt );
	connectOpt.connectionCount = consumerActivecfgChannelSet.size();
	connectOpt.reactorConnectionList = reactorConnectInfo;
	connectOpt.reconnectAttemptLimit = consumerActivecfgChannelSet[channelCfgSetLastIndex]->reconnectAttemptLimit;
	connectOpt.reconnectMinDelay = consumerActivecfgChannelSet[channelCfgSetLastIndex]->reconnectMinDelay;
	connectOpt.reconnectMaxDelay = consumerActivecfgChannelSet[channelCfgSetLastIndex]->reconnectMaxDelay;
	connectOpt.initializationTimeout = 5;

	EmaString channelParams;
	EmaString temp( "Attempt to connect using ");
	if(connectOpt.connectionCount > 1)
		temp = "Attempt to connect using the following list";
	UInt32 supportedConnectionTypeChannelCount = 0;
	EmaString errorStrUnsupportedConnectionType("Unknown connection type. Passed in type is");

	EmaString channelNames;

	
	for(unsigned int i = 0; i < connectOpt.connectionCount; ++i)
	{
		rsslClearReactorConnectInfo(&reactorConnectInfo[i]);

		if ( consumerActivecfgChannelSet[i]->connectionType == RSSL_CONN_TYPE_SOCKET   ||
			consumerActivecfgChannelSet[i]->connectionType == RSSL_CONN_TYPE_HTTP ||
			consumerActivecfgChannelSet[i]->connectionType == RSSL_CONN_TYPE_ENCRYPTED ||
			consumerActivecfgChannelSet[i]->connectionType == RSSL_CONN_TYPE_RELIABLE_MCAST)
		{
			Channel* pChannel = Channel::create( _ommConsImpl, consumerActivecfgChannelSet[i]->name, _pRsslReactor );

			reactorConnectInfo[i].rsslConnectOptions.userSpecPtr = (void*)pChannel;
			consumerActivecfgChannelSet[i]->pChannel = pChannel;

			reactorConnectInfo[i].rsslConnectOptions.majorVersion = RSSL_RWF_MAJOR_VERSION;
			reactorConnectInfo[i].rsslConnectOptions.minorVersion = RSSL_RWF_MINOR_VERSION;
			reactorConnectInfo[i].rsslConnectOptions.protocolType = RSSL_RWF_PROTOCOL_TYPE;		
			reactorConnectInfo[i].rsslConnectOptions.connectionType = consumerActivecfgChannelSet[i]->connectionType;
			reactorConnectInfo[i].rsslConnectOptions.pingTimeout = consumerActivecfgChannelSet[i]->connectionPingTimeout;
			reactorConnectInfo[i].rsslConnectOptions.guaranteedOutputBuffers = consumerActivecfgChannelSet[i]->guaranteedOutputBuffers;
			reactorConnectInfo[i].rsslConnectOptions.sysRecvBufSize = consumerActivecfgChannelSet[i]->sysRecvBufSize;
			reactorConnectInfo[i].rsslConnectOptions.sysSendBufSize = consumerActivecfgChannelSet[i]->sysSendBufSize;
			reactorConnectInfo[i].rsslConnectOptions.numInputBuffers = consumerActivecfgChannelSet[i]->numInputBuffers;
			reactorConnectInfo[i].rsslConnectOptions.componentVersion = ( char *) componentVersionInfo.c_str();
			EmaString strConnectionType;
			switch ( reactorConnectInfo[i].rsslConnectOptions.connectionType )
			{
			case RSSL_CONN_TYPE_SOCKET:
				{
				reactorConnectInfo[i].rsslConnectOptions.compressionType = consumerActivecfgChannelSet[i]->compressionType;
				reactorConnectInfo[i].rsslConnectOptions.connectionInfo.unified.address = (char*)static_cast<SocketChannelConfig*>(consumerActivecfgChannelSet[i])->hostName.c_str();
				reactorConnectInfo[i].rsslConnectOptions.connectionInfo.unified.serviceName = (char*)static_cast<SocketChannelConfig*>(consumerActivecfgChannelSet[i])->serviceName.c_str();
				reactorConnectInfo[i].rsslConnectOptions.tcpOpts.tcp_nodelay = static_cast<SocketChannelConfig*>(consumerActivecfgChannelSet[i])->tcpNodelay;
				reactorConnectInfo[i].rsslConnectOptions.connectionInfo.unified.interfaceName = (char*)consumerActivecfgChannelSet[i]->interfaceName.c_str();
				reactorConnectInfo[i].rsslConnectOptions.connectionInfo.unified.unicastServiceName = (char *) "";
				break;
				}
			case RSSL_CONN_TYPE_ENCRYPTED:
				{
				reactorConnectInfo[i].rsslConnectOptions.compressionType = consumerActivecfgChannelSet[i]->compressionType;
				reactorConnectInfo[i].rsslConnectOptions.connectionInfo.unified.address = (char*)static_cast<EncryptedChannelConfig*>(consumerActivecfgChannelSet[i])->hostName.c_str();
				reactorConnectInfo[i].rsslConnectOptions.connectionInfo.unified.serviceName = (char*)static_cast<EncryptedChannelConfig*>(consumerActivecfgChannelSet[i])->serviceName.c_str();
				reactorConnectInfo[i].rsslConnectOptions.tcpOpts.tcp_nodelay = static_cast<EncryptedChannelConfig*>(consumerActivecfgChannelSet[i])->tcpNodelay;
				reactorConnectInfo[i].rsslConnectOptions.objectName = (char*) static_cast<EncryptedChannelConfig*>(consumerActivecfgChannelSet[i])->objectName.c_str();
				reactorConnectInfo[i].rsslConnectOptions.connectionInfo.unified.interfaceName = (char*)consumerActivecfgChannelSet[i]->interfaceName.c_str();
				reactorConnectInfo[i].rsslConnectOptions.connectionInfo.unified.unicastServiceName = (char *) "";
				break;
				}
			case RSSL_CONN_TYPE_HTTP:
				{
				reactorConnectInfo[i].rsslConnectOptions.compressionType = consumerActivecfgChannelSet[i]->compressionType;
				reactorConnectInfo[i].rsslConnectOptions.connectionInfo.unified.address = (char*)static_cast<HttpChannelConfig*>(consumerActivecfgChannelSet[i])->hostName.c_str();
				reactorConnectInfo[i].rsslConnectOptions.connectionInfo.unified.serviceName = (char*)static_cast<HttpChannelConfig*>(consumerActivecfgChannelSet[i])->serviceName.c_str();
				reactorConnectInfo[i].rsslConnectOptions.tcpOpts.tcp_nodelay = static_cast<HttpChannelConfig*>(consumerActivecfgChannelSet[i])->tcpNodelay;
				reactorConnectInfo[i].rsslConnectOptions.objectName = (char*) static_cast<HttpChannelConfig*>(consumerActivecfgChannelSet[i])->objectName.c_str();
				reactorConnectInfo[i].rsslConnectOptions.connectionInfo.unified.interfaceName = (char*)consumerActivecfgChannelSet[i]->interfaceName.c_str();
				reactorConnectInfo[i].rsslConnectOptions.connectionInfo.unified.unicastServiceName = (char *) "";
				strConnectionType = "RSSL_CONN_TYPE_HTTP";
				break;
				}
			case RSSL_CONN_TYPE_RELIABLE_MCAST:
				{
				ReliableMcastChannelConfig *relMcastCfg = static_cast<ReliableMcastChannelConfig*>(consumerActivecfgChannelSet[i]);
				if(consumerActivecfgChannelSet[i]->interfaceName.empty())
					reactorConnectInfo[i].rsslConnectOptions.connectionInfo.segmented.interfaceName = 0;
				else 
					reactorConnectInfo[i].rsslConnectOptions.connectionInfo.segmented.interfaceName = (char*)consumerActivecfgChannelSet[i]->interfaceName.c_str();
				reactorConnectInfo[i].rsslConnectOptions.connectionInfo.segmented.recvAddress = (char*) relMcastCfg->recvAddress.c_str();
				reactorConnectInfo[i].rsslConnectOptions.connectionInfo.segmented.recvServiceName = (char*) relMcastCfg->recvServiceName.c_str();
				reactorConnectInfo[i].rsslConnectOptions.connectionInfo.segmented.unicastServiceName = (char*) relMcastCfg->unicastServiceName.c_str();
				reactorConnectInfo[i].rsslConnectOptions.connectionInfo.segmented.sendAddress = (char*) relMcastCfg->sendAddress.c_str();
				reactorConnectInfo[i].rsslConnectOptions.connectionInfo.segmented.sendServiceName = (char*) relMcastCfg->sendServiceName.c_str();
				reactorConnectInfo[i].rsslConnectOptions.multicastOpts.disconnectOnGaps = relMcastCfg->disconnectOnGap;
				reactorConnectInfo[i].rsslConnectOptions.multicastOpts.hsmInterface = (char *) relMcastCfg->hsmInterface.c_str();
				reactorConnectInfo[i].rsslConnectOptions.multicastOpts.hsmMultAddress = (char *) relMcastCfg->hsmMultAddress.c_str();
				reactorConnectInfo[i].rsslConnectOptions.multicastOpts.hsmPort = (char *) relMcastCfg->hsmPort.c_str();
				reactorConnectInfo[i].rsslConnectOptions.multicastOpts.hsmInterval =  relMcastCfg->hsmInterval;
				reactorConnectInfo[i].rsslConnectOptions.multicastOpts.packetTTL =  relMcastCfg->packetTTL;
				reactorConnectInfo[i].rsslConnectOptions.multicastOpts.tcpControlPort = (char *) relMcastCfg->tcpControlPort.c_str();
				reactorConnectInfo[i].rsslConnectOptions.multicastOpts.ndata =  relMcastCfg->ndata;
				reactorConnectInfo[i].rsslConnectOptions.multicastOpts.nmissing =  relMcastCfg->nmissing;
				reactorConnectInfo[i].rsslConnectOptions.multicastOpts.nrreq =  relMcastCfg->nrreq;
				reactorConnectInfo[i].rsslConnectOptions.multicastOpts.tdata =  relMcastCfg->tdata;
				reactorConnectInfo[i].rsslConnectOptions.multicastOpts.trreq =  relMcastCfg->trreq;
				reactorConnectInfo[i].rsslConnectOptions.multicastOpts.twait =  relMcastCfg->twait;
				reactorConnectInfo[i].rsslConnectOptions.multicastOpts.tpphold =  relMcastCfg->tpphold;
				reactorConnectInfo[i].rsslConnectOptions.multicastOpts.tbchold =  relMcastCfg->tbchold;
				break;
				}
			default :
				break;
			}

			pChannel->setChannelState( Channel::ChannelDownEnum );
			_channelList.addChannel( pChannel );
			supportedConnectionTypeChannelCount++;

			channelNames += pChannel->getName();
			if(i < channelCfgSetLastIndex)
			{
				channelNames.append(", ");
				consumerActivecfgChannelSet[i]->reconnectAttemptLimit = connectOpt.reconnectAttemptLimit;
				consumerActivecfgChannelSet[i]->reconnectMaxDelay = connectOpt.reconnectMaxDelay;
				consumerActivecfgChannelSet[i]->reconnectMinDelay = connectOpt.reconnectMinDelay;
				consumerActivecfgChannelSet[i]->xmlTraceFileName =  consumerActivecfgChannelSet[channelCfgSetLastIndex]->xmlTraceFileName;
				consumerActivecfgChannelSet[i]->xmlTraceToFile = consumerActivecfgChannelSet[channelCfgSetLastIndex]->xmlTraceToFile;
				consumerActivecfgChannelSet[i]->xmlTraceToStdout = consumerActivecfgChannelSet[channelCfgSetLastIndex]->xmlTraceToStdout;
				consumerActivecfgChannelSet[i]->xmlTraceToMultipleFiles = consumerActivecfgChannelSet[channelCfgSetLastIndex]->xmlTraceToMultipleFiles;
				consumerActivecfgChannelSet[i]->xmlTraceWrite = consumerActivecfgChannelSet[channelCfgSetLastIndex]->xmlTraceWrite;
				consumerActivecfgChannelSet[i]->xmlTraceRead = consumerActivecfgChannelSet[channelCfgSetLastIndex]->xmlTraceRead;
				consumerActivecfgChannelSet[i]->xmlTraceMaxFileSize = consumerActivecfgChannelSet[channelCfgSetLastIndex]->xmlTraceMaxFileSize;
				consumerActivecfgChannelSet[i]->msgKeyInUpdates = consumerActivecfgChannelSet[channelCfgSetLastIndex]->msgKeyInUpdates;
			}

			if ( OmmLoggerClient::VerboseEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
			{
				channelParams.clear();
				channelParametersToString(consumerActivecfgChannelSet[i], channelParams);
				temp.append( CR ).append(i+1).append("] ").append(channelParams);
					if(i == (connectOpt.connectionCount -1))
						_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::VerboseEnum, temp );
			}
		}
		else
		{
			reactorConnectInfo[i].rsslConnectOptions.userSpecPtr = 0;
			errorStrUnsupportedConnectionType.append( consumerActivecfgChannelSet[i]->connectionType )
			.append(" for ")
			.append(consumerActivecfgChannelSet[i]->name);
			if(i < channelCfgSetLastIndex)
				errorStrUnsupportedConnectionType.append(", ");
		}
	} 

	if(supportedConnectionTypeChannelCount > 0)
	{
		connectOpt.rsslConnectOptions.userSpecPtr = (void*) consumerActivecfgChannelSet[0]->pChannel;

		RsslErrorInfo rsslErrorInfo;
		if ( RSSL_RET_SUCCESS != rsslReactorConnect( _pRsslReactor, &connectOpt, (RsslReactorChannelRole*)(&consumerRole), &rsslErrorInfo ) )
		{

			EmaString temp( "Failed to add RsslChannel(s) to RsslReactor. Channel name(s) " );
			temp.append(channelNames ).append( CR )
				.append( "Consumer Name " ).append( _ommConsImpl.getConsumerName() ).append( CR )
				.append( "RsslReactor " ).append( ptrToStringAsHex( _pRsslReactor ) ).append( CR )
				.append( "RsslChannel " ).append( (UInt64)rsslErrorInfo.rsslError.channel ).append( CR )
				.append( "Error Id " ).append( rsslErrorInfo.rsslError.rsslErrorId ).append( CR )
				.append( "Internal sysError " ).append( rsslErrorInfo.rsslError.sysError ).append( CR )
				.append( "Error Location " ).append( rsslErrorInfo.errorLocation ).append( CR )
				.append( "Error Text " ).append( rsslErrorInfo.rsslError.text );

			if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
				_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp.trimWhitespace() );

			for(unsigned int i = 0; i < connectOpt.connectionCount; ++i)
			{
				Channel *pChannel =(Channel *) reactorConnectInfo[i].rsslConnectOptions.userSpecPtr;
				if(pChannel)
					Channel::destroy( pChannel );
			}

			delete [] reactorConnectInfo;
			throwIueException( temp );

			return;
		}

		_ommConsImpl.setState( OmmConsumerImpl::RsslChannelDownEnum );
		

		if ( OmmLoggerClient::VerboseEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
		{
			EmaString temp( "Successfully created a Reactor and Channel(s)" );
			temp.append( CR )
				.append(" Channel name(s) " ).append( channelNames ).append( CR )
				.append( "Consumer Name " ).append( _ommConsImpl.getConsumerName() );
			_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::VerboseEnum, temp );
		}	

		if(supportedConnectionTypeChannelCount < connectOpt.connectionCount)
			_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::WarningEnum, errorStrUnsupportedConnectionType );
	}
	else
	{
		delete [] reactorConnectInfo;
		throwIueException( errorStrUnsupportedConnectionType );
	}
	delete [] reactorConnectInfo;
}

void ChannelCallbackClient::removeChannel( RsslReactorChannel* pRsslReactorChannel )
{
	if ( pRsslReactorChannel )
		_channelList.removeChannel( (Channel*)pRsslReactorChannel->userSpecPtr );
}

RsslReactorCallbackRet ChannelCallbackClient::processCallback( RsslReactor* pRsslReactor,
															  RsslReactorChannel* pRsslReactorChannel,
															  RsslReactorChannelEvent* pEvent )
{
	Channel* pChannel = (Channel*)( pEvent->pReactorChannel->userSpecPtr );
	ChannelConfig *pChannelConfig = _ommConsImpl.getActiveConfig().findChannelConfig(pChannel);

	if(!pChannelConfig)
	{
		EmaString temp( "Failed to find channel config for channel " );
		temp.append( pChannel->getName() )
			.append(" that received event type: ")
			.append(pEvent->channelEventType ).append( CR )
			.append( "Consumer Name " ).append( _ommConsImpl.getConsumerName() ).append( CR )
			.append( "RsslReactor " ).append( ptrToStringAsHex( pRsslReactor ) ).append( CR );

		_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp.trimWhitespace() );
		_ommConsImpl.closeChannel( pRsslReactorChannel );

		return RSSL_RC_CRET_SUCCESS;
	}

	switch ( pEvent->channelEventType )
	{
		case RSSL_RC_CET_CHANNEL_OPENED :
		{
			if ( OmmLoggerClient::VerboseEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
			{
				EmaString temp( "Received ChannelOpened on channel " );
				temp.append( pChannel->getName() ).append( CR )
					.append( "Consumer Name " ).append( _ommConsImpl.getConsumerName() );

				_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::VerboseEnum, temp );
			}
			return RSSL_RC_CRET_SUCCESS;
		}
		case RSSL_RC_CET_CHANNEL_UP:
		{
			RsslReactorChannelInfo channelInfo;
			RsslErrorInfo rsslErrorInfo;
			RsslRet retChanInfo;
			retChanInfo = rsslReactorGetChannelInfo(pRsslReactorChannel, &channelInfo, &rsslErrorInfo);
			EmaString componentInfo("Connected component version: ");
			for(unsigned int i = 0; i < channelInfo.rsslChannelInfo.componentInfoCount; ++i)
			{
				componentInfo.append(channelInfo.rsslChannelInfo.componentInfo[i]->componentVersion.data);
				if( i < (channelInfo.rsslChannelInfo.componentInfoCount - 1) )
					componentInfo.append(", ");
			}
#ifdef WIN32
			int sendBfrSize = 65535;

			if ( rsslReactorChannelIoctl( pRsslReactorChannel, RSSL_SYSTEM_WRITE_BUFFERS, &sendBfrSize, &rsslErrorInfo ) != RSSL_RET_SUCCESS )
			{
				if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
				{
					EmaString temp( "Failed to set send buffer size on channel " );
					temp.append( pChannel->getName() ).append( CR )
						.append( "Consumer Name " ).append( _ommConsImpl.getConsumerName() ).append( CR )
						.append( "RsslReactor " ).append( ptrToStringAsHex( pRsslReactor ) ).append( CR )
						.append( "RsslChannel " ).append( ptrToStringAsHex( rsslErrorInfo.rsslError.channel ) ).append( CR )
						.append( "Error Id " ).append( rsslErrorInfo.rsslError.rsslErrorId ).append( CR )
						.append( "Internal sysError " ).append( rsslErrorInfo.rsslError.sysError ).append( CR )
						.append( "Error Location " ).append( rsslErrorInfo.errorLocation ).append( CR )
						.append( "Error text " ).append( rsslErrorInfo.rsslError.text );

					_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp.trimWhitespace() );
				}

				_ommConsImpl.closeChannel( pRsslReactorChannel );

				return RSSL_RC_CRET_SUCCESS;
			}

			int rcvBfrSize = 65535;
			if ( rsslReactorChannelIoctl( pRsslReactorChannel, RSSL_SYSTEM_READ_BUFFERS, &rcvBfrSize, &rsslErrorInfo ) != RSSL_RET_SUCCESS )
			{
				if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
				{
					EmaString temp( "Failed to set recv buffer size on channel " );
					temp.append( pChannel->getName() ).append( CR )
						.append( "Consumer Name " ).append( _ommConsImpl.getConsumerName() ).append( CR )
						.append( "RsslReactor " ).append( ptrToStringAsHex( pRsslReactor ) ).append( CR )
						.append( "RsslChannel " ).append( ptrToStringAsHex( rsslErrorInfo.rsslError.channel ) ).append( CR )
						.append( "Error Id " ).append( rsslErrorInfo.rsslError.rsslErrorId ).append( CR )
						.append( "Internal sysError " ).append( rsslErrorInfo.rsslError.sysError ).append( CR )
						.append( "Error Location " ).append( rsslErrorInfo.errorLocation ).append( CR )
						.append( "Error text " ).append( rsslErrorInfo.rsslError.text );

					_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp.trimWhitespace() );
				}

				_ommConsImpl.closeChannel( pRsslReactorChannel );

				return RSSL_RC_CRET_SUCCESS;
			}
#endif
			if (pChannelConfig->connectionType != RSSL_CONN_TYPE_RELIABLE_MCAST && rsslReactorChannelIoctl( pRsslReactorChannel, RSSL_COMPRESSION_THRESHOLD, &pChannelConfig->compressionThreshold, &rsslErrorInfo ) != RSSL_RET_SUCCESS )
			{
				if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
				{
					EmaString temp( "Failed to set compression threshold on channel " );
					temp.append( pChannel->getName() ).append( CR )
						.append( "Consumer Name " ).append( _ommConsImpl.getConsumerName() ).append( CR )
						.append( "RsslReactor " ).append( ptrToStringAsHex( pRsslReactor ) ).append( CR )
						.append( "RsslChannel " ).append( ptrToStringAsHex( rsslErrorInfo.rsslError.channel ) ).append( CR )
						.append( "Error Id " ).append( rsslErrorInfo.rsslError.rsslErrorId ).append( CR )
						.append( "Internal sysError " ).append( rsslErrorInfo.rsslError.sysError ).append( CR )
						.append( "Error Location " ).append( rsslErrorInfo.errorLocation ).append( CR )
						.append( "Error text " ).append( rsslErrorInfo.rsslError.text );

					_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp.trimWhitespace() );
				}

				_ommConsImpl.closeChannel( pRsslReactorChannel );

				return RSSL_RC_CRET_SUCCESS;
			}

			pChannel->setRsslChannel( pRsslReactorChannel )
				.setRsslSocket( pRsslReactorChannel->socketId )
				.setChannelState( Channel::ChannelUpEnum ); 
			if ( OmmLoggerClient::SuccessEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
			{
				EmaString temp( "Received ChannelUp event on channel " );
				temp.append( pChannel->getName() ).append( CR )
					.append( "Consumer Name " ).append( _ommConsImpl.getConsumerName() );
				if( channelInfo.rsslChannelInfo.componentInfoCount > 0)
					temp.append( CR ).append(componentInfo);
				_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::SuccessEnum, temp );
			}

			if ( pChannelConfig->xmlTraceToFile || pChannelConfig->xmlTraceToStdout ) 
			{
				EmaString fileName(pChannelConfig->xmlTraceFileName );
				fileName.append( "_" );

				RsslTraceOptions traceOptions;
				rsslClearTraceOptions( &traceOptions );

				traceOptions.traceMsgFileName = (char*)fileName.c_str();

				if ( pChannelConfig->xmlTraceToFile )
					traceOptions.traceFlags |= RSSL_TRACE_TO_FILE_ENABLE;

				if ( pChannelConfig->xmlTraceToStdout )
					traceOptions.traceFlags |= RSSL_TRACE_TO_STDOUT;

				if ( pChannelConfig->xmlTraceToMultipleFiles )
					traceOptions.traceFlags |= RSSL_TRACE_TO_MULTIPLE_FILES;

				if ( pChannelConfig->xmlTraceWrite )
					traceOptions.traceFlags |= RSSL_TRACE_WRITE;

				if (pChannelConfig->xmlTraceRead )
					traceOptions.traceFlags |= RSSL_TRACE_READ;
		
				traceOptions.traceMsgMaxFileSize = pChannelConfig->xmlTraceMaxFileSize;

				if ( RSSL_RET_SUCCESS != rsslReactorChannelIoctl( pRsslReactorChannel, (RsslIoctlCodes)RSSL_TRACE, (void *)&traceOptions, &rsslErrorInfo ) )
				{
					if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
					{
						EmaString temp( "Failed to enable Xml Tracing on channel " );
						temp.append( pChannel->getName() ).append( CR )
							.append( "Consumer Name " ).append( _ommConsImpl.getConsumerName() ).append( CR )
							.append( "RsslReactor " ).append( ptrToStringAsHex( pRsslReactor ) ).append( CR )
							.append( "RsslChannel " ).append( ptrToStringAsHex( rsslErrorInfo.rsslError.channel ) ).append( CR )
							.append( "Error Id " ).append( rsslErrorInfo.rsslError.rsslErrorId ).append( CR )
							.append( "Internal sysError " ).append( rsslErrorInfo.rsslError.sysError ).append( CR )
							.append( "Error Location " ).append( rsslErrorInfo.errorLocation ).append( CR )
							.append( "Error Text " ).append( rsslErrorInfo.rsslError.text );
						_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp.trimWhitespace() );
					}
				}
				else if ( OmmLoggerClient::VerboseEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
				{
					EmaString temp( "Xml Tracing enabled on channel " );
					temp.append( pChannel->getName() ).append( CR )
						.append( "Consumer Name " ).append( _ommConsImpl.getConsumerName() );
					_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::VerboseEnum, temp );
				}
			}

			_ommConsImpl.addSocket( pRsslReactorChannel->socketId );
			_ommConsImpl.setState( OmmConsumerImpl::RsslChannelUpEnum );
			return RSSL_RC_CRET_SUCCESS;
		}
		case RSSL_RC_CET_CHANNEL_READY:
		{
			pChannel->setChannelState( Channel::ChannelReadyEnum );

			if ( OmmLoggerClient::VerboseEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
			{
				EmaString temp( "Received ChannelReady event on channel " );
				temp.append( pChannel->getName() ).append( CR )
					.append( "Consumer Name " ).append( _ommConsImpl.getConsumerName() );
				_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::VerboseEnum, temp );
			}
			return RSSL_RC_CRET_SUCCESS;
		}
		case RSSL_RC_CET_FD_CHANGE:
		{
			if ( OmmLoggerClient::VerboseEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
			{
				EmaString temp( "Received FD Change event on channel " );
				temp.append( pChannel->getName() ).append( CR )
					.append( "Consumer Name " ).append( _ommConsImpl.getConsumerName() );
				_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::VerboseEnum, temp );
			}

			pChannel->setRsslChannel( pRsslReactorChannel )
				.setRsslSocket( pRsslReactorChannel->socketId );

			_ommConsImpl.removeSocket( pRsslReactorChannel->oldSocketId );
			_ommConsImpl.addSocket( pRsslReactorChannel->socketId );
			return RSSL_RC_CRET_SUCCESS;
		}
		case RSSL_RC_CET_CHANNEL_DOWN:
		{
			pChannel->setChannelState( Channel::ChannelDownEnum );

			if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
			{
				EmaString temp( "Received ChannelDown event on channel " );
				temp.append( pChannel->getName() ).append( CR )
					.append( "Consumer Name " ).append( _ommConsImpl.getConsumerName() ).append( CR )
					.append( "RsslReactor " ).append( ptrToStringAsHex( pRsslReactor ) ).append( CR )
					.append( "RsslChannel " ).append( ptrToStringAsHex( pEvent->pError->rsslError.channel ) ).append( CR )
					.append( "Error Id " ).append( pEvent->pError->rsslError.rsslErrorId ).append( CR )
					.append( "Internal sysError " ).append( pEvent->pError->rsslError.sysError ).append( CR )
					.append( "Error Location " ).append( pEvent->pError->errorLocation ).append( CR )
					.append( "Error Text " ).append( pEvent->pError->rsslError.text );
				
				_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp.trimWhitespace() );
			}

			_ommConsImpl.setState( OmmConsumerImpl::RsslChannelDownEnum );

			_ommConsImpl.closeChannel( pRsslReactorChannel );

			return RSSL_RC_CRET_SUCCESS;
		}
		case RSSL_RC_CET_CHANNEL_DOWN_RECONNECTING:
		{
			if ( OmmLoggerClient::WarningEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
			{
				EmaString temp( "Received ChannelDownReconnecting event on channel " );
				temp.append( pChannel->getName() ).append( CR )
					.append( "Consumer Name " ).append( _ommConsImpl.getConsumerName() ).append( CR )
					.append( "RsslReactor ").append( ptrToStringAsHex( pRsslReactor ) ).append( CR )
					.append( "RsslChannel " ).append( ptrToStringAsHex( pEvent->pError->rsslError.channel ) ).append( CR )
					.append( "Error Id " ).append( pEvent->pError->rsslError.rsslErrorId ).append( CR )
					.append( "Internal sysError " ).append( pEvent->pError->rsslError.sysError ).append(CR)
					.append( "Error Location " ).append( pEvent->pError->errorLocation ).append( CR )
					.append( "Error Text " ).append( pEvent->pError->rsslError.text );

				_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::WarningEnum, temp.trimWhitespace() );
			}

			if ( pRsslReactorChannel->socketId != REACTOR_INVALID_SOCKET )
				_ommConsImpl.removeSocket( pRsslReactorChannel->socketId );

			pChannel->setRsslSocket( 0 )
				.setChannelState( Channel::ChannelDownReconnectingEnum );

			return RSSL_RC_CRET_SUCCESS;
		}
		case RSSL_RC_CET_WARNING:
		{
			if ( OmmLoggerClient::WarningEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
			{
				EmaString temp( "Received Channel warning event on channel " );
				temp.append( pChannel->getName() ).append( CR )
					.append( "Consumer Name " ).append( _ommConsImpl.getConsumerName() ).append( CR )
					.append( "RsslReactor " ).append( ptrToStringAsHex( pRsslReactor ) ).append( CR )
					.append( "RsslChannel " ).append( ptrToStringAsHex( pEvent->pError->rsslError.channel ) ).append( CR )
					.append( "Error Id " ).append( pEvent->pError->rsslError.rsslErrorId ).append( CR )
					.append( "Internal sysError " ).append( pEvent->pError->rsslError.sysError ).append( CR )
					.append( "Error Location " ).append( pEvent->pError->errorLocation ).append( CR )
					.append( "Error Text " ).append( pEvent->pError->rsslError.text );

				_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::WarningEnum, temp.trimWhitespace() );
			}
			return RSSL_RC_CRET_SUCCESS;
		}
		default:
		{
			if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
			{
				EmaString temp( "Received unknown channel event type " );
				temp.append( pEvent->channelEventType ).append( CR )
					.append( "channel " ).append( pChannel->getName() ).append( CR )
					.append( "Consumer Name " ).append( _ommConsImpl.getConsumerName() ).append( CR )
					.append( "RsslReactor " ).append( ptrToStringAsHex( pRsslReactor ) ).append( CR )
					.append( "RsslChannel " ).append( ptrToStringAsHex( pEvent->pError->rsslError.channel ) ).append( CR )
					.append( "Error Id " ).append( pEvent->pError->rsslError.rsslErrorId ).append( CR )
					.append( "Internal sysError " ).append( pEvent->pError->rsslError.sysError ).append( CR )
					.append( "Error Location " ).append( pEvent->pError->errorLocation ).append( CR )
					.append( "Error Text " ).append( pEvent->pError->rsslError.text );

				_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp.trimWhitespace() );
			}
			return RSSL_RC_CRET_FAILURE;
		}
	}
}

const ChannelList & ChannelCallbackClient::getChannelList()
{
    return _channelList;
}
