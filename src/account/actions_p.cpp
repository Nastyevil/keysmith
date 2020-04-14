/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2020 Johan Ouwerkerk <jm.ouwerkerk@gmail.com>
 */
#include "actions_p.h"
#include "validation.h"

#include "../base32/base32.h"
#include "../logging_p.h"
#include "../oath/oath.h"

#include <QTimer>

KEYSMITH_LOGGER(logger, ".accounts.actions")
KEYSMITH_LOGGER(dispatcherLogger, ".accounts.dispatcher")

namespace accounts
{
    AccountJob::AccountJob() : QObject()
    {
    }

    AccountJob::~AccountJob()
    {
    }

    Null::Null() : AccountJob()
    {
    }

    void Null::run(void)
    {
        Q_EMIT finished();
    }

    void AccountJob::run(void)
    {
        Q_ASSERT_X(false, Q_FUNC_INFO, "should be overridden in derived classes!");
    }

    LoadAccounts::LoadAccounts(const SettingsProvider &settings) : AccountJob(), m_settings(settings)
    {
    }

    DeleteAccounts::DeleteAccounts(const SettingsProvider &settings, const QSet<QUuid> &ids) : AccountJob(), m_settings(settings), m_ids(ids)
    {
    }

    SaveHotp::SaveHotp(const SettingsProvider &settings, const QUuid &id, const QString &accountName, const QString &secret, quint64 counter, int tokenLength) :
        AccountJob(), m_settings(settings), m_id(id), m_accountName(accountName), m_secret(secret), m_counter(counter), m_tokenLength(tokenLength)
    {
    }

    SaveTotp::SaveTotp(const SettingsProvider &settings, const QUuid &id, const QString &accountName, const QString &secret, uint timeStep, int tokenLength) :
        AccountJob(), m_settings(settings), m_id(id), m_accountName(accountName), m_secret(secret), m_timeStep(timeStep), m_tokenLength(tokenLength)
    {
    }

    void SaveHotp::run(void)
    {
        if (!checkHotpAccount(m_id, m_accountName, m_secret, m_tokenLength)) {
            qCDebug(logger)
                << "Unable to save HOTP account:" << m_id
                << "Invalid account details";
            Q_EMIT invalid();
            Q_EMIT finished();
            return;
        }

        const PersistenceAction act([this](QSettings &settings) -> void
        {
            if (!settings.isWritable()) {
                qCWarning(logger)
                    << "Unable to save HOTP account:" << m_id
                    << "Storage not writable";
                Q_EMIT invalid();
                return;
            }

            qCInfo(logger) << "Saving HOTP account:" << m_id;

            const QString group = m_id.toString();
            settings.remove(group);
            settings.beginGroup(group);
            settings.setValue("account", m_accountName);
            settings.setValue("type", "hotp");
            settings.setValue("secret", m_secret);
            settings.setValue("counter", m_counter);
            settings.setValue("pinLength", m_tokenLength);
            settings.endGroup();

            // Try to guarantee that data will have been written before claiming the account was actually saved
            settings.sync();

            Q_EMIT saved(m_id, m_accountName, m_secret, m_counter, m_tokenLength);
        });
        m_settings(act);

        Q_EMIT finished();
    }

    void SaveTotp::run(void)
    {
        if (!checkTotpAccount(m_id, m_accountName, m_secret, m_tokenLength, m_timeStep)) {
            qCDebug(logger)
                << "Unable to save TOTP account:" << m_id
                << "Invalid account details";
            Q_EMIT invalid();
            Q_EMIT finished();
            return;
        }

        const PersistenceAction act([this](QSettings &settings) -> void
        {
            if (!settings.isWritable()) {
                qCWarning(logger)
                    << "Unable to save TOTP account:" << m_id
                    << "Storage not writable";
                Q_EMIT invalid();
                return;
            }

            qCInfo(logger) << "Saving TOTP account:" << m_id;

            const QString group = m_id.toString();
            settings.remove(group);
            settings.beginGroup(group);
            settings.setValue("account", m_accountName);
            settings.setValue("type", "totp");
            settings.setValue("secret", m_secret);
            settings.setValue("timeStep", m_timeStep);
            settings.setValue("pinLength", m_tokenLength);
            settings.endGroup();

            // Try to guarantee that data will have been written before claiming the account was actually saved
            settings.sync();

            Q_EMIT saved(m_id, m_accountName, m_secret, m_timeStep, m_tokenLength);
        });
        m_settings(act);

        Q_EMIT finished();
    }

    void DeleteAccounts::run(void)
    {
        const PersistenceAction act([this](QSettings &settings) -> void
        {
            if (!settings.isWritable()) {
                qCWarning(logger) << "Unable to delete accounts: storage not writable";
                Q_EMIT invalid();
                return;
            }

            qCInfo(logger) << "Deleting accounts";

            for (const QUuid &id : m_ids) {
                settings.remove(id.toString());
            }
        });
        m_settings(act);

        Q_EMIT finished();
    }

    void LoadAccounts::run(void)
    {
        const PersistenceAction act([this](QSettings &settings) -> void
        {
            qCInfo(logger, "Loading accounts from storage");
            const QStringList entries = settings.childGroups();
            for (const QString &group : entries) {
                const QUuid id(group);

                if (id.isNull()) {
                    qCDebug(logger)
                        << "Ignoring:" << group
                        << "Not an account section";
                    continue;
                }

                bool ok = false;
                settings.beginGroup(group);

                const QString secret = settings.value("secret").toString();
                const QString accountName = settings.value("account").toString();
                const QString type = settings.value("type", "hotp").toString();
                const int tokenLength = settings.value("pinLength").toInt(&ok);

                if (!ok || (type != "hotp" && type != "totp")) {
                    qCWarning(logger) << "Skipping invalid account:" << id;
                    settings.endGroup();
                    continue;
                }

                if (type == "totp") {
                    ok = false;
                    const uint timeStep = settings.value("timeStep").toUInt(&ok);
                    if (ok && checkTotpAccount(id, accountName, secret, tokenLength, timeStep)) {
                        qCInfo(logger) << "Found valid TOTP account:" << id;
                        Q_EMIT foundTotp(
                            id,
                            accountName,
                            secret,
                            timeStep,
                            tokenLength
                        );
                    }
                }
                if (type == "hotp") {
                    ok = false;
                    const quint64 counter = settings.value("counter").toULongLong(&ok);
                    if (ok && checkHotpAccount(id, accountName, secret, tokenLength)) {
                        qCInfo(logger) << "Found valid HOTP account:" << id;
                        Q_EMIT foundHotp(
                            id,
                            accountName,
                            secret,
                            counter,
                            tokenLength
                        );
                    }
                }

                settings.endGroup();
            }
        });
        m_settings(act);

        Q_EMIT finished();
    }

    ComputeTotp::ComputeTotp(const QString &secret, const QDateTime &epoch, uint timeStep, int tokenLength, const Account::Hash &hash, const std::function<qint64(void)> &clock) :
        AccountJob(), m_secret(secret), m_epoch(epoch), m_timeStep(timeStep), m_tokenLength(tokenLength), m_hash(hash), m_clock(clock)
    {
    }

    void ComputeTotp::run(void)
    {
        if (!checkTotp(m_secret, m_tokenLength, m_timeStep)) {
            qCDebug(logger) << "Unable to compute TOTP token: invalid token details";
            Q_EMIT finished();
            return;
        }

        std::optional<QByteArray> secret = base32::decode(m_secret);
        if (!secret.has_value()) {
            qCDebug(logger) << "Unable to compute TOTP token: unable to decode secret";
            Q_EMIT finished();
            return;
        }

        QCryptographicHash::Algorithm hash;
        switch(m_hash)
        {
        case Account::Hash::Sha256:
            hash = QCryptographicHash::Sha256;
            break;
        case Account::Hash::Sha512:
            hash = QCryptographicHash::Sha512;
            break;
        case Account::Hash::Default:
            hash = QCryptographicHash::Sha1;
            break;
        default:
            qCDebug(logger) << "Unable to compute TOTP token: unknown hashing algorithm:" << m_hash;
            Q_EMIT finished();
            return;

        }

        const std::optional<oath::Algorithm> algorithm = oath::Algorithm::usingDynamicTruncation(hash, m_tokenLength);
        if (!algorithm) {
            qCDebug(logger) << "Unable to compute TOTP token: unable to set up truncation for token length:" << m_tokenLength;
            Q_EMIT finished();
            return;
        }

        const std::optional<quint64> counter = oath::count(m_epoch, m_timeStep, m_clock);
        if (!counter) {
            qCDebug(logger) << "Unable to compute TOTP token: unable to count time steps";
            Q_EMIT finished();
            return;
        }

        const std::optional<QString> token = algorithm->compute(*counter, secret->data(), secret->size());
        if (token) {
            Q_EMIT otp(*token);
        } else {
            qCDebug(logger) << "Failed to compute TOTP token";
        }

        Q_EMIT finished();
    }

    ComputeHotp::ComputeHotp(const QString &secret, quint64 counter, int tokenLength, int offset, bool checksum) :
        AccountJob(), m_secret(secret), m_counter(counter), m_tokenLength(tokenLength), m_offset(offset), m_checksum(checksum)
    {
    }

    void ComputeHotp::run(void)
    {
        if (!checkHotp(m_secret, m_tokenLength)) {
            qCDebug(logger) << "Unable to compute HOTP token: invalid token details";
            Q_EMIT finished();
            return;
        }

        std::optional<QByteArray> secret = base32::decode(m_secret);
        if (!secret.has_value()) {
            qCDebug(logger) << "Unable to compute HOTP token: unable to decode secret";
            Q_EMIT finished();
            return;
        }

        const oath::Encoder encoder(m_tokenLength, m_checksum);
        const std::optional<oath::Algorithm> algorithm = m_offset >=0
            ? oath::Algorithm::usingTruncationOffset(QCryptographicHash::Sha1, (uint) m_offset, encoder)
            : oath::Algorithm::usingDynamicTruncation(QCryptographicHash::Sha1, encoder);
        if (!algorithm) {
            qCDebug(logger) << "Unable to compute HOTP token: unable to set up truncation for token length:" << m_tokenLength;
            Q_EMIT finished();
            return;
        }

        const std::optional<QString> token = algorithm->compute(m_counter, secret->data(), secret->size());
        if (token) {
            Q_EMIT otp(*token);
        } else {
            qCDebug(logger) << "Failed to compute HOTP token";
        }

        Q_EMIT finished();
    }

    Dispatcher::Dispatcher(QThread *thread, QObject *parent) : QObject(parent), m_thread(thread),  m_current(nullptr)
    {
    }

    bool Dispatcher::empty(void) const
    {
        return m_pending.isEmpty();
    }

    void Dispatcher::queueAndProceed(AccountJob *job, const std::function<void(void)> &setup_callbacks)
    {
        if (job) {
            qCDebug(dispatcherLogger) << "Queuing job for dispatcher";
            job->moveToThread(m_thread);
            setup_callbacks();
            m_pending.append(job);
            dispatchNext();
        }
    }

    void Dispatcher::dispatchNext(void)
    {
        qCDebug(dispatcherLogger) << "Handling request to dispatch next job";

        if (!empty() && !m_current) {
            qCDebug(dispatcherLogger) << "Dispatching next job";

            m_current = m_pending.takeFirst();
            QObject::connect(m_current, &AccountJob::finished, this, &Dispatcher::next);
            QObject::connect(this, &Dispatcher::dispatch, m_current, &AccountJob::run);
            Q_EMIT dispatch();
        }
    }

    void Dispatcher::next(void)
    {
        qCDebug(dispatcherLogger) << "Handling next continuation in dispatcher";

        QObject *from = sender();
        AccountJob *job = from ? qobject_cast<AccountJob*>(from) : nullptr;
        if (job) {
            Q_ASSERT_X(job == m_current, Q_FUNC_INFO, "sender() should match 'current' job!");
            QObject::disconnect(this, &Dispatcher::dispatch, job, &AccountJob::run);
            QTimer::singleShot(0, job, &AccountJob::deleteLater);
            m_current = nullptr;
            dispatchNext();
        }
    }
}
