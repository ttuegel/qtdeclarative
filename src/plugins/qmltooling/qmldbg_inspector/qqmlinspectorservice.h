/****************************************************************************
**
** Copyright (C) 2015 The Qt Company Ltd.
** Contact: http://www.qt.io/licensing/
**
** This file is part of the QtQml module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL21$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see http://www.qt.io/terms-conditions. For further
** information use the contact form at http://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 or version 3 as published by the Free
** Software Foundation and appearing in the file LICENSE.LGPLv21 and
** LICENSE.LGPLv3 included in the packaging of this file. Please review the
** following information to ensure the GNU Lesser General Public License
** requirements will be met: https://www.gnu.org/licenses/lgpl.html and
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** As a special exception, The Qt Company gives you certain additional
** rights. These rights are described in The Qt Company LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#ifndef QQMLINSPECTORSERVICE_H
#define QQMLINSPECTORSERVICE_H

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API.  It exists purely as an
// implementation detail.  This header file may change from version to
// version without notice, or even be removed.
//
// We mean it.
//

#include <private/qqmldebugserviceinterfaces_p.h>
#include <private/qqmldebugservicefactory_p.h>

#include <QtQml/qtqmlglobal.h>
#include <QtCore/QList>

QT_BEGIN_NAMESPACE

namespace QmlJSDebugger { class AbstractViewInspector; }

class QQmlInspectorInterface;

class QQmlInspectorServiceImpl : public QQmlInspectorService
{
    Q_OBJECT

public:
    QQmlInspectorServiceImpl(QObject *parent = 0);

    void addView(QObject *);
    void removeView(QObject *);

protected:
    virtual void stateChanged(State state);
    virtual void messageReceived(const QByteArray &);

private Q_SLOTS:
    void processMessage(const QByteArray &message);
    void updateState();

private:
    friend class QmlJSDebugger::AbstractViewInspector;
    friend class QQmlInspectorServiceFactory;
    void loadInspectorPlugins();

    QList<QObject*> m_views;
    QQmlInspectorInterface *m_currentInspectorPlugin;
    QList<QQmlInspectorInterface*> m_inspectorPlugins;
};

class QQmlInspectorServiceFactory : public QQmlDebugServiceFactory
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QQmlDebugServiceFactory_iid FILE "qqmlinspectorservice.json")
public:
    QQmlDebugService *create(const QString &key);
};

QT_END_NAMESPACE

#endif // QQMLINSPECTORSERVICE_H