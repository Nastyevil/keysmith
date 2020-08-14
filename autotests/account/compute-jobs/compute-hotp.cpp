/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2020 Johan Ouwerkerk <jm.ouwerkerk@gmail.com>
 */
#include "account/actions_p.h"

#include "../test-utils/secret.h"
#include "../../test-utils/spy.h"

#include <QSignalSpy>
#include <QTest>

class ComputeHotpTest: public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase(void);
    void testDefaults(void);
    void testDefaults_data(void);
private:
    accounts::AccountSecret m_secret;
};

// RFC test vector uses the key: 12345678901234567890
static QByteArray rfcSecret("12345678901234567890");

// the RFC test vector consists of 6-character tokens
static uint tokenLength = 6;

void ComputeHotpTest::initTestCase(void)
{
    QVERIFY2(test::useDummyPassword(&m_secret), "should be able to set up the master key");
}

void ComputeHotpTest::testDefaults(void)
{
    QFETCH(quint64, counter);

    std::optional<secrets::EncryptedSecret> tokenSecret = test::encrypt(&m_secret, rfcSecret);
    QVERIFY2(tokenSecret, "should be able to encrypt the token secret");

    accounts::ComputeHotp uut(&m_secret, *tokenSecret, tokenLength, counter, std::nullopt, false);
    QSignalSpy tokenGenerated(&uut, &accounts::ComputeHotp::otp);
    QSignalSpy jobFinished(&uut, &accounts::ComputeHotp::finished);

    uut.run();

    QVERIFY2(test::signal_eventually_emitted_once(tokenGenerated), "token should be generated by now");
    QVERIFY2(test::signal_eventually_emitted_once(jobFinished), "job should be finished by now");

    QTEST(tokenGenerated.at(0).at(0).toString(), "rfc-test-vector");
}

static void define_test_case(int k, const char *expected)
{

    QByteArray output(expected, tokenLength);
    QTest::newRow(qPrintable(QStringLiteral("RFC 4226 test vector, counter value = %1").arg(k))) << (quint64) k << QString::fromLocal8Bit(output);
}

void ComputeHotpTest::testDefaults_data(void)
{
    static const char * corpus[10] {
        "755224",
        "287082",
        "359152",
        "969429",
        "338314",
        "254676",
        "287922",
        "162583",
        "399871",
        "520489"
    };

    QTest::addColumn<quint64>("counter");
    QTest::addColumn<QString>("rfc-test-vector");

    for (int k = 0; k < 10; ++k) {
        define_test_case(k, corpus[k]);
    }
}

QTEST_MAIN(ComputeHotpTest)

#include "compute-hotp.moc"

