/*
 * Copyright (c) 2018 boud <boud@valdyas.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */
#include "TestResourceModel.h"

#include <QTest>
#include <QStandardPaths>
#include <QDir>
#include <QVersionNumber>
#include <QDirIterator>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryFile>
#include <QDir>

#include <kconfig.h>
#include <kconfiggroup.h>
#include <ksharedconfig.h>

#include <KisResourceCacheDb.h>
#include <KisResourceLocator.h>
#include <KisResourceLoaderRegistry.h>
#include <KisResourceModel.h>

#include <DummyResource.h>
#include <ResourceTestHelper.h>

#ifndef FILES_DATA_DIR
#error "FILES_DATA_DIR not set. A directory with the data used for testing installing resources"
#endif

#ifndef FILES_DEST_DIR
#error "FILES_DEST_DIR not set. A directory where data will be written to for testing installing resources"
#endif


void TestResourceModel::initTestCase()
{
    ResourceTestHelper::initTestDb();
    ResourceTestHelper::createDummyLoaderRegistry();

    m_srcLocation = QString(FILES_DATA_DIR);
    QVERIFY2(QDir(m_srcLocation).exists(), m_srcLocation.toUtf8());

    m_dstLocation = QString(FILES_DEST_DIR);
    ResourceTestHelper::cleanDstLocation(m_dstLocation);

    KConfigGroup cfg(KSharedConfig::openConfig(), "");
    cfg.writeEntry(KisResourceLocator::resourceLocationKey, m_dstLocation);

    m_locator = KisResourceLocator::instance();

    if (!KisResourceCacheDb::initialize(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))) {
        qDebug() << "Could not initialize KisResourceCacheDb on" << QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }
    QVERIFY(KisResourceCacheDb::isValid());

    KisResourceLocator::LocatorError r = m_locator->initialize(m_srcLocation);
    if (!m_locator->errorMessages().isEmpty()) qDebug() << m_locator->errorMessages();

    QVERIFY(r == KisResourceLocator::LocatorError::Ok);
    QVERIFY(QDir(m_dstLocation).exists());
}


void TestResourceModel::testRowCount()
{
    QSqlQuery q;
    QVERIFY(q.prepare("SELECT count(*)\n"
                      "FROM   resources\n"
                      ",      resource_types\n"
                      "WHERE  resources.resource_type_id = resource_types.id\n"
                      "AND    resource_types.name = :resource_type"));
    q.bindValue(":resource_type", m_resourceType);
    QVERIFY(q.exec());
    q.first();
    int rowCount = q.value(0).toInt();
    QVERIFY(rowCount == 3);
    KisResourceModel resourceModel(m_resourceType);
    QCOMPARE(resourceModel.rowCount(), rowCount);
}

void TestResourceModel::testData()
{
    KisResourceModel resourceModel(m_resourceType);

    QStringList resourceNames;

    for (int i = 0; i < resourceModel.rowCount(); ++i)  {
        QVariant v = resourceModel.data(resourceModel.index(i, KisResourceModel::Name), Qt::DisplayRole);
        resourceNames << v.toString();
    }
    QVERIFY(resourceNames.contains("test0.kpp"));
    QVERIFY(resourceNames.contains("test1.kpp"));
    QVERIFY(resourceNames.contains("test2.kpp"));
}


void TestResourceModel::testResourceForIndex()
{
    KisResourceModel resourceModel(m_resourceType);
    KoResourceSP resource = resourceModel.resourceForIndex(resourceModel.index(0, 0));
    QVERIFY(resource);
}

void TestResourceModel::testIndexFromResource()
{
    KisResourceModel resourceModel(m_resourceType);
    KoResourceSP resource = resourceModel.resourceForIndex(resourceModel.index(1, 0));
    QModelIndex idx = resourceModel.indexFromResource(resource);
    QVERIFY(idx.row() == 1);
    QVERIFY(idx.column() == 0);
}

void TestResourceModel::testRemoveResourceByIndex()
{
    KisResourceModel resourceModel(m_resourceType);
    int resourceCount = resourceModel.rowCount();
    KoResourceSP resource = resourceModel.resourceForIndex(resourceModel.index(1, 0));
    bool r = resourceModel.removeResource(resourceModel.index(1, 0));
    QVERIFY(r);
    QCOMPARE(resourceCount - 1, resourceModel.rowCount());
    QVERIFY(!resourceModel.indexFromResource(resource).isValid());

}

void TestResourceModel::testRemoveResource()
{
    KisResourceModel resourceModel(m_resourceType);
    int resourceCount = resourceModel.rowCount();

    KoResourceSP resource = resourceModel.resourceForIndex(resourceModel.index(1, 0));
    QVERIFY(resource);

    bool r = resourceModel.removeResource(resource);
    QVERIFY(r);
    QCOMPARE(resourceCount - 1, resourceModel.rowCount());

    QVERIFY(!resourceModel.indexFromResource(resource).isValid());
}

void TestResourceModel::testImportResourceFile()
{
    KisResourceModel resourceModel(m_resourceType);

    QTemporaryFile f(QDir::tempPath() + "/testresourcemodel-testimportresourcefile-XXXXXX.kpp");
    f.open();
    f.write("0");
    f.close();

    int resourceCount = resourceModel.rowCount();
    bool r = resourceModel.importResourceFile(f.fileName());
    QVERIFY(r);
    QCOMPARE(resourceCount + 1, resourceModel.rowCount());
}

void TestResourceModel::testAddResource()
{
    KisResourceModel resourceModel(m_resourceType);
    int resourceCount = resourceModel.rowCount();
    KoResourceSP resource(new DummyResource("dummy.kpp"));
    resource->setValid(true);
    bool r = resourceModel.addResource(resource);
    QVERIFY(r);
    QCOMPARE(resourceCount + 1, resourceModel.rowCount());
}

void TestResourceModel::testAddTemporaryResource()
{
    KisResourceModel resourceModel(m_resourceType);
    int resourceCount = resourceModel.rowCount();
    KoResourceSP resource(new DummyResource("dummy.kpp"));
    bool r = resourceModel.addResource(resource, false);
    QVERIFY(r);
    QCOMPARE(resourceCount + 1, resourceModel.rowCount());
}

void TestResourceModel::testUpdateResource()
{
    KisResourceModel resourceModel(m_resourceType);
    KoResourceSP resource = resourceModel.resourceForIndex(resourceModel.index(0, 0));
    QVERIFY(resource);
    bool r = resourceModel.updateResource(resource);
    QVERIFY(r);
}


void TestResourceModel::cleanupTestCase()
{
    ResourceTestHelper::rmTestDb();
    ResourceTestHelper::cleanDstLocation(m_dstLocation);
}




QTEST_MAIN(TestResourceModel)

