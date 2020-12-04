/*
 * RemotePlugin.h - base class providing RPC like mechanisms
 *
 * Copyright (c) 2008-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

#ifndef REMOTE_PLUGIN_H
#define REMOTE_PLUGIN_H

#include "MidiEvent.h"
#include "VstSyncData.h"
#include "IPCHelper.h"

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <cassert>


// Are we building remote plugin?
#ifdef BUILD_REMOTE_PLUGIN_CLIENT
// Remote plugin side
#undef LMMS_EXPORT
#define LMMS_EXPORT
#define COMPILE_REMOTE_PLUGIN_BASE

#else // BUILD_REMOTE_PLUGIN_CLIENT
// LMMS/local plugin side
#include "lmms_export.h"
#include <QMutex>
#include <QProcess>
#include <QThread>

#endif // BUILD_REMOTE_PLUGIN_CLIENT


enum RemoteMessageIDs
{
	IdUndefined,
	IdHostInfoGotten,
	IdInitDone,
	IdQuit,
	IdSampleRateInformation,
	IdBufferSizeInformation,
	IdInformationUpdated,
	IdMidiEvent,
	IdStartProcessing,
	IdProcessingDone,
	IdChangeSharedMemoryKey,
	IdChangeInputCount,
	IdChangeOutputCount,
	IdChangeInputOutputCount,
	IdShowUI,
	IdHideUI,
	IdToggleUI,
	IdIsUIVisible,
	IdSaveSettingsToString,
	IdSaveSettingsToFile,
	IdLoadSettingsFromString,
	IdLoadSettingsFromFile,
	IdSavePresetFile,
	IdLoadPresetFile,
	IdDebugMessage,
	IdUserBase = 64
} ;



class LMMS_EXPORT RemotePluginBase
{
public:
	struct message
	{
		message() :
			id( IdUndefined ),
			data()
		{
		}

		message( const message & _m ) :
			id( _m.id ),
			data( _m.data )
		{
		}

		message( int _id ) :
			id( _id ),
			data()
		{
		}

		inline message & addString( const std::string & _s )
		{
			data.push_back( _s );
			return *this;
		}

		message & addInt( int _i )
		{
			char buf[32];
			sprintf( buf, "%d", _i );
			data.push_back( std::string( buf ) );
			return *this;
		}

		message & addFloat( float _f )
		{
			char buf[32];
			sprintf( buf, "%f", _f );
			data.push_back( std::string( buf ) );
			return *this;
		}

		inline std::string getString( int _p = 0 ) const
		{
			return data[_p];
		}

#ifndef BUILD_REMOTE_PLUGIN_CLIENT
		inline QString getQString( int _p = 0 ) const
		{
			return QString::fromStdString( getString( _p ) );
		}
#endif

		inline int getInt( int _p = 0 ) const
		{
			return atoi( data[_p].c_str() );
		}

		inline float getFloat( int _p ) const
		{
			return (float) atof( data[_p].c_str() );
		}

		inline bool operator==( const message & _m ) const
		{
			return( id == _m.id );
		}

		int id;

	private:
		std::vector<std::string> data;

		friend class RemotePluginBase;

	} ;

#ifdef SYNC_WITH_SHM_FIFO
	RemotePluginBase( ShmFifo * _in, ShmFifo * _out );
#else
	RemotePluginBase();
#endif
	virtual ~RemotePluginBase();

#ifdef SYNC_WITH_SHM_FIFO
	void reset( ShmFifo *in, ShmFifo *out )
	{
		delete m_in;
		delete m_out;
		m_in = in;
		m_out = out;
	}
#endif

	int sendMessage( const message & _m );
	message receiveMessage();

	inline bool isInvalid() const
	{
#ifdef SYNC_WITH_SHM_FIFO
		return m_in->isInvalid() || m_out->isInvalid();
#else
		return m_invalid;
#endif
	}

	message waitForMessage( const message & _m,
						bool _busy_waiting = false );

	inline message fetchAndProcessNextMessage()
	{
		message m = receiveMessage();
		processMessage( m );
		return m;
	}

#ifndef SYNC_WITH_SHM_FIFO
	inline int32_t readInt()
	{
		int32_t i;
		read( &i, sizeof( i ) );
		return i;
	}

	inline void writeInt( const int32_t & _i )
	{
		write( &_i, sizeof( _i ) );
	}

	inline std::string readString()
	{
		const int len = readInt();
		if( len )
		{
			char * sc = new char[len + 1];
			read( sc, len );
			sc[len] = 0;
			std::string s( sc );
			delete[] sc;
			return s;
		}
		return std::string();
	}


	inline void writeString( const std::string & _s )
	{
		const int len = _s.size();
		writeInt( len );
		write( _s.c_str(), len );
	}
#endif

#ifndef BUILD_REMOTE_PLUGIN_CLIENT
	inline bool messagesLeft()
	{
#ifdef SYNC_WITH_SHM_FIFO
		return m_in->messagesLeft();
#else
		struct pollfd pollin;
		pollin.fd = m_socket;
		pollin.events = POLLIN;

		if ( poll( &pollin, 1, 0 ) == -1 )
		{
			qWarning( "Unexpected poll error." );
		}
		return pollin.revents & POLLIN;
#endif
	}

	inline void fetchAndProcessAllMessages()
	{
		while( messagesLeft() )
		{
			fetchAndProcessNextMessage();
		}
	}

	static bool isMainThreadWaiting()
	{
		return waitDepthCounter() > 0;
	}
#endif

	virtual bool processMessage( const message & _m ) = 0;


protected:
#ifdef SYNC_WITH_SHM_FIFO
	inline const ShmFifo * in() const
	{
		return m_in;
	}

	inline const ShmFifo * out() const
	{
		return m_out;
	}
#endif

	inline void invalidate()
	{
#ifdef SYNC_WITH_SHM_FIFO
		m_in->invalidate();
		m_out->invalidate();
		m_in->messageSent();
#else
		m_invalid = true;
#endif
	}


#ifndef SYNC_WITH_SHM_FIFO
	int m_socket;
#endif


private:
#ifndef BUILD_REMOTE_PLUGIN_CLIENT
	static int & waitDepthCounter()
	{
		static int waitDepth = 0;
		return waitDepth;
	}
#endif

#ifdef SYNC_WITH_SHM_FIFO
	ShmFifo * m_in;
	ShmFifo * m_out;
#else
	void read( void * _buf, int _len )
	{
		if( isInvalid() )
		{
			memset( _buf, 0, _len );
			return;
		}
		char * buf = (char *) _buf;
		int remaining = _len;
		while ( remaining )
		{
			ssize_t nread = ::read( m_socket, buf, remaining );
			switch ( nread )
			{
				case -1:
					fprintf( stderr,
						"Error while reading.\n" );
				case 0:
					invalidate();
					memset( _buf, 0, _len );
					return;
			}
			buf += nread;
			remaining -= nread;
		}
	}

	void write( const void * _buf, int _len )
	{
		if( isInvalid() )
		{
			return;
		}
		const char * buf = (const char *) _buf;
		int remaining = _len;
		while ( remaining )
		{
			ssize_t nwritten = ::write( m_socket, buf, remaining );
			switch ( nwritten )
			{
				case -1:
					fprintf( stderr,
						"Error while writing.\n" );
				case 0:
					invalidate();
					return;
			}
			buf += nwritten;
			remaining -= nwritten;
		}
	}


	bool m_invalid;

	pthread_mutex_t m_receiveMutex;
	pthread_mutex_t m_sendMutex;
#endif

} ;



#ifndef BUILD_REMOTE_PLUGIN_CLIENT


class RemotePlugin;

class ProcessWatcher : public QThread
{
	Q_OBJECT
public:
	ProcessWatcher( RemotePlugin * );
	virtual ~ProcessWatcher() = default;

	void stop()
	{
		m_quit = true;
		quit();
	}

	void reset()
	{
		m_quit = false;
	}

private:
	void run() override;

	RemotePlugin * m_plugin;
	volatile bool m_quit;

} ;


class LMMS_EXPORT RemotePlugin : public QObject, public RemotePluginBase
{
	Q_OBJECT
public:
	RemotePlugin();
	virtual ~RemotePlugin();

	inline bool isRunning()
	{
#ifdef DEBUG_REMOTE_PLUGIN
		return true;
#else
		return m_process.state() != QProcess::NotRunning;
#endif
	}

	bool init( const QString &pluginExecutable, bool waitForInitDoneMsg, QStringList extraArgs = {} );

	inline void waitForHostInfoGotten()
	{
		m_failed = waitForMessage( IdHostInfoGotten ).id
							!= IdHostInfoGotten;
	}

	inline void waitForInitDone( bool _busyWaiting = true )
	{
		m_failed = waitForMessage( IdInitDone, _busyWaiting ).id != IdInitDone;
	}

	bool processMessage( const message & _m ) override;

	bool process( const sampleFrame * _in_buf, sampleFrame * _out_buf );

	void processMidiEvent( const MidiEvent&, const f_cnt_t _offset );

	void updateSampleRate( sample_rate_t _sr )
	{
		lock();
		sendMessage( message( IdSampleRateInformation ).addInt( _sr ) );
		waitForMessage( IdInformationUpdated, true );
		unlock();
	}


	virtual void toggleUI()
	{
		lock();
		sendMessage( IdToggleUI );
		unlock();
	}

	int isUIVisible()
	{
		lock();
		sendMessage( IdIsUIVisible );
		unlock();
		message m = waitForMessage( IdIsUIVisible );
		return m.id != IdIsUIVisible ? -1 : m.getInt() ? 1 : 0;
	}

	inline bool failed() const
	{
		return m_failed;
	}

	inline void lock()
	{
		m_commMutex.lock();
	}

	inline void unlock()
	{
		m_commMutex.unlock();
	}

public slots:
	virtual void showUI();
	virtual void hideUI();

protected:
	inline void setSplittedChannels( bool _on )
	{
		m_splitChannels = _on;
	}


	bool m_failed;
private:
	void resizeSharedProcessingMemory();


	QProcess m_process;
	ProcessWatcher m_watcher;

	QString m_exec;
	QStringList m_args;

	QMutex m_commMutex;
	bool m_splitChannels;
	SharedMemory m_shmObj;
	size_t m_shmSize;
	float * m_shm;

	int m_inputCount;
	int m_outputCount;

#ifndef SYNC_WITH_SHM_FIFO
	int m_server;
	QString m_socketFile;
#endif

	friend class ProcessWatcher;


private slots:
	void processFinished( int exitCode, QProcess::ExitStatus exitStatus );
	void processErrored(QProcess::ProcessError err );
} ;

#endif


#ifdef BUILD_REMOTE_PLUGIN_CLIENT

class RemotePluginClient : public RemotePluginBase
{
public:
#ifdef SYNC_WITH_SHM_FIFO
	RemotePluginClient(key_t shmIn, key_t shmOut, key_t shmVstSync);
#else
	RemotePluginClient(const char * socketPath, key_t shmVstSync);
#endif
	virtual ~RemotePluginClient();

	VstSyncData * getVSTshm();

	virtual bool processMessage( const message & _m );

	virtual void process( const sampleFrame * _in_buf,
					sampleFrame * _out_buf ) = 0;

	virtual void processMidiEvent( const MidiEvent&, const f_cnt_t /* _offset */ )
	{
	}

	inline float * sharedMemory()
	{
		return m_shm;
	}

	virtual void updateSampleRate()
	{
	}

	virtual void updateBufferSize()
	{
	}

	inline sample_rate_t sampleRate() const
	{
		return m_sampleRate;
	}

	inline fpp_t bufferSize() const
	{
		return m_bufferSize;
	}

	void setInputCount( int _i )
	{
		m_inputCount = _i;
		sendMessage( message( IdChangeInputCount ).addInt( _i ) );
	}

	void setOutputCount( int _i )
	{
		m_outputCount = _i;
		sendMessage( message( IdChangeOutputCount ).addInt( _i ) );
	}

	void setInputOutputCount( int i, int o )
	{
		m_inputCount = i;
		m_outputCount = o;
		sendMessage( message( IdChangeInputOutputCount )
				.addInt( i )
				.addInt( o ) );
	}

	virtual int inputCount() const
	{
		return m_inputCount;
	}

	virtual int outputCount() const
	{
		return m_outputCount;
	}

	void debugMessage( const std::string & _s )
	{
		sendMessage( message( IdDebugMessage ).addString( _s ) );
	}


private:
	void setShmKey( key_t _key, int _size );
	void doProcessing();

	SharedMemory m_shmObj;
	SharedMemory m_shmVstSync;

	VstSyncData * m_vstSyncData;
	float * m_shm;

	int m_inputCount;
	int m_outputCount;

	sample_rate_t m_sampleRate;
	fpp_t m_bufferSize;

} ;

#endif





#ifdef COMPILE_REMOTE_PLUGIN_BASE

#ifndef BUILD_REMOTE_PLUGIN_CLIENT
#include <QtCore/QCoreApplication>
#endif


#ifdef SYNC_WITH_SHM_FIFO
RemotePluginBase::RemotePluginBase( ShmFifo * _in, ShmFifo * _out ) :
	m_in( _in ),
	m_out( _out )
#else
RemotePluginBase::RemotePluginBase() :
	m_socket( -1 ),
	m_invalid( false )
#endif
{
#ifdef LMMS_HAVE_LOCALE_H
	// make sure, we're using common ways to print/scan
	// floats to/from strings (',' vs. '.' for decimal point etc.)
	setlocale( LC_NUMERIC, "C" );
#endif
#ifndef SYNC_WITH_SHM_FIFO
	pthread_mutex_init( &m_receiveMutex, NULL );
	pthread_mutex_init( &m_sendMutex, NULL );
#endif
}




RemotePluginBase::~RemotePluginBase()
{
#ifdef SYNC_WITH_SHM_FIFO
	delete m_in;
	delete m_out;
#else
	pthread_mutex_destroy( &m_receiveMutex );
	pthread_mutex_destroy( &m_sendMutex );
#endif
}




int RemotePluginBase::sendMessage( const message & _m )
{
#ifdef SYNC_WITH_SHM_FIFO
	m_out->lock();
	m_out->writeInt( _m.id );
	m_out->writeInt( _m.data.size() );
	int j = 8;
	for( unsigned int i = 0; i < _m.data.size(); ++i )
	{
		m_out->writeString( _m.data[i] );
		j += 4 + _m.data[i].size();
	}
	m_out->unlock();
	m_out->messageSent();
#else
	pthread_mutex_lock( &m_sendMutex );
	writeInt( _m.id );
	writeInt( _m.data.size() );
	int j = 8;
	for( unsigned int i = 0; i < _m.data.size(); ++i )
	{
		writeString( _m.data[i] );
		j += 4 + _m.data[i].size();
	}
	pthread_mutex_unlock( &m_sendMutex );
#endif

	return j;
}




RemotePluginBase::message RemotePluginBase::receiveMessage()
{
#ifdef SYNC_WITH_SHM_FIFO
	m_in->waitForMessage();
	m_in->lock();
	message m;
	m.id = m_in->readInt();
	const int s = m_in->readInt();
	for( int i = 0; i < s; ++i )
	{
		m.data.push_back( m_in->readString() );
	}
	m_in->unlock();
#else
	pthread_mutex_lock( &m_receiveMutex );
	message m;
	m.id = readInt();
	const int s = readInt();
	for( int i = 0; i < s; ++i )
	{
		m.data.push_back( readString() );
	}
	pthread_mutex_unlock( &m_receiveMutex );
#endif
	return m;
}




RemotePluginBase::message RemotePluginBase::waitForMessage(
							const message & _wm,
							bool _busy_waiting )
{
#ifndef BUILD_REMOTE_PLUGIN_CLIENT
	if( _busy_waiting )
	{
		// No point processing events outside of the main thread
		_busy_waiting = QThread::currentThread() ==
					QCoreApplication::instance()->thread();
	}

	struct WaitDepthCounter
	{
		WaitDepthCounter( int & depth, bool busy ) :
			m_depth( depth ),
			m_busy( busy )
		{
			if( m_busy ) { ++m_depth; }
		}

		~WaitDepthCounter()
		{
			if( m_busy ) { --m_depth; }
		}

		int & m_depth;
		bool m_busy;
	};

	WaitDepthCounter wdc( waitDepthCounter(), _busy_waiting );
#endif
	while( !isInvalid() )
	{
#ifndef BUILD_REMOTE_PLUGIN_CLIENT
		if( _busy_waiting && !messagesLeft() )
		{
			QCoreApplication::processEvents(
				QEventLoop::ExcludeUserInputEvents, 50 );
			continue;
		}
#endif
		message m = receiveMessage();
		processMessage( m );
		if( m.id == _wm.id )
		{
			return m;
		}
		else if( m.id == IdUndefined )
		{
			return m;
		}
	}

	return message();
}


#endif





#ifdef BUILD_REMOTE_PLUGIN_CLIENT


#ifdef SYNC_WITH_SHM_FIFO
RemotePluginClient::RemotePluginClient(key_t shmIn, key_t shmOut, key_t shmVstSync) :
	RemotePluginBase(new ShmFifo(shmIn), new ShmFifo(shmOut)),
#else
RemotePluginClient::RemotePluginClient(const char * socketPath, key_t shmVstSync) :
	RemotePluginBase(),
#endif
	m_shmObj(),
	m_shmVstSync(shmVstSync),
	m_vstSyncData( NULL ),
	m_shm( NULL ),
	m_inputCount( 0 ),
	m_outputCount( 0 ),
	m_sampleRate( 44100 ),
	m_bufferSize( 0 )
{
#ifndef SYNC_WITH_SHM_FIFO
	struct sockaddr_un sa;
	sa.sun_family = AF_LOCAL;

	size_t length = strlen( socketPath );
	if ( length >= sizeof sa.sun_path )
	{
		length = sizeof sa.sun_path - 1;
		fprintf( stderr, "Socket path too long.\n" );
	}
	memcpy( sa.sun_path, socketPath, length );
	sa.sun_path[length] = '\0';

	m_socket = socket( PF_LOCAL, SOCK_STREAM, 0 );
	if ( m_socket == -1 )
	{
		fprintf( stderr, "Could not connect to local server.\n" );
	}
	if ( ::connect( m_socket, (struct sockaddr *) &sa, sizeof sa ) == -1 )
	{
		fprintf( stderr, "Could not connect to local server.\n" );
	}
#endif

	if (m_shmVstSync.attach(true))
	{
		m_vstSyncData = (VstSyncData *) m_shmVstSync.get();
		m_bufferSize = m_vstSyncData->m_bufferSize;
		m_sampleRate = m_vstSyncData->m_sampleRate;
		sendMessage( IdHostInfoGotten );
		return;
	}

	// if attaching shared memory fails
	sendMessage( IdSampleRateInformation );
	sendMessage( IdBufferSizeInformation );
	if( waitForMessage( IdBufferSizeInformation ).id
						!= IdBufferSizeInformation )
	{
		fprintf( stderr, "Could not get buffer size information\n" );
	}
	sendMessage( IdHostInfoGotten );
}




RemotePluginClient::~RemotePluginClient()
{
#ifndef SYNC_WITH_SHM_FIFO
	if ( close( m_socket ) == -1)
	{
		fprintf( stderr, "Error freeing resources.\n" );
	}
#endif
}



VstSyncData * RemotePluginClient::getVSTshm()
{
	return m_vstSyncData;
}



bool RemotePluginClient::processMessage( const message & _m )
{
	message reply_message( _m.id );
	bool reply = false;
	switch( _m.id )
	{
		case IdUndefined:
			return false;

		case IdSampleRateInformation:
			m_sampleRate = _m.getInt();
			updateSampleRate();
			reply_message.id = IdInformationUpdated;
			reply = true;
			break;

		case IdBufferSizeInformation:
			// Should LMMS gain the ability to change buffer size
			// without a restart, it must wait for this message to
			// complete processing or else risk VST crashes
			m_bufferSize = _m.getInt();
			updateBufferSize();
			break;

		case IdQuit:
			return false;

		case IdMidiEvent:
			processMidiEvent(
				MidiEvent( static_cast<MidiEventTypes>(
							_m.getInt( 0 ) ),
						_m.getInt( 1 ),
						_m.getInt( 2 ),
						_m.getInt( 3 ) ),
							_m.getInt( 4 ) );
			break;

		case IdStartProcessing:
			doProcessing();
			reply_message.id = IdProcessingDone;
			reply = true;
			break;

		case IdChangeSharedMemoryKey:
			setShmKey( _m.getInt( 0 ), _m.getInt( 1 ) );
			break;

		case IdInitDone:
			break;

		default:
		{
			char buf[64];
			sprintf( buf, "undefined message: %d\n", (int) _m.id );
			debugMessage( buf );
			break;
		}
	}
	if( reply )
	{
		sendMessage( reply_message );
	}

	return true;
}




void RemotePluginClient::setShmKey(key_t key, int size)
{
	(void)size;
	m_shmObj.setKey(key);
	if (!(m_shm = (float*)m_shmObj.attach()))
	{
		std::string errorMsg = "failed getting shared memory: " + m_shmObj.errorMessage() + "\n";
		debugMessage(errorMsg.data());
	}
}




void RemotePluginClient::doProcessing()
{
	if( m_shm != NULL )
	{
		process( (sampleFrame *)( m_inputCount > 0 ? m_shm : NULL ),
				(sampleFrame *)( m_shm +
					( m_inputCount*m_bufferSize ) ) );
	}
	else
	{
		debugMessage( "doProcessing(): have no shared memory!\n" );
	}
}



#endif

#define QSTR_TO_STDSTR(s)	std::string( s.toUtf8().constData() )

#endif
