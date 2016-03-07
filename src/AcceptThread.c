#include "G6.h"

static int MatchClientAddr( struct NetAddress *p_netaddr , struct ForwardRule *p_forward_rule , unsigned int *p_client_index )
{
	char			port_str[ PORT_MAXLEN + 1 ] ;
	unsigned int		match_client_index ;
	struct ClientNetAddress	*p_match_addr = NULL ;
	
	memset( port_str , 0x00 , sizeof(port_str) );
	snprintf( port_str , sizeof(port_str)-1 , "%d" , p_netaddr->port.port_int );
	
	for( match_client_index = 0 , p_match_addr = & (p_forward_rule->client_addr_array[0])
		; match_client_index < p_forward_rule->client_addr_count
		; match_client_index++ , p_match_addr++ )
	{
		if(	IsMatchString( p_match_addr->netaddr.ip , p_netaddr->ip , '*' , '?' ) == 0
			&&
			IsMatchString( p_match_addr->netaddr.port.port_str , port_str , '*' , '?' ) == 0
		)
		{
			(*p_client_index) = match_client_index ;
			return MATCH;
		}
	}
	
	return NOT_MATCH;
}

static int SelectServerAddress( struct ServerEnv *penv , struct ForwardSession *p_forward_session )
{
	struct ForwardRule	*p_forward_rule = p_forward_session->p_forward_rule ;
	
	unsigned int		server_addr_index ;
		
	if( STRCMP( p_forward_rule->load_balance_algorithm , == , LOAD_BALANCE_ALGORITHM_MS ) ) /* 主备算法 */
	{
		for( server_addr_index = 0 ; server_addr_index < p_forward_rule->server_addr_count ; server_addr_index++ )
		{
			if(	p_forward_rule->server_addr_array[p_forward_rule->selected_addr_index].server_unable == 0
				||
				(
					p_forward_rule->server_addr_array[p_forward_rule->selected_addr_index].server_unable == 1
					&&
					GETTIMEVAL.tv_sec > p_forward_rule->server_addr_array[p_forward_rule->selected_addr_index].timestamp_to_enable
				)
			)
			{
				p_forward_session->server_index = p_forward_rule->selected_addr_index ;
				break;
			}
			
			p_forward_rule->selected_addr_index++;
			if( p_forward_rule->selected_addr_index >= p_forward_rule->server_addr_count )
				p_forward_rule->selected_addr_index = 0 ;
		}
		if( server_addr_index >= p_forward_rule->server_addr_count )
		{
			ErrorLog( __FILE__ , __LINE__ , "all servers unable" );
			return -1;
		}
	}
	else if( STRCMP( p_forward_rule->load_balance_algorithm , == , LOAD_BALANCE_ALGORITHM_RR ) ) /* 轮询算法 */
	{
		for( server_addr_index = 0 ; server_addr_index < p_forward_rule->server_addr_count ; server_addr_index++ )
		{
			if(	p_forward_rule->server_addr_array[p_forward_rule->selected_addr_index].server_unable == 0
				||
				(
					p_forward_rule->server_addr_array[p_forward_rule->selected_addr_index].server_unable == 1
					&&
					GETTIMEVAL.tv_sec >= p_forward_rule->server_addr_array[p_forward_rule->selected_addr_index].timestamp_to_enable
				)
			)
			{
				p_forward_session->server_index = p_forward_rule->selected_addr_index ;
				p_forward_rule->selected_addr_index++;
				if( p_forward_rule->selected_addr_index >= p_forward_rule->server_addr_count )
					p_forward_rule->selected_addr_index = 0 ;
				break;
			}
			
			p_forward_rule->selected_addr_index++;
			if( p_forward_rule->selected_addr_index >= p_forward_rule->server_addr_count )
				p_forward_rule->selected_addr_index = 0 ;
		}
		if( server_addr_index >= p_forward_rule->server_addr_count )
		{
			ErrorLog( __FILE__ , __LINE__ , "all servers unable" );
			return -1;
		}
	}
	else if( STRCMP( p_forward_rule->load_balance_algorithm , == , LOAD_BALANCE_ALGORITHM_LC ) ) /* 最少连接算法 */
	{
		p_forward_session->server_index = -1 ;
		
		pthread_mutex_lock( & (penv->server_connection_count_mutex) );
		for( server_addr_index = 0 ; server_addr_index < p_forward_rule->server_addr_count ; server_addr_index++ )
		{
			if(	p_forward_rule->server_addr_array[p_forward_rule->selected_addr_index].server_unable == 0
				||
				(
					p_forward_rule->server_addr_array[p_forward_rule->selected_addr_index].server_unable == 1
					&&
					GETTIMEVAL.tv_sec >= p_forward_rule->server_addr_array[p_forward_rule->selected_addr_index].timestamp_to_enable
				)
			)
			{
				if( p_forward_session->server_index == -1 || p_forward_rule->server_addr_array[p_forward_session->server_index].server_connection_count < p_forward_rule->server_addr_array[p_forward_rule->selected_addr_index].server_connection_count )
					p_forward_session->server_index = p_forward_rule->selected_addr_index ;
			}
			
			p_forward_rule->selected_addr_index++;
			if( p_forward_rule->selected_addr_index >= p_forward_rule->server_addr_count )
				p_forward_rule->selected_addr_index = 0 ;
		}
		pthread_mutex_unlock( & (penv->server_connection_count_mutex) );
		if( p_forward_session->server_index == -1 )
		{
			ErrorLog( __FILE__ , __LINE__ , "all servers unable" );
			return -1;
		}
		
		p_forward_rule->selected_addr_index++;
		if( p_forward_rule->selected_addr_index >= p_forward_rule->server_addr_count )
			p_forward_rule->selected_addr_index %= p_forward_rule->server_addr_count ;
	}
	else if( STRCMP( p_forward_rule->load_balance_algorithm , == , LOAD_BALANCE_ALGORITHM_RT ) ) /* 最小响应时间算法 */
	{
		p_forward_session->server_index = -1 ;
		
		pthread_mutex_lock( & (penv->server_connection_count_mutex) );
		for( server_addr_index = 0 ; server_addr_index < p_forward_rule->server_addr_count ; server_addr_index++ )
		{
			if(	p_forward_rule->server_addr_array[p_forward_rule->selected_addr_index].server_unable == 0
				||
				(
					p_forward_rule->server_addr_array[p_forward_rule->selected_addr_index].server_unable == 1
					&&
					GETTIMEVAL.tv_sec >= p_forward_rule->server_addr_array[p_forward_rule->selected_addr_index].timestamp_to_enable
				)
			)
			{
				if( p_forward_session->server_index == -1 || abs(p_forward_rule->server_addr_array[p_forward_session->server_index].rtt-p_forward_rule->server_addr_array[p_forward_session->server_index].wtt) < abs(p_forward_rule->server_addr_array[p_forward_rule->selected_addr_index].rtt-p_forward_rule->server_addr_array[p_forward_rule->selected_addr_index].wtt) )
					p_forward_session->server_index = p_forward_rule->selected_addr_index ;
			}
			
			p_forward_rule->selected_addr_index++;
			if( p_forward_rule->selected_addr_index >= p_forward_rule->server_addr_count )
				p_forward_rule->selected_addr_index = 0 ;
		}
		pthread_mutex_unlock( & (penv->server_connection_count_mutex) );
		if( p_forward_session->server_index == -1 )
		{
			ErrorLog( __FILE__ , __LINE__ , "all servers unable" );
			return -1;
		}
		
		p_forward_rule->selected_addr_index++;
		if( p_forward_rule->selected_addr_index >= p_forward_rule->server_addr_count )
			p_forward_rule->selected_addr_index %= p_forward_rule->server_addr_count ;
	}
	else if( STRCMP( p_forward_rule->load_balance_algorithm , == , LOAD_BALANCE_ALGORITHM_RD ) ) /* 随机算法 */
	{
		p_forward_rule->selected_addr_index += Rand( 1 , p_forward_rule->server_addr_count ) ;
		if( p_forward_rule->selected_addr_index >= p_forward_rule->server_addr_count )
			p_forward_rule->selected_addr_index %= p_forward_rule->server_addr_count ;
		
		for( server_addr_index = 0 ; server_addr_index < p_forward_rule->server_addr_count ; server_addr_index++ )
		{
			if(	p_forward_rule->server_addr_array[p_forward_rule->selected_addr_index].server_unable == 0
				||
				(
					p_forward_rule->server_addr_array[p_forward_rule->selected_addr_index].server_unable == 1
					&&
					GETTIMEVAL.tv_sec >= p_forward_rule->server_addr_array[p_forward_rule->selected_addr_index].timestamp_to_enable
				)
			)
			{
				unsigned long		selected_addr_2_index ;
				
				if( server_addr_index == 0 )
				{
					p_forward_session->server_index = p_forward_rule->selected_addr_index ;
				}
				else
				{
					selected_addr_2_index = Rand( 0 , p_forward_rule->server_addr_count - 1 ) ;
					if( selected_addr_2_index != p_forward_rule->selected_addr_index )
					{
						struct ServerNetAddress		tmp ;
						
						memcpy( & tmp , p_forward_rule->server_addr_array+p_forward_rule->selected_addr_index , sizeof(struct ServerNetAddress) );
						memcpy( p_forward_rule->server_addr_array+p_forward_rule->selected_addr_index , p_forward_rule->server_addr_array+selected_addr_2_index , sizeof(struct ServerNetAddress) );
						memcpy( p_forward_rule->server_addr_array+selected_addr_2_index , & tmp , sizeof(struct ServerNetAddress) );
					}
					p_forward_session->server_index = selected_addr_2_index ;
				}
				break;
			}
			
			p_forward_rule->selected_addr_index++;
			if( p_forward_rule->selected_addr_index >= p_forward_rule->server_addr_count )
				p_forward_rule->selected_addr_index = 0 ;
		}
		if( server_addr_index >= p_forward_rule->server_addr_count )
		{
			ErrorLog( __FILE__ , __LINE__ , "all servers unable" );
			return -1;
		}
	}
	else if( STRCMP( p_forward_rule->load_balance_algorithm , == , LOAD_BALANCE_ALGORITHM_HS ) ) /* 哈希算法 */
	{
		p_forward_session->server_index = CalcHash(p_forward_rule->client_addr_array[p_forward_session->client_index].netaddr.ip) % p_forward_rule->server_addr_count ;
		if(	p_forward_rule->server_addr_array[p_forward_rule->selected_addr_index].server_unable == 0
			||
			(
				p_forward_rule->server_addr_array[p_forward_rule->selected_addr_index].server_unable == 1
				&&
				GETTIMEVAL.tv_sec >= p_forward_rule->server_addr_array[p_forward_rule->selected_addr_index].timestamp_to_enable
			)
		)
		{
		}
		else
		{
			ErrorLog( __FILE__ , __LINE__ , "server unable" );
			return -1;
		}
	}
	else
	{
		ErrorLog( __FILE__ , __LINE__ , "load_balance_algorithm[%s] invalid" , p_forward_session->p_forward_rule->load_balance_algorithm );
		return -1;
	}
	
	SERVER_CONNECTION_COUNT_INCREASE(penv,p_forward_session->p_forward_rule->server_addr_array[p_forward_session->server_index],1)
	
	memcpy( & (p_forward_session->netaddr) , & (p_forward_session->p_forward_rule->server_addr_array[p_forward_session->server_index].netaddr) , sizeof(struct NetAddress) );
	
	return 0;
}

static int TryToConnectServer( struct ServerEnv *penv , struct ForwardSession *p_reverse_forward_session );

static int ResolveConnectingError( struct ServerEnv *penv , struct ForwardSession *p_reverse_forward_session )
{
	struct ServerNetAddress		*p_servers_addr = NULL ;
	
	int				nret = 0 ;
	
	/* 设置服务端不可用 */
	p_servers_addr = p_reverse_forward_session->p_forward_rule->server_addr_array + p_reverse_forward_session->server_index ;
	p_servers_addr->server_unable = 1 ;
	p_servers_addr->timestamp_to_enable = GETTIMEVAL.tv_sec + penv->timeout ;
	
	/* 非堵塞连接服务端 */
	nret = TryToConnectServer( penv , p_reverse_forward_session ) ;
	if( nret )
	{
		ErrorLog( __FILE__ , __LINE__ , "TryToConnectServer failed[%d]" , nret );
		return nret;
	}
	
	return 0;
}

static int TryToConnectServer( struct ServerEnv *penv , struct ForwardSession *p_reverse_forward_session )
{
	_SOCKLEN_T		addr_len ;
	struct epoll_event	event ;
	unsigned int		forward_thread_index ;
	
	int			nret = 0 ;
	
	/* 根据负载均衡算法选择服务端 */
	nret = SelectServerAddress( penv , p_reverse_forward_session ) ;
	if( nret )
	{
		ErrorLog( __FILE__ , __LINE__ , "SelectServerAddress failed[%d]" , nret );
		return 1;
	}
	
	/* 创建连接服务端的客户端 */
	p_reverse_forward_session->sock = socket( AF_INET , SOCK_STREAM , IPPROTO_TCP );
	if( p_reverse_forward_session->sock == -1 )
	{
		ErrorLog( __FILE__ , __LINE__ , "socket failed , errno[%d]" , errno );
		return -1;
	}
	
	SetNonBlocking( p_reverse_forward_session->sock );
	SetCloseExec( p_reverse_forward_session->sock );
	
	/* 连接服务端 */
	addr_len = sizeof(struct sockaddr) ;
	nret = connect( p_reverse_forward_session->sock , (struct sockaddr *) & (p_reverse_forward_session->netaddr.sockaddr) , addr_len );
	if( nret == -1 )
	{
		if( _ERRNO == _EINPROGRESS ) /* 正在连接 */
		{
			p_reverse_forward_session->status = FORWARD_SESSION_STATUS_CONNECTING ;
			
			InfoLog( __FILE__ , __LINE__ , "#%d# connecting [%s:%d]#%d# ..." , p_reverse_forward_session->p_reverse_forward_session->sock , p_reverse_forward_session->netaddr.ip , p_reverse_forward_session->netaddr.port.port_int , p_reverse_forward_session->sock );
			
			memset( & event , 0x00 , sizeof(struct epoll_event) );
			event.data.ptr = p_reverse_forward_session ;
			event.events = EPOLLOUT | EPOLLERR ;
			epoll_ctl( penv->accept_epoll_fd , EPOLL_CTL_ADD , p_reverse_forward_session->sock , & event );
			
			memset( & event , 0x00 , sizeof(struct epoll_event) );
			event.data.ptr = p_reverse_forward_session->p_reverse_forward_session ;
			event.events = EPOLLERR ;
			epoll_ctl( penv->accept_epoll_fd , EPOLL_CTL_ADD , p_reverse_forward_session->p_reverse_forward_session->sock , & event );
		}
		else /* 连接失败 */
		{
			ErrorLog( __FILE__ , __LINE__ , "#%d#-#%d# connect [%s:%d] failed , errno[%d]" , p_reverse_forward_session->p_reverse_forward_session->sock , p_reverse_forward_session->sock , p_reverse_forward_session->netaddr.ip , p_reverse_forward_session->netaddr.port.port_int , errno );
			SERVER_CONNECTION_COUNT_DECREASE(penv,p_reverse_forward_session->p_forward_rule->server_addr_array[p_reverse_forward_session->server_index],1)
			DebugLog( __FILE__ , __LINE__ , "close #%d#" , p_reverse_forward_session->sock );
			_CLOSESOCKET( p_reverse_forward_session->sock );
			nret = ResolveConnectingError( penv , p_reverse_forward_session ) ;
			if( nret )
			{
				struct ForwardSession	*p = p_reverse_forward_session->p_reverse_forward_session ;
				RemoveIpConnectionStat( penv , &(penv->ip_connection_stat) , p->p_forward_rule->client_addr_array[p->client_index].netaddr.sockaddr.sin_addr.s_addr );
				DebugLog( __FILE__ , __LINE__ , "close #%d#" , p_reverse_forward_session->p_reverse_forward_session->sock );
				_CLOSESOCKET( p_reverse_forward_session->p_reverse_forward_session->sock );
			}
		}
	}
	else /* 连接成功 */
	{
		p_reverse_forward_session->status = FORWARD_SESSION_STATUS_CONNECTED ;
		p_reverse_forward_session->p_reverse_forward_session->status = FORWARD_SESSION_STATUS_CONNECTED ;
		
		InfoLog( __FILE__ , __LINE__ , "#%d#-#%d# connect [%s:%d] ok" , p_reverse_forward_session->p_reverse_forward_session->sock , p_reverse_forward_session->sock , p_reverse_forward_session->netaddr.ip , p_reverse_forward_session->netaddr.port.port_int );
		
		if( p_reverse_forward_session->p_forward_rule->server_addr_array[p_reverse_forward_session->server_index].server_unable == 1 )
			p_reverse_forward_session->p_forward_rule->server_addr_array[p_reverse_forward_session->server_index].server_unable = 0 ;
		
		p_reverse_forward_session->timeout_timestamp = time(NULL) + DEFAULT_ALIVE_TIMEOUT ;
		p_reverse_forward_session->p_reverse_forward_session->timeout_timestamp = time(NULL) + DEFAULT_ALIVE_TIMEOUT ;
		
		AddTimeoutTreeNode2( penv , p_reverse_forward_session , p_reverse_forward_session->p_reverse_forward_session );
		
		epoll_ctl( penv->accept_epoll_fd , EPOLL_CTL_DEL , p_reverse_forward_session->sock , NULL );
		epoll_ctl( penv->accept_epoll_fd , EPOLL_CTL_DEL , p_reverse_forward_session->p_reverse_forward_session->sock , NULL );
		
		forward_thread_index = (p_reverse_forward_session->sock) % (penv->cmd_para.forward_thread_size) ;
		
		memset( & event , 0x00 , sizeof(struct epoll_event) );
		event.data.ptr = p_reverse_forward_session ;
		event.events = EPOLLIN | EPOLLERR ;
		epoll_ctl( penv->forward_epoll_fd_array[forward_thread_index] , EPOLL_CTL_MOD , p_reverse_forward_session->sock , & event );
		
		memset( & event , 0x00 , sizeof(struct epoll_event) );
		event.data.ptr = p_reverse_forward_session->p_reverse_forward_session ;
		event.events = EPOLLIN | EPOLLERR ;
		epoll_ctl( penv->forward_epoll_fd_array[forward_thread_index] , EPOLL_CTL_MOD , p_reverse_forward_session->p_reverse_forward_session->sock , & event );
		
		nret = write( penv->forward_request_pipe[forward_thread_index].fds[1] , "L" , 1 ) ;
	}
	
	return 0;
}

static int OnListenAccept( struct ServerEnv *penv , struct ForwardSession *p_listen_session )
{
	unsigned int		client_index ;
	int			sock ;
	struct NetAddress	netaddr ;
	_SOCKLEN_T		addr_len = sizeof(struct sockaddr) ;
	
	struct ForwardSession	*p_forward_session = NULL ;
	struct ForwardSession	*p_reverse_forward_session = NULL ;
	struct epoll_event	event ;
	
	int			nret = 0 ;
	
	while(1)
	{
		/* 接收新客户端连接 */
		sock = accept( p_listen_session->sock , (struct sockaddr *) & (netaddr.sockaddr) , & addr_len ) ;
		if( sock == -1 )
		{
			if( _ERRNO == _EWOULDBLOCK )
				break;
			
			ErrorLog( __FILE__ , __LINE__ , "accept failed , errno[%d]" , errno );
			return 0;
		}
		else
		{
			GetNetAddress( & netaddr );
			InfoLog( __FILE__ , __LINE__ , "accept [%s:%d]#%d# ok" , netaddr.ip , netaddr.port.port_int , sock );
		}
		
		/* 登记统计信息 */
		nret = AddIpConnectionStat( penv , &(penv->ip_connection_stat) , netaddr.sockaddr.sin_addr.s_addr ) ;
		if( nret )
		{
			ErrorLog( __FILE__ , __LINE__ , "AddIpConnectionStat failed[%d]" , nret );
			DebugLog( __FILE__ , __LINE__ , "close #%d#" , sock );
			_CLOSESOCKET( sock );
			return 0;
		}
		
		/* 设置socket选项 */		
		SetNonBlocking( sock );
		SetCloseExec( sock );
		
		/* 检查客户端白名单 */
		nret = MatchClientAddr( & netaddr , p_listen_session->p_forward_rule , & client_index ) ;
		if( nret == NOT_MATCH )
		{
			ErrorLog( __FILE__ , __LINE__ , "MatchClientAddr failed[%d] , rule_id[%s]" , nret , p_listen_session->p_forward_rule->rule_id );
			DebugLog( __FILE__ , __LINE__ , "close #%d#" , sock );
			_CLOSESOCKET( sock );
			return 0;
		}
		
		if( STRCMP( p_listen_session->p_forward_rule->load_balance_algorithm , == , LOAD_BALANCE_ALGORITHM_G ) )
		{
			/* 获取空闲会话结构 */
			p_forward_session = GetForwardSessionUnused( penv ) ;
			if( p_forward_session == NULL )
			{
				ErrorLog( __FILE__ , __LINE__ , "GetForwardSessionUnused failed" );
				DebugLog( __FILE__ , __LINE__ , "close #%d#" , sock );
				_CLOSESOCKET( sock );
				return 0;
			}
			
			/* 登记epoll池 */
			p_forward_session->p_forward_rule = p_listen_session->p_forward_rule ;
			p_forward_session->p_reverse_forward_session = p_listen_session ;
			p_forward_session->status = FORWARD_SESSION_STATUS_COMMAND ;
			p_forward_session->type = FORWARD_SESSION_TYPE_MANAGER ;
			p_forward_session->sock = sock ;
			memcpy( & (p_forward_session->netaddr) , & netaddr , sizeof(struct NetAddress) );
			p_forward_session->client_index = client_index ;
			p_forward_session->forward_index = p_listen_session->forward_index ;
			p_forward_session->p_reverse_forward_session = p_listen_session ;
			
			memset( & event , 0x00 , sizeof(struct epoll_event) );
			event.data.ptr = p_forward_session ;
			event.events = EPOLLIN | EPOLLERR ;
			epoll_ctl( penv->accept_epoll_fd , EPOLL_CTL_ADD , p_forward_session->sock , & event );
		}
		else
		{
			/* 获取两个空闲会话结构 */
			p_forward_session = GetForwardSessionUnused( penv ) ;
			if( p_forward_session == NULL )
			{
				ErrorLog( __FILE__ , __LINE__ , "GetForwardSessionUnused failed" );
				DebugLog( __FILE__ , __LINE__ , "close #%d#" , sock );
				_CLOSESOCKET( sock );
				return 0;
			}
			
			p_reverse_forward_session = GetForwardSessionUnused( penv ) ;
			if( p_reverse_forward_session == NULL )
			{
				ErrorLog( __FILE__ , __LINE__ , "GetForwardSessionUnused failed" );
				DebugLog( __FILE__ , __LINE__ , "close #%d#" , sock );
				_CLOSESOCKET( sock );
				SetForwardSessionUnused( penv , p_forward_session );
				return 0;
			}
			
			/* 登记epoll池 */
			p_forward_session->p_forward_rule = p_listen_session->p_forward_rule ;
			p_forward_session->p_reverse_forward_session = p_reverse_forward_session ;
			p_forward_session->type = FORWARD_SESSION_TYPE_SERVER ;
			p_forward_session->sock = sock ;
			memcpy( & (p_forward_session->netaddr) , & netaddr , sizeof(struct NetAddress) );
			p_forward_session->client_index = client_index ;
			p_forward_session->forward_index = p_listen_session->forward_index ;
			
			p_reverse_forward_session->p_forward_rule = p_listen_session->p_forward_rule ;
			p_reverse_forward_session->p_reverse_forward_session = p_forward_session ;
			p_reverse_forward_session->type = FORWARD_SESSION_TYPE_CLIENT ;
			p_reverse_forward_session->forward_index = p_listen_session->forward_index ;
			
DebugLog( __FILE__ , __LINE__ , "111 - p_forward_session[%p]#%d# p_reverse_forward_session[%p]" , p_forward_session , p_forward_session->sock , p_reverse_forward_session );
			memset( & event , 0x00 , sizeof(struct epoll_event) );
			event.data.ptr = p_forward_session ;
			event.events = EPOLLERR ;
			epoll_ctl( penv->accept_epoll_fd , EPOLL_CTL_ADD , p_forward_session->sock , & event );
			
			/* 非堵塞连接服务端 */
			nret = TryToConnectServer( penv , p_reverse_forward_session ) ;
			if( nret )
			{
				DebugLog( __FILE__ , __LINE__ , "close #%d#" , p_forward_session->sock );
				_CLOSESOCKET( p_forward_session->sock );
				SetForwardSessionUnused2( penv , p_forward_session , p_reverse_forward_session );
				return 0;
			}
		}
	}
	
	return 0;
}

static int OnConnectingServer( struct ServerEnv *penv , struct ForwardSession *p_forward_session )
{
#if ( defined __linux ) || ( defined __unix )
	int			error , code ;
#endif
	_SOCKLEN_T		addr_len ;
	struct epoll_event	event ;
	
	struct ServerNetAddress	*p_servers_addr = NULL ;
	unsigned int		forward_thread_index ;
	
	int			nret = 0 ;
	
	/* 检查非堵塞连接结果 */
#if ( defined __linux ) || ( defined __unix )
	addr_len = sizeof(int) ;
	code = getsockopt( p_forward_session->sock , SOL_SOCKET , SO_ERROR , & error , & addr_len ) ;
	if( code < 0 || error )
#elif ( defined _WIN32 )
	addr_len = sizeof(struct sockaddr_in) ;
	nret = connect( p_forward_session->sock , ( struct sockaddr *) & (p_forward_session->p_forward_rule->server_addr_array[p_forward_session->balance_algorithm.MS.server_index]->netaddr) , addr_len ) ;
	if( nret == -1 && _ERRNO == _EISCONN )
	{
		;
	}
	else
#endif
        {
		ErrorLog( __FILE__ , __LINE__ , "#%d#-#%d# connect [%s:%d] failed , errno[%d]" , p_forward_session->sock , p_forward_session->p_reverse_forward_session->sock , p_servers_addr->netaddr.ip , p_servers_addr->netaddr.port.port_int , errno );
		epoll_ctl( penv->accept_epoll_fd , EPOLL_CTL_DEL , p_forward_session->sock , NULL );
		DebugLog( __FILE__ , __LINE__ , "close #%d#" , p_forward_session->sock );
		_CLOSESOCKET( p_forward_session->sock );
		nret = ResolveConnectingError( penv , p_forward_session->p_reverse_forward_session ) ;
		if( nret )
		{
			struct ForwardSession	*p_reverse_forward_session = p_forward_session->p_reverse_forward_session ;
			RemoveIpConnectionStat( penv , &(penv->ip_connection_stat) , p_reverse_forward_session->p_forward_rule->client_addr_array[p_reverse_forward_session->client_index].netaddr.sockaddr.sin_addr.s_addr );
			SERVER_CONNECTION_COUNT_DECREASE(penv,p_forward_session->p_forward_rule->server_addr_array[p_forward_session->server_index],1)
			epoll_ctl( penv->accept_epoll_fd , EPOLL_CTL_DEL , p_forward_session->sock , NULL );
			epoll_ctl( penv->accept_epoll_fd , EPOLL_CTL_DEL , p_forward_session->p_reverse_forward_session->sock , NULL );
			DebugLog( __FILE__ , __LINE__ , "close #%d#-#%d#" , p_forward_session->sock , p_forward_session->p_reverse_forward_session->sock );
			_CLOSESOCKET2( p_forward_session->sock , p_forward_session->p_reverse_forward_session->sock );
		}
		
		return 0;
        }
	
	/* 连接成功 */
	p_forward_session->status = FORWARD_SESSION_STATUS_CONNECTED ;
	p_forward_session->p_reverse_forward_session->status = FORWARD_SESSION_STATUS_CONNECTED ;
	
	p_servers_addr = p_forward_session->p_forward_rule->server_addr_array + p_forward_session->server_index ;
	InfoLog( __FILE__ , __LINE__ , "#%d#-#%d# connect2 [%s:%d] ok" , p_forward_session->p_reverse_forward_session->sock , p_forward_session->sock , p_servers_addr->netaddr.ip , p_servers_addr->netaddr.port.port_int );
	
	if( p_forward_session->p_forward_rule->server_addr_array[p_forward_session->server_index].server_unable == 1 )
		p_forward_session->p_forward_rule->server_addr_array[p_forward_session->server_index].server_unable = 0 ;
	
	p_forward_session->timeout_timestamp = time(NULL) + DEFAULT_ALIVE_TIMEOUT ;
	p_forward_session->p_reverse_forward_session->timeout_timestamp = time(NULL) + DEFAULT_ALIVE_TIMEOUT ;
	
	AddTimeoutTreeNode2( penv , p_forward_session , p_forward_session->p_reverse_forward_session );
	
	epoll_ctl( penv->accept_epoll_fd , EPOLL_CTL_DEL , p_forward_session->sock , NULL );
	epoll_ctl( penv->accept_epoll_fd , EPOLL_CTL_DEL , p_forward_session->p_reverse_forward_session->sock , NULL );
	
	forward_thread_index = (p_forward_session->sock) % (penv->cmd_para.forward_thread_size) ;
	
	memset( & event , 0x00 , sizeof(struct epoll_event) );
	event.data.ptr = p_forward_session ;
	event.events = EPOLLIN | EPOLLERR ;
	epoll_ctl( penv->forward_epoll_fd_array[forward_thread_index] , EPOLL_CTL_ADD , p_forward_session->sock , & event );
	
	memset( & event , 0x00 , sizeof(struct epoll_event) );
	event.data.ptr = p_forward_session->p_reverse_forward_session ;
	event.events = EPOLLIN | EPOLLERR ;
	epoll_ctl( penv->forward_epoll_fd_array[forward_thread_index] , EPOLL_CTL_ADD , p_forward_session->p_reverse_forward_session->sock , & event );
	
	write( penv->forward_request_pipe[forward_thread_index].fds[1] , " " , 1 );
	
	return 0;
}

char	g_ForwardSessionTypeDesc[][7+1] = { "LISTEN" , "CLIENT" , "SERVER" , "MANAGER" } ;
char	g_ForwardSessionStatusDesc[][10+1] = { "UNUSED" , "READY" , "LISTEN" , "CONNECTING" , "CONNECTED" , "COMMAND" } ;

#define COMMAND_SEPCHAR		" \t"

static int ProcessCommand( struct ServerEnv *penv , struct ForwardSession *p_forward_session )
{
	char			*cmd = NULL ;
	char			*sub_cmd = NULL ;
	
	int			sock = p_forward_session->sock ;
	char			io_buffer[ IO_BUFFER_SIZE + 1 ] ;
	int			io_buffer_len ;
	
	cmd = strtok( p_forward_session->io_buffer , COMMAND_SEPCHAR ) ;
	if( cmd == NULL )
	{
		;
	}
	else if( STRCMP( cmd , == , "?" ) )
	{
		io_buffer_len = snprintf( io_buffer , sizeof(io_buffer)-1
			, "show (rules|sessions)\n"
			"disable (ip) (port) (disable_timeout)\n"
			"enable (ip) (port)\n"
			"quit\n"
			);
		send( sock , io_buffer , io_buffer_len , 0 );
	}
	else if( STRCMP( cmd , == , "quit" ) )
	{
		return 1;
	}
	else if( STRCMP( cmd , == , "show" ) )
	{
		sub_cmd = strtok( NULL , COMMAND_SEPCHAR ) ;
		if( sub_cmd && STRCMP( sub_cmd , == , "rules" ) )
		{
			unsigned int			forward_rule_index ;
			struct ForwardRule		*p_forward_rule = NULL ;
			unsigned int			clients_addr_index ;
			struct ClientNetAddress		*p_clients_addr = NULL ;
			unsigned int			forwards_addr_index ;
			struct ForwardNetAddress	*p_forwards_addr = NULL ;
			unsigned int			servers_addr_index ;
			struct ServerNetAddress		*p_servers_addr = NULL ;
			
			for( forward_rule_index = 0 , p_forward_rule = penv->forward_rule_array ; forward_rule_index < penv->forward_rule_count ; forward_rule_index++ , p_forward_rule++ )
			{
				io_buffer_len = snprintf( io_buffer , sizeof(io_buffer)-1 , "%s | %s | " , p_forward_rule->rule_id , p_forward_rule->load_balance_algorithm );
				send( sock , io_buffer , io_buffer_len , 0 );
				
				for( clients_addr_index = 0 , p_clients_addr = p_forward_rule->client_addr_array ; clients_addr_index < p_forward_rule->client_addr_count ; clients_addr_index++ , p_clients_addr++ )
				{
					io_buffer_len = snprintf( io_buffer , sizeof(io_buffer)-1 , "%s:%s " , p_clients_addr->netaddr.ip , p_clients_addr->netaddr.port.port_str );
					send( sock , io_buffer , io_buffer_len , 0 );
				}
				
				io_buffer_len = snprintf( io_buffer , sizeof(io_buffer)-1 , "- " );
				send( p_forward_session->sock , io_buffer , io_buffer_len , 0 );
				
				for( forwards_addr_index = 0 , p_forwards_addr = p_forward_rule->forward_addr_array ; forwards_addr_index < p_forward_rule->forward_addr_count ; forwards_addr_index++ , p_forwards_addr++ )
				{
					io_buffer_len = snprintf( io_buffer , sizeof(io_buffer)-1 , "%s:%d " , p_forwards_addr->netaddr.ip , p_forwards_addr->netaddr.port.port_int );
					send( sock , io_buffer , io_buffer_len , 0 );
				}
				
				if( p_forward_rule->server_addr_count )
				{
					io_buffer_len = snprintf( io_buffer , sizeof(io_buffer)-1 , "> " );
					send( p_forward_session->sock , io_buffer , io_buffer_len , 0 );
					
					for( servers_addr_index = 0 , p_servers_addr = p_forward_rule->server_addr_array ; servers_addr_index < p_forward_rule->server_addr_count ; servers_addr_index++ , p_servers_addr++ )
					{
						io_buffer_len = snprintf( io_buffer , sizeof(io_buffer)-1 , "%s:%d " , p_servers_addr->netaddr.ip , p_servers_addr->netaddr.port.port_int );
						send( sock , io_buffer , io_buffer_len , 0 );
					}
				}
				
				io_buffer_len = snprintf( io_buffer , sizeof(io_buffer)-1 , "\n" );
				send( sock , io_buffer , io_buffer_len , 0 );
			}
		}
		else if( sub_cmd && STRCMP( sub_cmd , == , "sessions" ) )
		{
			unsigned int			forward_session_index ;
			struct ForwardSession		*p_forward_session = NULL ;
			
			for( forward_session_index = 0 , p_forward_session = penv->forward_session_array ; forward_session_index < penv->cmd_para.forward_session_size ; forward_session_index++ , p_forward_session++ )
			{
				if( IsForwardSessionUsed( p_forward_session ) )
				{
					if( p_forward_session->type == FORWARD_SESSION_TYPE_LISTEN )
					{
						io_buffer_len = snprintf( io_buffer , sizeof(io_buffer)-1
							, "%*s | %*s | - %s:%d(#%d#)\n"
							, (int)sizeof(g_ForwardSessionTypeDesc[0]) , g_ForwardSessionTypeDesc[p_forward_session->type]
							, (int)sizeof(g_ForwardSessionStatusDesc[0]) , g_ForwardSessionStatusDesc[p_forward_session->status]
							, p_forward_session->p_forward_rule->forward_addr_array[p_forward_session->forward_index].netaddr.ip
							, p_forward_session->p_forward_rule->forward_addr_array[p_forward_session->forward_index].netaddr.port.port_int
							, p_forward_session->p_forward_rule->forward_addr_array[p_forward_session->forward_index].sock
							);
						send( sock , io_buffer , io_buffer_len , 0 );
					}
					else if( p_forward_session->type == FORWARD_SESSION_TYPE_MANAGER )
					{
						io_buffer_len = snprintf( io_buffer , sizeof(io_buffer)-1
							, "%*s | %*s | %s:%d(#%d#) - %s:%d(#%d#)\n"
							, (int)sizeof(g_ForwardSessionTypeDesc[0]) , g_ForwardSessionTypeDesc[p_forward_session->type]
							, (int)sizeof(g_ForwardSessionStatusDesc[0]) , g_ForwardSessionStatusDesc[p_forward_session->status]
							, p_forward_session->netaddr.ip
							, p_forward_session->netaddr.port.port_int
							, p_forward_session->sock
							, p_forward_session->p_forward_rule->forward_addr_array[p_forward_session->forward_index].netaddr.ip
							, p_forward_session->p_forward_rule->forward_addr_array[p_forward_session->forward_index].netaddr.port.port_int
							, p_forward_session->p_forward_rule->forward_addr_array[p_forward_session->forward_index].sock
							);
						send( sock , io_buffer , io_buffer_len , 0 );
					}
					else
					{
						io_buffer_len = snprintf( io_buffer , sizeof(io_buffer)-1
							, "%*s | %*s | %s:%d(#%d#) - %s:%d(#%d#) > %s:%d(#%d#)\n"
							, (int)sizeof(g_ForwardSessionTypeDesc[0]) , g_ForwardSessionTypeDesc[p_forward_session->type]
							, (int)sizeof(g_ForwardSessionStatusDesc[0]) , g_ForwardSessionStatusDesc[p_forward_session->status]
							, p_forward_session->netaddr.ip
							, p_forward_session->netaddr.port.port_int
							, p_forward_session->sock
							, p_forward_session->p_forward_rule->forward_addr_array[p_forward_session->forward_index].netaddr.ip
							, p_forward_session->p_forward_rule->forward_addr_array[p_forward_session->forward_index].netaddr.port.port_int
							, p_forward_session->p_forward_rule->forward_addr_array[p_forward_session->forward_index].sock
							, p_forward_session->p_reverse_forward_session->netaddr.ip
							, p_forward_session->p_reverse_forward_session->netaddr.port.port_int
							, p_forward_session->p_reverse_forward_session->sock
							);
						send( sock , io_buffer , io_buffer_len , 0 );
					}
				}
			}
		}
		else
		{
			io_buffer_len = snprintf( io_buffer , sizeof(io_buffer)-1 , "USAGE : show (rules|sessions)\n" );
			send( sock , io_buffer , io_buffer_len , 0 );
		}
	}
	else if( STRCMP( cmd , == , "disable" ) )
	{
		char			*ip = NULL ;
		char			*port_str = NULL ;
		unsigned int		port_int ;
		char			*disable_timeout = NULL ;
		
		unsigned int		forward_rule_index ;
		struct ForwardRule	*p_forward_rule = NULL ;
		unsigned int		servers_addr_index ;
		struct ServerNetAddress	*p_servers_addr = NULL ;
		
		ip = strtok( NULL , COMMAND_SEPCHAR ) ;
		port_str = strtok( NULL , COMMAND_SEPCHAR ) ;
		disable_timeout = strtok( NULL , COMMAND_SEPCHAR ) ;
		if( ip == NULL || port_str == NULL || disable_timeout == NULL )
		{
			io_buffer_len = snprintf( io_buffer , sizeof(io_buffer)-1 , "disable (ip) (port) (disable_timeout)" );
			return 0;
		}
		
		port_int = atoi(port_str) ;
		
		for( forward_rule_index = 0 , p_forward_rule = penv->forward_rule_array ; forward_rule_index < penv->forward_rule_count ; forward_rule_index++ , p_forward_rule++ )
		{
			for( servers_addr_index = 0 , p_servers_addr = p_forward_rule->server_addr_array ; servers_addr_index < p_forward_rule->server_addr_count ; servers_addr_index++ , p_servers_addr++ )
			{
				if( STRCMP( p_servers_addr->netaddr.ip , == , ip ) && p_servers_addr->netaddr.port.port_int == port_int )
				{
					p_servers_addr->server_unable = 1 ;
					p_servers_addr->timestamp_to_enable = GETTIMEVAL.tv_sec + atoi(disable_timeout) ;
					
					io_buffer_len = snprintf( io_buffer , sizeof(io_buffer)-1 , "%s %s %d unable\n" , p_forward_rule->rule_id , p_servers_addr->netaddr.ip , p_servers_addr->netaddr.port.port_int );
					send( sock , io_buffer , io_buffer_len , 0 );
				}
			}
		}
	}
	else if( STRCMP( cmd , == , "enable" ) )
	{
		char			*ip = NULL ;
		char			*port_str = NULL ;
		int			port_int ;
		
		unsigned int		forward_rule_index ;
		struct ForwardRule	*p_forward_rule = NULL ;
		unsigned int		servers_addr_index ;
		struct ServerNetAddress	*p_servers_addr = NULL ;
		
		ip = strtok( NULL , COMMAND_SEPCHAR ) ;
		port_str = strtok( NULL , COMMAND_SEPCHAR ) ;
		if( ip == NULL || port_str == NULL )
		{
			io_buffer_len = snprintf( io_buffer , sizeof(io_buffer)-1 , "enable (ip) (port)" );
			return 0;
		}
		
		port_int = atoi(port_str) ;
		
		for( forward_rule_index = 0 , p_forward_rule = penv->forward_rule_array ; forward_rule_index < penv->forward_rule_count ; forward_rule_index++ , p_forward_rule++ )
		{
			for( servers_addr_index = 0 , p_servers_addr = p_forward_rule->server_addr_array ; servers_addr_index < p_forward_rule->server_addr_count ; servers_addr_index++ , p_servers_addr++ )
			{
				if( STRCMP( p_servers_addr->netaddr.ip , == , ip ) && p_servers_addr->netaddr.port.port_int == port_int )
				{
					p_servers_addr->server_unable = 0 ;
					
					io_buffer_len = snprintf( io_buffer , sizeof(io_buffer)-1 , "%s %s %d enabled\n" , p_forward_rule->rule_id , p_servers_addr->netaddr.ip , p_servers_addr->netaddr.port.port_int );
					send( sock , io_buffer , io_buffer_len , 0 );
				}
			}
		}
	}
	else
	{
		io_buffer_len = snprintf( io_buffer , sizeof(io_buffer)-1 , "invalid command[%s]" , cmd );
		send( sock , io_buffer , io_buffer_len , 0 );
		return 0;
	}
	
	return 0;
}

static int OnReceiveCommand( struct ServerEnv *penv , struct ForwardSession *p_forward_session )
{
	int			len ;
	char			*p = NULL ;
	
	int			nret = 0 ;
	
	/* 接收通讯数据 */
	len = recv( p_forward_session->sock , p_forward_session->io_buffer+p_forward_session->io_buffer_len , IO_BUFFER_SIZE-p_forward_session->io_buffer_len , 0 ) ;
	if( len == 0 )
	{
		/* 对端断开连接 */
		InfoLog( __FILE__ , __LINE__ , "recv #%d# closed" , p_forward_session->sock );
		return -1;
	}
	else if( len == -1 )
	{
		/* 通讯接收出错 */
		ErrorLog( __FILE__ , __LINE__ , "recv #%d# failed , errno[%d]" , p_forward_session->sock , errno );
		return -1;
	}
	
	p = memchr( p_forward_session->io_buffer+p_forward_session->io_buffer_len , '\n' , len ) ;
	if( p )
	{
		(*p) = '\0' ;
		if( p > p_forward_session->io_buffer && *(p-1) == '\r' )
			*(p-1) = '\0' ;
		nret = ProcessCommand( penv , p_forward_session ) ;
		if( nret )
		{
			if( nret < 0 )
			ErrorLog( __FILE__ , __LINE__ , "ProcessCommand failed[%d]" , nret );
			return -1;
		}
		
		p_forward_session->io_buffer_len = (p_forward_session->io_buffer_len+len) - (p-p_forward_session->io_buffer)-1 ;
		memmove( p_forward_session->io_buffer , p+1 , p_forward_session->io_buffer_len );
	}
	else
	{
		p_forward_session->io_buffer_len += len ;
	}
	
	return 0;
}

void *AcceptThread( struct ServerEnv *penv )
{
	int			timeout ;
	struct epoll_event	events[ WAIT_EVENTS_COUNT ] ;
	int			event_count ;
	int			event_index ;
	struct epoll_event	*p_event = NULL ;
	
	struct ForwardSession	*p_forward_session = NULL ;
	
	int			nret = 0 ;
	
	/* 设置日志输出文件 */
	SetLogFile( "%s/log/G6.log" , getenv("HOME") );
	SetLogLevel( penv->cmd_para.log_level );
	InfoLog( __FILE__ , __LINE__ , "--- G6.WorkerProcess.AcceptThread ---" );
	
	/* 主工作循环 */
	g_exit_flag = 0 ;
	while( ! ( g_exit_flag == 1 && penv->forward_session_count == 0 ) )
	{
		if( g_exit_flag == 1 )
			timeout = 1000 ;
		else
			timeout = -1 ;
		
		DebugLog( __FILE__ , __LINE__ , "epoll_wait [%d][...]... timeout[%d]" , penv->accept_request_pipe.fds[0] , timeout );
		CloseLogFile();
		event_count = epoll_wait( penv->accept_epoll_fd , events , WAIT_EVENTS_COUNT , timeout ) ;
		DebugLog( __FILE__ , __LINE__ , "epoll_wait return [%d]events" , event_count );
		for( event_index = 0 , p_event = events ; event_index < event_count ; event_index++ , p_event++ )
		{
			p_forward_session = p_event->data.ptr ;
			
			if( p_forward_session == NULL )
			{
				char		command ;
				
				DebugLog( __FILE__ , __LINE__ , "pipe session event" );
				
				InfoLog( __FILE__ , __LINE__ , "read accept_request_pipe ..." );
				nret = read( penv->accept_request_pipe.fds[0] , & command , 1 ) ;
				InfoLog( __FILE__ , __LINE__ , "read accept_request_pipe done[%d][%c]" , nret , command );
				if( nret == -1 )
				{
					ErrorLog( __FILE__ , __LINE__ , "read request pipe failed , errno[%d]" , errno );
					exit(0);
				}
				else if( nret == 0 )
				{
					ErrorLog( __FILE__ , __LINE__ , "read request pipe close" );
					exit(0);
				}
				else
				{
					if( command == 'L' )
					{
						unsigned long		forward_thread_index ;
						
						CloseLogFile();
						
						for( forward_thread_index = 0 ; forward_thread_index < penv->cmd_para.forward_thread_size ; forward_thread_index++ )
						{
							InfoLog( __FILE__ , __LINE__ , "write forward_request_pipe L ..." );
							nret = write( penv->forward_request_pipe[forward_thread_index].fds[1] , "L" , 1 ) ;
							InfoLog( __FILE__ , __LINE__ , "write forward_request_pipe L done[%d]" , nret );
						}
					}
					else if( command == 'Q' )
					{
						int			forward_session_index ;
						struct ForwardSession	*p_forward_session = NULL ;
						
						for( forward_session_index = 0 , p_forward_session = penv->forward_session_array ; forward_session_index < penv->cmd_para.forward_session_size ; forward_session_index++ , p_forward_session++ )
						{
							if( p_forward_session->type == FORWARD_SESSION_TYPE_LISTEN )
							{
								epoll_ctl( penv->accept_epoll_fd , EPOLL_CTL_DEL , p_forward_session->sock , NULL );
								DebugLog( __FILE__ , __LINE__ , "close #%d#" , p_forward_session->sock );
								_CLOSESOCKET( p_forward_session->sock );
								SetForwardSessionUnused( penv , p_forward_session );
							}
						}
						
						g_exit_flag = 1 ;
						DebugLog( __FILE__ , __LINE__ , "set g_exit_flag[%d]" , g_exit_flag );
						
						InfoLog( __FILE__ , __LINE__ , "write accept_response_pipe Q ..." );
						nret = write( penv->accept_response_pipe.fds[1] , "Q" , 1 ) ;
						InfoLog( __FILE__ , __LINE__ , "write accept_response_pipe Q done[%d]" , nret );
					}
				}
			}
			else if( p_forward_session->status == FORWARD_SESSION_STATUS_LISTEN )
			{
				DebugLog( __FILE__ , __LINE__ , "listen session event" );
				
				if( p_event->events & EPOLLIN ) /* 侦听端口事件 */
				{
					nret = OnListenAccept( penv , p_forward_session ) ;
					if( nret )
					{
						ErrorLog( __FILE__ , __LINE__ , "OnListenAccept failed[%d]" , nret );
					}
				}
				else
				{
					ErrorLog( __FILE__ , __LINE__ , "accept pipe EPOLLERR" );
				}
			}
			else if( p_forward_session->status == FORWARD_SESSION_STATUS_CONNECTING )
			{
				DebugLog( __FILE__ , __LINE__ , "connecting session event" );
				
				if( p_event->events & EPOLLOUT ) /* 非堵塞连接事件 */
				{
					nret = OnConnectingServer( penv , p_forward_session ) ;
					if( nret )
					{
						ErrorLog( __FILE__ , __LINE__ , "OnConnectingServer failed[%d]" , nret );
					}
				}
				else
				{
					ErrorLog( __FILE__ , __LINE__ , "connecting session EPOLLERR" );
					epoll_ctl( penv->accept_epoll_fd , EPOLL_CTL_DEL , p_forward_session->sock , NULL );
					DebugLog( __FILE__ , __LINE__ , "close #%d#" , p_forward_session->sock );
					_CLOSESOCKET( p_forward_session->sock );
					nret = ResolveConnectingError( penv , p_forward_session ) ;
					if( nret )
					{
						struct ForwardSession	*p_reverse_forward_session = p_forward_session->p_reverse_forward_session ;
						RemoveIpConnectionStat( penv , &(penv->ip_connection_stat) , p_reverse_forward_session->p_forward_rule->client_addr_array[p_reverse_forward_session->client_index].netaddr.sockaddr.sin_addr.s_addr );
						SERVER_CONNECTION_COUNT_DECREASE(penv,p_forward_session->p_forward_rule->server_addr_array[p_forward_session->server_index],1)
						epoll_ctl( penv->accept_epoll_fd , EPOLL_CTL_DEL , p_forward_session->sock , NULL );
						epoll_ctl( penv->accept_epoll_fd , EPOLL_CTL_DEL , p_forward_session->p_reverse_forward_session->sock , NULL );
						DebugLog( __FILE__ , __LINE__ , "close #%d#" , p_forward_session->p_reverse_forward_session->sock );
						_CLOSESOCKET( p_forward_session->p_reverse_forward_session->sock );
					}
				}
			}
			else if( p_forward_session->status == FORWARD_SESSION_STATUS_READY )
			{
				DebugLog( __FILE__ , __LINE__ , "accepted session event" );
				RemoveIpConnectionStat( penv , &(penv->ip_connection_stat) , p_forward_session->p_forward_rule->client_addr_array[p_forward_session->client_index].netaddr.sockaddr.sin_addr.s_addr );
				SERVER_CONNECTION_COUNT_DECREASE(penv,p_forward_session->p_reverse_forward_session->p_forward_rule->server_addr_array[p_forward_session->p_reverse_forward_session->server_index],1)
				epoll_ctl( penv->accept_epoll_fd , EPOLL_CTL_DEL , p_forward_session->sock , NULL );
				epoll_ctl( penv->accept_epoll_fd , EPOLL_CTL_DEL , p_forward_session->p_reverse_forward_session->sock , NULL );
				DebugLog( __FILE__ , __LINE__ , "close #%d#-#%d#" , p_forward_session->sock , p_forward_session->p_reverse_forward_session->sock );
				_CLOSESOCKET2( p_forward_session->sock , p_forward_session->p_reverse_forward_session->sock );
			}
			else if( p_forward_session->status == FORWARD_SESSION_STATUS_COMMAND )
			{
				nret = OnReceiveCommand( penv , p_forward_session ) ;
				if( nret )
				{
					ErrorLog( __FILE__ , __LINE__ , "OnReceiveCommand failed[%d]" , nret );
					RemoveIpConnectionStat( penv , &(penv->ip_connection_stat) , p_forward_session->p_forward_rule->client_addr_array[p_forward_session->client_index].netaddr.sockaddr.sin_addr.s_addr );
					epoll_ctl( penv->accept_epoll_fd , EPOLL_CTL_DEL , p_forward_session->sock , NULL );
					DebugLog( __FILE__ , __LINE__ , "close #%d#" , p_forward_session->sock );
					_CLOSESOCKET( p_forward_session->sock );
					SetForwardSessionUnused( penv , p_forward_session );
				}
			}
			else
			{
				ErrorLog( __FILE__ , __LINE__ , "unknow event" );
				exit(0);
			}
		}
	}
	
	return 0;
}

void *_AcceptThread( void *pv )
{
	return AcceptThread( (struct ServerEnv *)pv );
}

