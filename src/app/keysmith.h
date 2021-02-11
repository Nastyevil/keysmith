/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2020-2021 Johan Ouwerkerk <jm.ouwerkerk@gmail.com>
 */
#ifndef APP_KEYSMITH_H
#define APP_KEYSMITH_H

#include "../account/account.h"
#include "../model/accounts.h"
#include "../model/password.h"

#include <QMetaEnum>
#include <QObject>
#include <QQmlEngine>

namespace app
{
    class Navigation: public QObject
    {
        Q_OBJECT
    public:
        enum Page
        {
            Error,
            AddAccount,
            RenameAccount,
            AccountsOverview,
            SetupPassword,
            UnlockAccounts
        };
        Q_ENUM(Page)
    public:
        explicit Navigation(QQmlEngine * const engine);
        Q_INVOKABLE QString name(app::Navigation::Page page) const;
    public Q_SLOTS:
        void push(app::Navigation::Page page, QObject *modelToTransfer);
        void navigate(app::Navigation::Page page, QObject *modelToTransfer);
    Q_SIGNALS:
        void routed(const QString &route, QObject *transferred);
        void pushed(const QString &route, QObject *transferred);
    private:
        QQmlEngine * const m_engine;
    };

    class Keysmith: public QObject
    {
        Q_OBJECT
        Q_PROPERTY(app::Navigation * navigation READ navigation CONSTANT)
    public:
        explicit Keysmith(Navigation * const navigation, QObject *parent = nullptr);
        virtual ~Keysmith();
        Navigation * navigation(void) const;
        Q_INVOKABLE void copyToClipboard(const QString &text);
        Q_INVOKABLE model::SimpleAccountListModel * accountListModel(void);
        Q_INVOKABLE model::PasswordRequest * passwordRequest(void);
    private:
        accounts::AccountStorage * storage(void);
    private:
        Navigation * const m_navigation;
        accounts::AccountStorage *m_storage;
    };
}

Q_DECLARE_METATYPE(app::Navigation *);

#endif
