/*
 * IPC.h - Inter-Process Communication helpers
 *
 * Copyright (c) 2008-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
 * Copyright (c) 2016 Javier Serrano Polo <javier@jasp.net>
 * Copyright (c) 2020 Hyunjin Song <tteu.ingog@gmail.com>
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

#ifndef IPCHELPER_H
#define IPCHELPER_H

#include "lmmsconfig.h"

#include <atomic>
#include <cassert>
#include <cstring>
#include <string>

// Common headers
#ifdef LMMS_HAVE_LOCALE_H
#include <locale.h>
#endif

#ifdef LMMS_HAVE_PTHREAD_H
#include <pthread.h>
#endif


#ifdef LMMS_HAVE_PROCESS_H
#include <process.h>
#endif

// Conditionals
#if !defined(LMMS_HAVE_SEMAPHORE_H) || defined(LMMS_BUILD_WIN32)
#define USE_QT_SEMAPHORES
#endif

#if !(defined(LMMS_HAVE_SYS_IPC_H) && defined(LMMS_HAVE_SYS_SHM_H))
#define USE_QT_SHMEM
#endif

#if !(defined(LMMS_HAVE_SYS_SOCKET_H) && defined(LMMS_HAVE_SYS_UN_H))
#define SYNC_WITH_SHM_FIFO
#endif

// Error: POSIX semaphores break interoperability between 32bit and 64bit
#if defined(SYNC_WITH_SHM_FIFO) && !defined(USE_QT_SEMAPHORES)
#error "Trying to use POSIX semaphores which breaks interoperability!"
#endif

// TODO: check if we're mixing POSIX/SysV APIs and Qt APIs

// Semaphores
#ifdef USE_QT_SEMAPHORES // Qt semaphores
#include <QtGlobal>
#include <QSystemSemaphore>
typedef QSystemSemaphore IPCSemaphore;
#else // POSIX semaphores
// Not used because of interoperability issues between 32bit and 64bit
// https://sourceware.org/bugzilla/show_bug.cgi?id=17980
#if 0
class IPCSemaphorePOSIX
{
	// TODO fill this once we use it
	;
};

typedef IPCSemaphorePOSIX IPCSemaphore;
#endif
#endif // end semaphores



// Shared memory
// TODO add POSIX shared memory
#if !defined(LMMS_HAVE_SYS_TYPES_H) || defined(LMMS_BUILD_WIN32)
typedef int32_t key_t;
#else
#include <sys/types.h>
#endif

#ifndef USE_QT_SHMEM // System-V shm
#include <sys/shm.h>
#include <errno.h>
#ifdef LMMS_HAVE_UNISTD_H
#include <unistd.h>
#endif

class SharedMemorySysV
{
public:
	SharedMemorySysV(key_t key = 1) :
		m_key(key), m_shmID(-1), m_data(nullptr), m_size(0), m_isMaster(false)
	{}
	~SharedMemorySysV()
	{
		this->detach();
	}

	inline key_t key() const { return m_key; }
	inline void setKey(key_t key) { this->detach(); m_key = key; }
	inline void* get() { return m_data; }
	inline size_t size() const { return m_size; }

	inline void* attach(bool readOnly = false)
	{
		if (m_shmID == -1)
		{
			return nullptr;
		}
		if (!m_isMaster)
		{
			m_shmID = shmget(m_key, 0, readOnly ? 0400 : 0600);
			if (m_shmID == -1)
			{
				return nullptr;
			}
		}
		m_data = shmat(m_shmID, 0, readOnly ? SHM_RDONLY : 0);
		if (m_data == reinterpret_cast<void*>(-1))
		{
			m_size = 0;
			m_data = nullptr;
		}
		if (!m_isMaster)
		{
			// get size
			shmid_ds shmid_ds;
			if (shmctl(m_shmID, IPC_STAT, &shmid_ds) != -1)
			{
				m_size = static_cast<size_t>(shmid_ds.shm_segsz);
			}
		}
		return m_data;
	}

	inline bool detach()
	{
		if (m_shmID == -1)
		{
			return false;
		}
		m_size = 0;
		if (shmdt(m_data) != -1)
		{
			return false;
		}
		m_data = nullptr;
		if (m_isMaster)
		{
			if (shmctl(m_shmID, IPC_RMID, nullptr) == -1 && errno != EINVAL)
			{
				return false;
			}
		}
		m_shmID = -1;
		return true;
	}

	inline void* create(size_t size, bool readOnly = false)
	{
		m_shmID = shmget(m_key, size, IPC_CREAT | IPC_EXCL | 0600);
		if (m_shmID == -1)
		{
			return nullptr;
		}
		m_isMaster = true;
		m_size = size;
		return this->attach();
	}

	std::string errorMessage()
	{
		return std::string(strerror(errno));
	}

private:
	key_t m_key;
	int m_shmID;
	void *m_data;
	size_t m_size;
	bool m_isMaster;
};

typedef SharedMemorySysV SharedMemory;

#else // Qt shm
#include <QtGlobal>
#include <QSharedMemory>

class SharedMemoryQt
{
public:
	SharedMemoryQt(key_t key = 1) :
		m_shm()
	{
		setKey(key);
	}
	~SharedMemoryQt() = default;

	inline key_t key() const { return m_key; }
	inline void setKey(key_t key) { m_key = key; m_shm.setKey(QString::number(key)); }
	inline void* get() { return m_shm.data(); }
	inline size_t size() const { return m_shm.size(); }

	inline void* attach(bool readOnly = false)
	{
		if (!m_shm.attach(readOnly ? QSharedMemory::ReadOnly : QSharedMemory::ReadWrite))
		{
			return nullptr;
		}
		return m_shm.data();
	}

	inline bool detach()
	{
		return m_shm.detach();
	}

	inline void* create(size_t size)
	{
		if (!m_shm.create(size, QSharedMemory::ReadWrite))
		{
			return nullptr;
		}
		return m_shm.data();
	}

	std::string errorMessage()
	{
		return m_shm.errorString().toStdString();
	}

private:
	key_t m_key;
	QSharedMemory m_shm;
};

typedef SharedMemoryQt SharedMemory;

#endif // End shared memory

// TODO move to a namespace or SharedMemory class?
inline static void *createShmWithFreeKey(SharedMemory &shm, size_t size, key_t &key)
{
	do
	{
		shm.setKey(++key);
	} while (!shm.create(size));
	return shm.get();
}


// Communication channel: shm FIFO vs. socket
#ifdef SYNC_WITH_SHM_FIFO
// sometimes we need to exchange bigger messages (e.g. for VST parameter dumps)
// so set a usable value here
const int SHM_FIFO_SIZE = 512*1024;

// TODO: reformat and refactor
// Don't rely on QSystemSemaphore
// implements a FIFO inside a shared memory segment
class ShmFifo
{
	// need this union to handle different sizes of sem_t on 32 bit
	// and 64 bit platforms
	union sem32_t
	{
		int semKey;
		char fill[32];
	};
	struct ShmData
	{
		sem32_t dataSem;	// semaphore for locking this
					// FIFO management data
		sem32_t messageSem;	// semaphore for incoming messages
		volatile int32_t startPtr; // current start of FIFO in memory
		volatile int32_t endPtr;   // current end of FIFO in memory
		char data[SHM_FIFO_SIZE];  // actual data
	};

public:
	// constructor for master-side
	ShmFifo() :
		m_invalid(false),
		m_master(true),
		m_shmKey(0),
		m_shmObj(),
		m_data(nullptr),
		m_dataSem(QString()),
		m_messageSem(QString()),
		m_lockDepth(0)
	{
		m_data = (ShmData *)createShmWithFreeKey(m_shmObj, sizeof(ShmData), m_shmKey);

		assert(m_data != NULL);
		m_data->startPtr = m_data->endPtr = 0;
		static int k = 0;
		m_data->dataSem.semKey = (getpid()<<10) + ++k;
		m_data->messageSem.semKey = (getpid()<<10) + ++k;
		m_dataSem.setKey(QString::number(m_data->dataSem.semKey),
						1, QSystemSemaphore::Create);
		m_messageSem.setKey(QString::number(
						m_data->messageSem.semKey),
						0, QSystemSemaphore::Create);
	}

	// constructor for remote-/client-side - use shmKey for making up
	// the connection to master
	ShmFifo(key_t shmKey) :
		m_invalid(false),
		m_master(false),
		m_shmKey(0),
		m_shmObj(shmKey),
		m_data(NULL),
		m_dataSem(QString()),
		m_messageSem(QString()),
		m_lockDepth(0)
	{
		m_data = (ShmData*)m_shmObj.attach();
		assert(m_data != NULL);
		m_dataSem.setKey(QString::number(m_data->dataSem.semKey));
		m_messageSem.setKey(QString::number(
						m_data->messageSem.semKey));
	}

	~ShmFifo()
	{
	}

	inline bool isInvalid() const
	{
		return m_invalid;
	}

	void invalidate()
	{
		m_invalid = true;
	}

	// do we act as master (i.e. not as remote-process?)
	inline bool isMaster() const
	{
		return m_master;
	}

	// recursive lock
	inline void lock()
	{
		if (!isInvalid() && m_lockDepth.fetch_add(1) == 0)
		{
			m_dataSem.acquire();
		}
	}

	// recursive unlock
	inline void unlock()
	{
		if (m_lockDepth.fetch_sub(1) <= 1)
		{
			m_dataSem.release();
		}
	}

	// wait until message-semaphore is available
	inline void waitForMessage()
	{
		if (!isInvalid())
		{
			m_messageSem.acquire();
		}
	}

	// increase message-semaphore
	inline void messageSent()
	{
		m_messageSem.release();
	}


	inline int32_t readInt()
	{
		int32_t i;
		read(&i, sizeof(i));
		return i;
	}

	inline void writeInt(const int32_t &i)
	{
		write(&i, sizeof(i));
	}

	inline std::string readString()
	{
		const int len = readInt();
		if (len)
		{
			char * sc = new char[len + 1];
			read(sc, len);
			sc[len] = 0;
			std::string s(sc);
			delete[] sc;
			return s;
		}
		return std::string();
	}


	inline void writeString(const std::string &s)
	{
		const int len = s.size();
		writeInt(len);
		write(s.c_str(), len);
	}


	inline bool messagesLeft()
	{
		if (isInvalid())
		{
			return false;
		}
		lock();
		const bool empty = (m_data->startPtr == m_data->endPtr);
		unlock();
		return !empty;
	}


	inline int shmKey() const
	{
		return m_shmKey;
	}


private:
	static inline void fastMemCpy(void * dest, const void * src,
							const int len)
	{
		// calling memcpy() for just an integer is obsolete overhead
		if (len == 4)
		{
			*((int32_t *) dest) = *((int32_t *) src);
		}
		else
		{
			memcpy(dest, src, len);
		}
	}

	void read(void * buf, int len)
	{
		if (isInvalid())
		{
			memset(buf, 0, len);
			return;
		}
		lock();
		while (isInvalid() == false &&
				len > m_data->endPtr - m_data->startPtr)
		{
			unlock();
#ifndef LMMS_BUILD_WIN32
			usleep(5);
#endif
			lock();
		}
		fastMemCpy(buf, m_data->data + m_data->startPtr, len);
		m_data->startPtr += len;
		// nothing left?
		if (m_data->startPtr == m_data->endPtr)
		{
			// then reset to 0
			m_data->startPtr = m_data->endPtr = 0;
		}
		unlock();
	}

	void write(const void * buf, int len)
	{
		if (isInvalid() || len > SHM_FIFO_SIZE)
		{
			return;
		}
		lock();
		while (len > SHM_FIFO_SIZE - m_data->endPtr)
		{
			// if no space is left, try to move data to front
			if (m_data->startPtr > 0)
			{
				memmove(m_data->data,
					m_data->data + m_data->startPtr,
					m_data->endPtr - m_data->startPtr);
				m_data->endPtr = m_data->endPtr -
							m_data->startPtr;
				m_data->startPtr = 0;
			}
			unlock();
#ifndef LMMS_BUILD_WIN32
			usleep(5);
#endif
			lock();
		}
		fastMemCpy(m_data->data + m_data->endPtr, buf, len);
		m_data->endPtr += len;
		unlock();
	}

	volatile bool m_invalid;
	bool m_master;
	key_t m_shmKey;
	SharedMemory m_shmObj;
	ShmData * m_data;
	QSystemSemaphore m_dataSem;
	QSystemSemaphore m_messageSem;
	std::atomic_int m_lockDepth;
};
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <unistd.h>

/*
// ================================
	struct sockaddr_un sa;
	sa.sun_family = AF_LOCAL;

	m_socketFile = QDir::tempPath() + QDir::separator() +
						QUuid::createUuid().toString();
	auto path = m_socketFile.toUtf8();
	size_t length = path.length();
	if (length >= sizeof sa.sun_path)
	{
		length = sizeof sa.sun_path - 1;
		qWarning("Socket path too long.");
	}
	memcpy(sa.sun_path, path.constData(), length);
	sa.sun_path[length] = '\0';

	m_server = socket(PF_LOCAL, SOCK_STREAM, 0);
	if (m_server == -1)
	{
		qWarning("Unable to start the server.");
	}
	remove(path.constData());
	int ret = bind(m_server, (struct sockaddr *) &sa, sizeof sa);
	if (ret == -1 || listen(m_server, 1) == -1)
	{
		qWarning("Unable to start the server.");
	}

// ================================
	if (close(m_server) == -1)
	{
		qWarning("Error freeing resources.");
	}
	remove(m_socketFile.toUtf8().constData());
// ===============================

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
			qWarning("Unexpected poll error.");
			break;

		case 0:
			qWarning("Remote plugin did not connect.");
			break;

		default:
			m_socket = accept(m_server, NULL, NULL);
			if (m_socket == -1)
			{
				qWarning("Unexpected socket error.");
			}
		}
		++retryCount;
	} while (result == -1 && retryCount <= 3);
// =================================

	inline int32_t readInt()
	{
		int32_t i;
		read(&i, sizeof(i));
		return i;
	}

	inline void writeInt(const int32_t & _i)
	{
		write(&_i, sizeof(_i));
	}

	inline std::string readString()
	{
		const int len = readInt();
		if (len)
		{
			char * sc = new char[len + 1];
			read(sc, len);
			sc[len] = 0;
			std::string s(sc);
			delete[] sc;
			return s;
		}
		return std::string();
	}


	inline void writeString(const std::string & _s)
	{
		const int len = _s.size();
		writeInt(len);
		write(_s.c_str(), len);
	}
// ==============================

		struct pollfd pollin;
		pollin.fd = m_socket;
		pollin.events = POLLIN;

		if (poll(&pollin, 1, 0) == -1)
		{
			qWarning("Unexpected poll error.");
		}
		return pollin.revents & POLLIN;
// ================================

	int m_socket;
//================


	void read(void * buf, int len)
	{
		if (isInvalid())
		{
			memset(buf, 0, len);
			return;
		}
		char * buf = (char *) buf;
		int remaining = len;
		while (remaining)
		{
			ssize_t nread = ::read(m_socket, buf, remaining);
			switch (nread)
			{
				case -1:
					fprintf(stderr,
						"Error while reading.\n");
				case 0:
					invalidate();
					memset(buf, 0, len);
					return;
			}
			buf += nread;
			remaining -= nread;
		}
	}

	void write(const void * buf, int len)
	{
		if (isInvalid())
		{
			return;
		}
		const char * buf = (const char *) buf;
		int remaining = len;
		while (remaining)
		{
			ssize_t nwritten = ::write(m_socket, buf, remaining);
			switch (nwritten)
			{
				case -1:
					fprintf(stderr,
						"Error while writing.\n");
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
// =========================

	struct sockaddr_un sa;
	sa.sun_family = AF_LOCAL;

	size_t length = strlen(socketPath);
	if (length >= sizeof sa.sun_path)
	{
		length = sizeof sa.sun_path - 1;
		fprintf(stderr, "Socket path too long.\n");
	}
	memcpy(sa.sun_path, socketPath, length);
	sa.sun_path[length] = '\0';

	m_socket = socket(PF_LOCAL, SOCK_STREAM, 0);
	if (m_socket == -1)
	{
		fprintf(stderr, "Could not connect to local server.\n");
	}
	if (::connect(m_socket, (struct sockaddr *) &sa, sizeof sa) == -1)
	{
		fprintf(stderr, "Could not connect to local server.\n");
	}
*/
#endif // SYNC_WITH_SHM_FIFO

#endif // IPCHELPER_H
