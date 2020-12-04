/*
 * RemotePlugin.cpp - base class providing RPC like mechanisms
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

#define COMPILE_REMOTE_PLUGIN_BASE
//#define DEBUG_REMOTE_PLUGIN
#ifdef DEBUG_REMOTE_PLUGIN
#include <QDebug>
#endif

#include "BufferManager.h"
#include "RemotePlugin.h"
#include "Mixer.h"
#include "Engine.h"
#include "Song.h"

#include <QDebug>
#include <QDir>

#ifndef SYNC_WITH_SHM_FIFO
#include <QtCore/QUuid>
#include <sys/socket.h>
#include <sys/un.h>
#endif


// simple helper thread monitoring our RemotePlugin - if process terminates
// unexpectedly invalidate plugin so LMMS doesn't lock up
ProcessWatcher::ProcessWatcher( RemotePlugin * _p ) :
	QThread(),
	m_plugin( _p ),
	m_quit( false )
{
}


void ProcessWatcher::run()
{
	m_plugin->m_process.start( m_plugin->m_exec, m_plugin->m_args );
	exec();
	m_plugin->m_process.moveToThread( m_plugin->thread() );
	while( !m_quit && m_plugin->messagesLeft() )
	{
		msleep( 200 );
	}
	if( !m_quit )
	{
		fprintf( stderr,
				"remote plugin died! invalidating now.\n" );

		m_plugin->invalidate();
	}
}





RemotePlugin::RemotePlugin() :
	QObject(),
#ifdef SYNC_WITH_SHM_FIFO
	RemotePluginBase(new ShmFifo(), new ShmFifo()),
#else
	RemotePluginBase(),
#endif
	m_failed( true ),
	m_watcher( this ),
	m_commMutex( QMutex::Recursive ),
	m_splitChannels( false ),
	m_shmObj(),
	m_shmSize( 0 ),
	m_shm( NULL ),
	m_inputCount( DEFAULT_CHANNELS ),
	m_outputCount( DEFAULT_CHANNELS )
{
#ifndef SYNC_WITH_SHM_FIFO
	struct sockaddr_un sa;
	sa.sun_family = AF_LOCAL;

	m_socketFile = QDir::tempPath() + QDir::separator() +
						QUuid::createUuid().toString();
	auto path = m_socketFile.toUtf8();
	size_t length = path.length();
	if ( length >= sizeof sa.sun_path )
	{
		length = sizeof sa.sun_path - 1;
		qWarning( "Socket path too long." );
	}
	memcpy(sa.sun_path, path.constData(), length );
	sa.sun_path[length] = '\0';

	m_server = socket( PF_LOCAL, SOCK_STREAM, 0 );
	if ( m_server == -1 )
	{
		qWarning( "Unable to start the server." );
	}
	remove(path.constData());
	int ret = bind( m_server, (struct sockaddr *) &sa, sizeof sa );
	if ( ret == -1 || listen( m_server, 1 ) == -1 )
	{
		qWarning( "Unable to start the server." );
	}
#endif

	connect( &m_process, SIGNAL( finished( int, QProcess::ExitStatus ) ),
		this, SLOT( processFinished( int, QProcess::ExitStatus ) ),
		Qt::DirectConnection );
	connect( &m_process, SIGNAL( errorOccurred( QProcess::ProcessError ) ),
			 this, SLOT( processErrored( QProcess::ProcessError ) ),
		Qt::DirectConnection );
	connect( &m_process, SIGNAL( finished( int, QProcess::ExitStatus ) ),
		&m_watcher, SLOT( quit() ), Qt::DirectConnection );
}




RemotePlugin::~RemotePlugin()
{
	m_watcher.stop();
	m_watcher.wait();

	if( m_failed == false )
	{
		if( isRunning() )
		{
			lock();
			sendMessage( IdQuit );

			m_process.waitForFinished( 1000 );
			if( m_process.state() != QProcess::NotRunning )
			{
				m_process.terminate();
				m_process.kill();
			}
			unlock();
		}
	}

#ifndef SYNC_WITH_SHM_FIFO
	if ( close( m_server ) == -1)
	{
		qWarning( "Error freeing resources." );
	}
	remove( m_socketFile.toUtf8().constData() );
#endif
}




bool RemotePlugin::init(const QString &pluginExecutable,
							bool waitForInitDoneMsg , QStringList extraArgs)
{
	lock();
	if( m_failed )
	{
#ifdef SYNC_WITH_SHM_FIFO
		reset( new ShmFifo(), new ShmFifo() );
#endif
		m_failed = false;
	}
	QString exec = QFileInfo(QDir("plugins:"), pluginExecutable).absoluteFilePath();
#ifdef LMMS_BUILD_APPLE
	// search current directory first
	QString curDir = QCoreApplication::applicationDirPath() + "/" + pluginExecutable;
	if( QFile( curDir ).exists() )
	{
		exec = curDir;
	}
#endif
#ifdef LMMS_BUILD_WIN32
	if( ! exec.endsWith( ".exe", Qt::CaseInsensitive ) )
	{
		exec += ".exe";
	}
#endif

	if( ! QFile( exec ).exists() )
	{
		qWarning( "Remote plugin '%s' not found.",
						exec.toUtf8().constData() );
		m_failed = true;
		invalidate();
		unlock();
		return failed();
	}

	// ensure the watcher is ready in case we're running again
	// (e.g. 32-bit VST plugins on Windows)
	m_watcher.wait();
	m_watcher.reset();

	QStringList args;
#ifdef SYNC_WITH_SHM_FIFO
	// swap in and out for bidirectional communication
	args << QString::number( out()->shmKey() );
	args << QString::number( in()->shmKey() );
#else
	args << m_socketFile;
#endif
	// FIXME what should I pass as the ID?
	args << QString::number(Engine::getSong()->vstSyncController().sharedMemoryKey());
	args << extraArgs;
#ifndef DEBUG_REMOTE_PLUGIN
	m_process.setProcessChannelMode( QProcess::ForwardedChannels );
	m_process.setWorkingDirectory( QCoreApplication::applicationDirPath() );
	m_exec = exec;
	m_args = args;
	// we start the process on the watcher thread to work around QTBUG-8819
	m_process.moveToThread( &m_watcher );
	m_watcher.start( QThread::LowestPriority );
#else
	qDebug() << exec << args;
#endif

#ifndef SYNC_WITH_SHM_FIFO
	struct pollfd pollin;
	pollin.fd = m_server;
	pollin.events = POLLIN;

	int retryCount = 0;
	int result;
	do
	{
		if (retryCount > 0)
		{
			qWarning("Retrying to connect to the remote plugin...");
		}
		result = poll(&pollin, 1, 30000);
		switch (result)
		{
		case -1:
			qWarning( "Unexpected poll error." );
			break;

		case 0:
			qWarning( "Remote plugin did not connect." );
			break;

		default:
			m_socket = accept( m_server, NULL, NULL );
			if ( m_socket == -1 )
			{
				qWarning( "Unexpected socket error." );
			}
		}
		++retryCount;
	} while (result == -1 && retryCount <= 3);
#endif

	resizeSharedProcessingMemory();

	if( waitForInitDoneMsg )
	{
		waitForInitDone();
	}
	unlock();

	return failed();
}




bool RemotePlugin::process( const sampleFrame * _in_buf,
						sampleFrame * _out_buf )
{
	const fpp_t frames = Engine::mixer()->framesPerPeriod();

	if( m_failed || !isRunning() )
	{
		if( _out_buf != NULL )
		{
			BufferManager::clear( _out_buf, frames );
		}
		return false;
	}

	if( m_shm == NULL )
	{
		// m_shm being zero means we didn't initialize everything so
		// far so process one message each time (and hope we get
		// information like SHM-key etc.) until we process messages
		// in a later stage of this procedure
		if( m_shmSize == 0 )
		{
			lock();
			fetchAndProcessAllMessages();
			unlock();
		}
		if( _out_buf != NULL )
		{
			BufferManager::clear( _out_buf, frames );
		}
		return false;
	}

	memset( m_shm, 0, m_shmSize );

	ch_cnt_t inputs = qMin<ch_cnt_t>( m_inputCount, DEFAULT_CHANNELS );

	if( _in_buf != NULL && inputs > 0 )
	{
		if( m_splitChannels )
		{
			for( ch_cnt_t ch = 0; ch < inputs; ++ch )
			{
				for( fpp_t frame = 0; frame < frames; ++frame )
				{
					m_shm[ch * frames + frame] =
							_in_buf[frame][ch];
				}
			}
		}
		else if( inputs == DEFAULT_CHANNELS )
		{
			memcpy( m_shm, _in_buf, frames * BYTES_PER_FRAME );
		}
		else
		{
			sampleFrame * o = (sampleFrame *) m_shm;
			for( ch_cnt_t ch = 0; ch < inputs; ++ch )
			{
				for( fpp_t frame = 0; frame < frames; ++frame )
				{
					o[frame][ch] = _in_buf[frame][ch];
				}
			}
		}
	}

	lock();
	sendMessage( IdStartProcessing );

	if( m_failed || _out_buf == NULL || m_outputCount == 0 )
	{
		unlock();
		return false;
	}

	waitForMessage( IdProcessingDone );
	unlock();

	const ch_cnt_t outputs = qMin<ch_cnt_t>( m_outputCount,
							DEFAULT_CHANNELS );
	if( m_splitChannels )
	{
		for( ch_cnt_t ch = 0; ch < outputs; ++ch )
		{
			for( fpp_t frame = 0; frame < frames; ++frame )
			{
				_out_buf[frame][ch] = m_shm[( m_inputCount+ch )*
								frames + frame];
			}
		}
	}
	else if( outputs == DEFAULT_CHANNELS )
	{
		memcpy( _out_buf, m_shm + m_inputCount * frames,
						frames * BYTES_PER_FRAME );
	}
	else
	{
		sampleFrame * o = (sampleFrame *) ( m_shm +
							m_inputCount*frames );
		// clear buffer, if plugin didn't fill up both channels
		BufferManager::clear( _out_buf, frames );

		for( ch_cnt_t ch = 0; ch <
				qMin<int>( DEFAULT_CHANNELS, outputs ); ++ch )
		{
			for( fpp_t frame = 0; frame < frames; ++frame )
			{
				_out_buf[frame][ch] = o[frame][ch];
			}
		}
	}

	return true;
}




void RemotePlugin::processMidiEvent( const MidiEvent & _e,
							const f_cnt_t _offset )
{
	message m( IdMidiEvent );
	m.addInt( _e.type() );
	m.addInt( _e.channel() );
	m.addInt( _e.param( 0 ) );
	m.addInt( _e.param( 1 ) );
	m.addInt( _offset );
	lock();
	sendMessage( m );
	unlock();
}

void RemotePlugin::showUI()
{
	lock();
	sendMessage( IdShowUI );
	unlock();
}

void RemotePlugin::hideUI()
{
	lock();
	sendMessage( IdHideUI );
	unlock();
}




void RemotePlugin::resizeSharedProcessingMemory()
{
	const size_t s = ( m_inputCount+m_outputCount ) *
				Engine::mixer()->framesPerPeriod() *
							sizeof( float );
	if( m_shm != NULL )
	{
		m_shmObj.detach();
	}

	static int shmKey = 0;
	m_shm = (float*)createShmWithFreeKey(m_shmObj, s, shmKey);
	sendMessage( message( IdChangeSharedMemoryKey ).
				addInt( shmKey ).addInt( m_shmSize ) );
}




void RemotePlugin::processFinished( int exitCode,
					QProcess::ExitStatus exitStatus )
{
	if ( exitStatus == QProcess::CrashExit )
	{
		qCritical() << "Remote plugin crashed";
	}
	else if ( exitCode )
	{
		qCritical() << "Remote plugin exit code: " << exitCode;
	}
#ifndef SYNC_WITH_SHM_FIFO
	invalidate();
#endif
}

void RemotePlugin::processErrored( QProcess::ProcessError err )
{
	qCritical() << "Process error: " << err;
}




bool RemotePlugin::processMessage( const message & _m )
{
	lock();
	message reply_message( _m.id );
	bool reply = false;
	switch( _m.id )
	{
		case IdUndefined:
			unlock();
			return false;

		case IdInitDone:
			reply = true;
			break;

		case IdSampleRateInformation:
			reply = true;
			reply_message.addInt( Engine::mixer()->processingSampleRate() );
			break;

		case IdBufferSizeInformation:
			reply = true;
			reply_message.addInt( Engine::mixer()->framesPerPeriod() );
			break;

		case IdChangeInputCount:
			m_inputCount = _m.getInt( 0 );
			resizeSharedProcessingMemory();
			break;

		case IdChangeOutputCount:
			m_outputCount = _m.getInt( 0 );
			resizeSharedProcessingMemory();
			break;

		case IdChangeInputOutputCount:
			m_inputCount = _m.getInt( 0 );
			m_outputCount = _m.getInt( 1 );
			resizeSharedProcessingMemory();
			break;

		case IdDebugMessage:
			fprintf( stderr, "RemotePlugin::DebugMessage: %s",
						_m.getString( 0 ).c_str() );
			break;

		case IdProcessingDone:
		case IdQuit:
		default:
			break;
	}
	if( reply )
	{
		sendMessage( reply_message );
	}
	unlock();

	return true;
}
