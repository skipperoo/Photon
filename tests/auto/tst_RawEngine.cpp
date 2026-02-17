#include <QtTest>
#include <QSignalSpy>
#include "RawEngine.h"

class TestRawEngine : public QObject
{
    Q_OBJECT

private slots:
    void testLoadInvalidFile();
    void testLoadValidFile();
};

void TestRawEngine::testLoadInvalidFile()
{
    RawEngine engine;
    QSignalSpy errorSpy(&engine, &RawEngine::errorOccurred);
    engine.setSource("non_existent.arw");
    
    // Wait for the async load to finish (it should fail)
    QTRY_VERIFY_WITH_TIMEOUT(errorSpy.count() > 0, 5000);
}

void TestRawEngine::testLoadValidFile()
{
    // Skip for now if we don't have a valid RAW file for testing
    // QSKIP("No valid RAW file available for testing");
}

QTEST_MAIN(TestRawEngine)
#include "tst_RawEngine.moc"
