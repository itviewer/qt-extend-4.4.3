/****************************************************************************
**
** Copyright (C) 2008 Nokia Corporation and/or its subsidiary(-ies).
** Contact: Qt Software Information (qt-info@nokia.com)
**
** This file is part of the QtGui module of the Qt Toolkit.
**
** Commercial Usage
** Licensees holding valid Qt Commercial licenses may use this file in
** accordance with the Qt Commercial License Agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Nokia.
**
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License versions 2.0 or 3.0 as published by the Free
** Software Foundation and appearing in the file LICENSE.GPL included in
** the packaging of this file.  Please review the following information
** to ensure GNU General Public Licensing requirements will be met:
** http://www.fsf.org/licensing/licenses/info/GPLv2.html and
** http://www.gnu.org/copyleft/gpl.html.
**
** Qt for Windows(R) Licensees
** As a special exception, Nokia, as the sole copyright holder for Qt
** Designer, grants users of the Qt/Eclipse Integration plug-in the
** right for the Qt/Eclipse Integration to link to functionality
** provided by Qt Designer and its related libraries.
**
** If you are unsure which license is appropriate for your use, please
** contact the sales department at qt-sales@nokia.com.
**
****************************************************************************/

#include "qplatformdefs.h"
#include "qwssocket_qws.h"

#ifndef QT_NO_QWS_MULTIPROCESS

#include <fcntl.h>
#include <netdb.h>
#include <errno.h>
#include <stdio.h>
#include <sys/file.h>
#include <sys/time.h>
#include <sys/un.h>

#ifdef __MIPSEL__
# ifndef SOCK_DGRAM
#  define SOCK_DGRAM 1
# endif
# ifndef SOCK_STREAM
#  define SOCK_STREAM 2
# endif
#endif

#if defined(Q_OS_SOLARIS) || defined (QT_LSB)
// uff-da apparently Solaris doesn't have the SUN_LEN macro, here is
// an implementation of it...
#  ifndef SUN_LEN
#    define SUN_LEN(su) \
            sizeof(*(su)) - sizeof((su)->sun_path) + strlen((su)->sun_path)
#  endif

// nor the POSIX names of UNIX domain sockets *sigh*
#  ifndef AF_LOCAL
#    define AF_LOCAL        AF_UNIX
#  endif
#  ifndef PF_LOCAL
#    define PF_LOCAL        PF_UNIX
#  endif
#endif // Q_OS_SOLARIS || QT_LSB

QT_BEGIN_NAMESPACE

/***********************************************************************
 *
 * QWSSocket
 *
 **********************************************************************/
QWSSocket::QWSSocket(QObject *parent)
    : QWS_SOCK_BASE(parent)
{
#ifndef QT_NO_SXE
    QObject::connect( this, SIGNAL(stateChanged(SocketState)),
            this, SLOT(forwardStateChange(SocketState)));
#endif
}

QWSSocket::~QWSSocket()
{
}

#ifndef QT_NO_SXE
QString QWSSocket::errorString()
{
    switch (QUnixSocket::error()) {
    case NoError:
        return QString();
    case InvalidPath:
    case NonexistentPath:
        return QLatin1String("Bad path"); // NO_TR
    default:
        return QLatin1String("Bad socket"); // NO TR
    }
}

void QWSSocket::forwardStateChange(QUnixSocket::SocketState st  )
{
    switch ( st )
    {
        case ConnectedState:
            emit connected();
            break;
        case ClosingState:
            break;
        case UnconnectedState:
            emit disconnected();
            break;
        default:
            // nothing
            break;
    }
    if ( QUnixSocket::error() != NoError )
        emit error((QAbstractSocket::SocketError)0);
}
#endif

bool QWSSocket::connectToLocalFile(const QString &file)
{
#ifndef QT_NO_SXE
    bool result = QUnixSocket::connect( file.toLocal8Bit() );
    if ( !result )
    {
        perror( "QWSSocketAuth::connectToLocalFile could not connect:" );
        emit error(QAbstractSocket::ConnectionRefusedError);
        return false;
    }
    return true;
#else
    // create socket
    int s = ::socket(PF_LOCAL, SOCK_STREAM, 0);

    // connect to socket
    struct sockaddr_un a;
    memset(&a, 0, sizeof(a));
    a.sun_family = PF_LOCAL;
    strncpy(a.sun_path, file.toLocal8Bit().constData(), sizeof(a.sun_path) - 1);
    int r = ::connect(s, (struct sockaddr*)&a, SUN_LEN(&a));
    if (r == 0) {
        setSocketDescriptor(s);
    } else {
        perror("QWSSocket::connectToLocalFile could not connect:");
        ::close(s);
        emit error(ConnectionRefusedError);
        return false;
    }
#endif
    return true;
}


/***********************************************************************
 *
 * QWSServerSocket
 *
 **********************************************************************/
QWSServerSocket::QWSServerSocket(const QString& file, QObject *parent)
#ifndef QT_NO_SXE
    : QUnixSocketServer(parent)
#else
    : QTcpServer(parent)
#endif
{
    init(file);
}

void QWSServerSocket::init(const QString &file)
{
#ifndef QT_NO_SXE
    QByteArray fn = file.toLocal8Bit();
    bool result = QUnixSocketServer::listen( fn );
    if ( !result )
    {
        QUnixSocketServer::ServerError err = serverError();
        switch ( err )
        {
            case InvalidPath:
                qWarning("QWSServerSocket:: invalid path %s", qPrintable(file));
                break;
            case ResourceError:
            case BindError:
            case ListenError:
                qWarning("QWSServerSocket:: could not listen on path %s", qPrintable(file));
                break;
            default:
                break;
        }
    }
#else
    int backlog = 16; //#####

// create socket
    int s = ::socket(PF_LOCAL, SOCK_STREAM, 0);
    if (s == -1) {
        perror("QWSServerSocket::init");
        qWarning("QWSServerSocket: unable to create socket.");
        return;
    }

    QByteArray fn = file.toLocal8Bit();
    unlink(fn.constData()); // doesn't have to succeed

    // bind socket
    struct sockaddr_un a;
    memset(&a, 0, sizeof(a));
    a.sun_family = PF_LOCAL;
    strncpy(a.sun_path, fn.constData(), sizeof(a.sun_path) - 1);
    int r = ::bind(s, (struct sockaddr*)&a, SUN_LEN(&a));
    if (r < 0) {
        perror("QWSServerSocket::init");
        qWarning("QWSServerSocket: could not bind to file %s", fn.constData());
        ::close(s);
        return;
    }

    if (chmod(fn.constData(), 0600) < 0) {
        perror("QWSServerSocket::init");
        qWarning("Could not set permissions of %s", fn.constData());
        ::close(s);
        return;
    }

    // listen
    if (::listen(s, backlog) == 0) {
        if (!setSocketDescriptor(s))
            qWarning( "QWSServerSocket could not set descriptor %d : %s", s, errorString().toLatin1().constData());
    } else {
        perror("QWSServerSocket::init");
        qWarning("QWSServerSocket: could not listen to file %s", fn.constData());
        ::close(s);
    }
#endif
}

QWSServerSocket::~QWSServerSocket()
{
}

#ifndef QT_NO_SXE

void QWSServerSocket::incomingConnection(int socketDescriptor)
{
    inboundConnections.append( socketDescriptor );
    emit newConnection();
}


QWSSocket *QWSServerSocket::nextPendingConnection()
{
    QMutexLocker locker( &ssmx );
    if ( inboundConnections.count() == 0 )
        return 0;
    QWSSocket *s = new QWSSocket();
    s->setSocketDescriptor( inboundConnections.takeFirst() );
    return s;
}

#endif // QT_NO_SXE

QT_END_NAMESPACE

#endif  //QT_NO_QWS_MULTIPROCESS