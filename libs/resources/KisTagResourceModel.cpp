/*
 * SPDX-FileCopyrightText: 2020 Boudewijn Rempt <boud@valdyas.org>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#include "KisTagResourceModel.h"

#include <QtSql>
#include <KisResourceLocator.h>
#include <KisResourceModel.h>
#include <KisResourceModelProvider.h>
#include <KisResourceQueryMapper.h>
#include <KisStorageModel.h>
#include <kis_assert.h>

struct KisAllTagResourceModel::Private {
    QString resourceType;
    QSqlQuery query;
    int columnCount { TagName + 1 };
    int cachedRowCount {-1};
};


KisAllTagResourceModel::KisAllTagResourceModel(const QString &resourceType, QObject *parent)
    : QAbstractTableModel(parent)
    , d(new Private())
{
    d->resourceType = resourceType;
    resetQuery();

    connect(KisResourceLocator::instance(), SIGNAL(storageAdded(const QString&)), this, SLOT(addStorage(const QString&)));
    connect(KisResourceLocator::instance(), SIGNAL(storageRemoved(const QString&)), this, SLOT(removeStorage(const QString&)));
    connect(KisStorageModel::instance(), SIGNAL(storageEnabled(const QString&)), this, SLOT(addStorage(const QString&)));
    connect(KisStorageModel::instance(), SIGNAL(storageDisabled(const QString&)), this, SLOT(removeStorage(const QString&)));
}

KisAllTagResourceModel::~KisAllTagResourceModel()
{
    delete d;
}

int KisAllTagResourceModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }

    if (d->cachedRowCount < 0) {
        QSqlQuery q;
        q.prepare("SELECT COUNT(DISTINCT resource_tags.tag_id || resources.name || resources.filename || resources.md5sum)\n"
                  "FROM   resource_tags\n"
                  ",      resources\n"
                  ",      resource_types\n"
                  "WHERE  resource_tags.resource_id = resources.id\n"
                  "AND    resources.resource_type_id = resource_types.id\n"
                  "AND    resource_types.name = :resource_type");

        q.bindValue(":resource_type", d->resourceType);

        if (!q.exec()) {
            qWarning() << "Could not execute tags rowcount query" << q.lastError();
        }

        q.first();

        const_cast<KisAllTagResourceModel*>(this)->d->cachedRowCount = q.value(0).toInt();
    }
    return d->cachedRowCount;
}

int KisAllTagResourceModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }

    return d->columnCount;
}

QVariant KisAllTagResourceModel::data(const QModelIndex &index, int role) const
{
    QVariant v;

    if (!index.isValid()) { return v; }
    if (index.row() > rowCount()) { return v; }
    if (index.column() > d->columnCount) { return v;}

    bool pos = const_cast<KisAllTagResourceModel*>(this)->d->query.seek(index.row());
    if (!pos) {return v;}



    if (role < Qt::UserRole + TagId && index.column() < TagId) {
        int id = d-> query.value("resource_id").toInt();
        v = KisResourceQueryMapper::variantFromResourceQueryById(id, index.column(), role);
    }

    if (index.column() >= TagId) {
        // trick to get the correct value without writing everything again
        // this is used for example in case of sorting
        role = Qt::UserRole + index.column();
    }
    // These are not shown, but needed for the filter
    switch(role) {
    case Qt::UserRole + TagId:
    {
        v = d->query.value("tag_id");
        break;
    }
    case Qt::UserRole + ResourceId:
    {
        v = d->query.value("resource_id");
        break;
    }
    case Qt::UserRole + Tag:
    {
        KisTagSP tag(new KisTag());
        tag->setUrl(d->query.value("tag_url").toString());
        tag->setName(d->query.value("tag_name").toString());
        tag->setComment(d->query.value("tag_comment").toString());
        tag->setId(d->query.value("tag_id").toInt());
        tag->setActive(d->query.value("tag_active").toBool());
        tag->setResourceType(d->resourceType);
        tag->setValid(true);

        v = QVariant::fromValue(tag);
        break;
    }
    case Qt::UserRole + Resource:
    {
        v = QVariant::fromValue(KisResourceLocator::instance()->resourceForId(d->query.value("resource_id").toInt()));
        break;
    }
    case Qt::UserRole + ResourceActive:
    {
        v = d->query.value("resource_active");
        break;
    }
    case Qt::UserRole + TagActive:
    {
        v = d->query.value("tag_active");
        break;
    }
    case Qt::UserRole + ResourceStorageActive:
    {
        v = d->query.value("resource_storage_active");
        break;
    }
    case Qt::UserRole + ResourceName:
    {
        v = d->query.value("resource_name").toString();
        break;
    }
    case Qt::UserRole + TagName:
    {
        v = d->query.value("tag_name").toString();
        break;
    }

    default:
        ;
    }
    return v;
}

bool KisAllTagResourceModel::tagResource(const KisTagSP tag, const int resourceId)
{
    KIS_SAFE_ASSERT_RECOVER_RETURN_VALUE(resourceId >= 0, false);
    KIS_SAFE_ASSERT_RECOVER_RETURN_VALUE(tag && tag->valid() && tag->id() >= 0, false);

    if (isResourceTagged(tag, resourceId)) return true;

    beginInsertRows(QModelIndex(), rowCount(), rowCount());

    QSqlQuery q;
    if (!q.prepare("INSERT INTO resource_tags\n"
                   "(resource_id, tag_id)\n"
                   "VALUES (:resource_id,:tag_id);\n")) {
        qWarning() << "Could not prepare insert into resource tags statement" << q.lastError();
        return false;
    }

    q.bindValue(":resource_id", resourceId);
    q.bindValue(":tag_id", tag->id());

    if (!q.exec()) {
        qWarning() << "Could not execute insert into resource tags statement" << q.boundValues() << q.lastError();
        return false;
    }

    resetQuery();

    endInsertRows();

    return true;
}

bool KisAllTagResourceModel::untagResource(const KisTagSP tag, const int resourceId)
{
    if (resourceId < 0) return false;
    if (!tag || !tag->valid()) return false;
    if (!d->query.isSelect()) return false;
    if (rowCount() < 1) return false;

    if (!isResourceTagged(tag, resourceId)) return true; // we check it later but this is with sql, so probably faster

    d->query.seek(0);
    bool found = false;
    do {
        int tagId = d->query.value("tag_id").toInt();
        int existingResourceId = d->query.value("resource_id").toInt();
        if (tagId == tag->id() && existingResourceId == resourceId) {
            found = true;
            break;
        }

    } while (d->query.next());

    if (!found) {
        return true;
    }

    beginRemoveRows(QModelIndex(), d->query.at(), d->query.at());

    {
        QSqlQuery q;

        if (!q.prepare("DELETE FROM resource_tags\n"
                       "WHERE  tag_id = :tag_id\n"
                       "AND    resource_id = :resource_id")) {
            qWarning() << "Could not prepare untagResource query" << q.lastError();
            endRemoveRows();
            return false;
        }

        q.bindValue(":tag_id", tag->id());
        q.bindValue(":resource_id", resourceId);

        if (!q.exec()) {
            qWarning() << "Could not execute untagResource query" << q.lastError() << q.boundValues();
            endRemoveRows();
            return false;
        }

        resetQuery();
    }
    endRemoveRows();

    return true;
}

bool KisAllTagResourceModel::isResourceTagged(const KisTagSP tag, const int resourceId)
{
    QSqlQuery query;
    bool r = query.prepare("SELECT COUNT(*)\n"
                           "FROM   resource_tags\n"
                           "WHERE  resource_tags.resource_id = :resource_id\n"
                           "AND    resource_tags.tag_id = :tag_id\n");

    if (!r) {
        qWarning() << "Could not prepare bool KisAllTagResourceModel::isResourceTagged query" << query.lastError();
        return false;
    }

    query.bindValue(":resource_id", resourceId);
    query.bindValue(":tag_id", tag->id());

    if (!query.exec()) {
        qWarning() << "Could not execute is resource tagged with a specific tag query" << query.boundValues() << query.lastError();
        return false;
    }

    r = query.first();
    if (!r) {
        qWarning() << "Could not call query.first() on SELECT COUNT query in isResourceTagged";
        return false;
    }

    return query.value(0).toInt() > 0;

}

void KisAllTagResourceModel::addStorage(const QString &location)
{
    Q_UNUSED(location);
    beginInsertRows(QModelIndex(), rowCount(), rowCount());
    resetQuery();
    endInsertRows();
}

void KisAllTagResourceModel::removeStorage(const QString &location)
{
    Q_UNUSED(location);
    beginRemoveRows(QModelIndex(), rowCount(), rowCount());
    resetQuery();
    endRemoveRows();
}

bool KisAllTagResourceModel::resetQuery()
{
    bool r = d->query.prepare("SELECT tags.id                as tag_id\n"
                              ",      tags.url               as tag_url\n"
                              ",      tags.name              as tag_name\n"
                              ",      tags.comment           as tag_comment\n"
                              ",      tags.active            as tag_active\n"
                              ",      resources.id           as resource_id\n"
                              ",      resources.status       as resource_active\n"
                              ",      storages.active        as resource_storage_active\n"
                              ",      resources.name         as resource_name\n"
                              ",      resources.storage_id   as storage_id\n"
                              ",      storages.active        as storage_active\n"
                              "FROM   resources\n"
                              ",      resource_types\n"
                              ",      storages\n"
                              ",      tags\n"
                              ",      resource_tags\n"
                              "WHERE  tags.id                    = resource_tags.tag_id\n"
                              "AND    tags.resource_type_id      = resource_types.id\n"
                              "AND    resources.id               = resource_tags.resource_id\n"
                              "AND    resources.resource_type_id = resource_types.id\n"
                              "AND    resources.storage_id       = storages.id\n"
                              "AND    resource_types.id          = resources.resource_type_id\n"
                              "AND    resource_types.name        = :resource_type\n"
                              "GROUP BY tags.id\n"
                              ",        resources.name\n"
                              ",        resources.filename\n"
                              ",        resources.md5sum\n");

    if (!r) {
        qWarning() << "Could not prepare KisAllTagResourcesModel query" << d->query.lastError();
    }

    d->query.bindValue(":resource_type", d->resourceType);

    r = d->query.exec();

    if (!r) {
        qWarning() << "Could not execute KisAllTagResourcesModel query" << d->query.lastError();
    }

    d->cachedRowCount = -1;

    return r;
}


struct KisTagResourceModel::Private {
    QString resourceType;
    KisAllTagResourceModel *sourceModel {0};
    QVector<int> tagIds;
    QVector<int> resourceIds;
    TagFilter tagFilter {ShowActiveTags};
    StorageFilter storageFilter {ShowActiveStorages};
    ResourceFilter resourceFilter {ShowActiveResources};
};


KisTagResourceModel::KisTagResourceModel(const QString &resourceType, QObject *parent)
    : QSortFilterProxyModel(parent)
    , d(new Private())
{
    d->resourceType = resourceType;
    d->sourceModel = KisResourceModelProvider::tagResourceModel(resourceType);
    setSourceModel(d->sourceModel);

    connect(KisResourceLocator::instance(), SIGNAL(storageAdded(const QString &)), this, SLOT(storageChanged(const QString &)));
    connect(KisResourceLocator::instance(), SIGNAL(storageRemoved(const QString &)), this, SLOT(storageChanged(const QString &)));
    connect(KisStorageModel::instance(), SIGNAL(storageEnabled(const QString &)), this, SLOT(storageChanged(const QString &)));
    connect(KisStorageModel::instance(), SIGNAL(storageDisabled(const QString &)), this, SLOT(storageChanged(const QString &)));
}

KisTagResourceModel::~KisTagResourceModel()
{
    delete d;
}

void KisTagResourceModel::setTagFilter(KisTagResourceModel::TagFilter filter)
{
    d->tagFilter = filter;
    invalidateFilter();
}

void KisTagResourceModel::setResourceFilter(KisTagResourceModel::ResourceFilter filter)
{
    d->resourceFilter = filter;
    invalidateFilter();
}

void KisTagResourceModel::setStorageFilter(KisTagResourceModel::StorageFilter filter)
{
    d->storageFilter = filter;
    invalidateFilter();
}

bool KisTagResourceModel::tagResource(const KisTagSP tag, const int resourceId)
{
    bool r = d->sourceModel->tagResource(tag, resourceId);
    return r;
}

bool KisTagResourceModel::untagResource(const KisTagSP tag, const int resourceId)
{
    return d->sourceModel->untagResource(tag, resourceId);
}

bool KisTagResourceModel::isResourceTagged(const KisTagSP tag, const int resourceId)
{
    return d->sourceModel->isResourceTagged(tag, resourceId);
}

void KisTagResourceModel::setTagsFilter(const QVector<int> tagIds)
{
    d->tagIds = tagIds;
    invalidateFilter();
}

void KisTagResourceModel::setResourcesFilter(const QVector<int> resourceIds)
{
    d->resourceIds = resourceIds;
    invalidateFilter();
}

void KisTagResourceModel::setTagsFilter(const QVector<KisTagSP> tags)
{
    d->tagIds.clear();
    Q_FOREACH(const KisTagSP tag, tags) {
        if (tag && tag->valid() && tag->id() > -1) {
            d->tagIds << tag->id();
        }
    }
    invalidateFilter();
}

void KisTagResourceModel::setResourcesFilter(const QVector<KoResourceSP> resources)
{
    d->resourceIds.clear();
    Q_FOREACH(const KoResourceSP resource, resources) {
        if (resource->valid() && resource->resourceId() > -1) {
            d->resourceIds << resource->resourceId();
        }
    }
    invalidateFilter();
}

bool KisTagResourceModel::filterAcceptsRow(int source_row, const QModelIndex &source_parent) const
{
    QModelIndex idx = sourceModel()->index(source_row, 0, source_parent);
    if (!idx.isValid()) return false;

    int tagId = idx.data(Qt::UserRole + KisAllTagResourceModel::TagId).toInt();
    int resourceId = idx.data(Qt::UserRole + KisAllTagResourceModel::ResourceId).toInt();
    bool tagActive = idx.data(Qt::UserRole + KisAllTagResourceModel::TagActive).toBool();
    bool resourceActive = idx.data(Qt::UserRole + KisAllTagResourceModel::ResourceActive).toBool();
    bool resourceStorageActive = idx.data(Qt::UserRole + KisAllTagResourceModel::ResourceStorageActive).toBool();

    if (d->tagFilter == ShowAllTags && d->resourceFilter == ShowAllResources && d->storageFilter == ShowAllStorages) {
        return ((d->tagIds.contains(tagId) || d->tagIds.isEmpty()) &&
                (d->resourceIds.contains(resourceId) || d->resourceIds.isEmpty()));
    }

    if ((d->tagFilter == ShowActiveTags && !tagActive)
            || (d->tagFilter == ShowInactiveTags && tagActive)) {
        return false;
    }

    if ((d->resourceFilter == ShowActiveResources && !resourceActive)
            || (d->resourceFilter == ShowInactiveResources && resourceActive)) {
        return false;
    }

    if ((d->storageFilter == ShowActiveStorages && !resourceStorageActive)
            || (d->storageFilter == ShowInactiveStorages && resourceStorageActive)) {
        return false;
    }

    return ((d->tagIds.contains(tagId) || d->tagIds.isEmpty())
            && (d->resourceIds.contains(resourceId) || d->resourceIds.isEmpty()));
}

bool KisTagResourceModel::lessThan(const QModelIndex &source_left, const QModelIndex &source_right) const
{
    QString nameLeft = sourceModel()->data(source_left, Qt::UserRole + KisAllTagResourceModel::ResourceName).toString();
    QString nameRight = sourceModel()->data(source_right, Qt::UserRole + KisAllTagResourceModel::ResourceName).toString();
    return nameLeft.toLower() < nameRight.toLower();
}

void KisTagResourceModel::storageChanged(const QString &location)
{
    Q_UNUSED(location);
    invalidateFilter();
}

KoResourceSP KisTagResourceModel::resourceForIndex(QModelIndex index) const
{
    int id = data(index, Qt::UserRole + KisAllTagResourceModel::ResourceId).toInt();
    if (id < 1)  return nullptr;
    KoResourceSP res = KisResourceLocator::instance()->resourceForId(id);
    return res;
}

QModelIndex KisTagResourceModel::indexForResource(KoResourceSP resource) const
{
    if (!resource || !resource->valid() || resource->resourceId() < 0) return QModelIndex();

    for (int i = 0; i < rowCount(); ++i)  {
        QModelIndex idx = index(i, Qt::UserRole + KisAllTagResourceModel::ResourceId);
        Q_ASSERT(idx.isValid());
        if (idx.data(Qt::UserRole + KisAllTagResourceModel::ResourceId).toInt() == resource->resourceId()) {
            return idx;
        }
    }
    return QModelIndex();
}

QModelIndex KisTagResourceModel::indexForResourceId(int resourceId) const
{
    if (resourceId < 0) return QModelIndex();
    for (int i = 0; i < rowCount(); ++i)  {
        QModelIndex idx = index(i, Qt::UserRole + KisAllTagResourceModel::ResourceId);
        Q_ASSERT(idx.isValid());
        if (idx.data(Qt::UserRole + KisAllTagResourceModel::ResourceId).toInt() == resourceId) {
            return idx;
        }
    }
    return QModelIndex();
}

bool KisTagResourceModel::setResourceInactive(const QModelIndex &index)
{
    KisResourceModel resourceModel(d->resourceType);
    QModelIndex idx = resourceModel.indexForResource(resourceForIndex(index));
    return resourceModel.setResourceInactive(idx);
}

KoResourceSP KisTagResourceModel::importResourceFile(const QString &filename, const bool allowOverwrite, const QString &storageId)
{
    // Since we're importing the resource, there's no reason to add rows to the tags::resources table,
    // because the resource is untagged.
    KisResourceModel resourceModel(d->resourceType);
    return resourceModel.importResourceFile(filename, allowOverwrite, storageId);
}

KoResourceSP KisTagResourceModel::importResource(const QString &filename, QIODevice *device, const bool allowOverwrite, const QString &storageId)
{
    // Since we're importing the resource, there's no reason to add rows to the tags::resources table,
    // because the resource is untagged.
    KisResourceModel resourceModel(d->resourceType);
    return resourceModel.importResource(filename, device, allowOverwrite, storageId);
}

bool KisTagResourceModel::importWillOverwriteResource(const QString &fileName, const QString &storageLocation) const
{
    KisResourceModel resourceModel(d->resourceType);
    return resourceModel.importWillOverwriteResource(fileName, storageLocation);
}

bool KisTagResourceModel::exportResource(KoResourceSP resource, QIODevice *device)
{
    KisResourceModel resourceModel(d->resourceType);
    return resourceModel.exportResource(resource, device);
}

bool KisTagResourceModel::addResource(KoResourceSP resource, const QString &storageId)
{
    // Since we're importing the resource, there's no reason to add rows to the tags::resources table,
    // because the resource is untagged.
    KisResourceModel resourceModel(d->resourceType);
    return resourceModel.addResource(resource, storageId);
}

bool KisTagResourceModel::updateResource(KoResourceSP resource)
{
    KisResourceModel resourceModel(d->resourceType);
    bool r = resourceModel.updateResource(resource);
    if (r) {
        QModelIndex index = indexForResource(resource);
        if (index.isValid()) {
            emit dataChanged(index, index, {Qt::EditRole});
        }
    }
    return r;
}

bool KisTagResourceModel::reloadResource(KoResourceSP resource)
{
    KisResourceModel resourceModel(d->resourceType);
    bool r = resourceModel.reloadResource(resource);
    if (r) {
        QModelIndex index = indexForResource(resource);
        if (index.isValid()) {
            emit dataChanged(index, index, {Qt::EditRole});
        }
    }
    return r;
}

bool KisTagResourceModel::renameResource(KoResourceSP resource, const QString &name)
{
    KisResourceModel resourceModel(d->resourceType);
    bool r = resourceModel.renameResource(resource, name);
    if (r) {
        QModelIndex index = indexForResource(resource);
        if (index.isValid()) {
            emit dataChanged(index, index, {Qt::EditRole});
        }
    }
    return r;
}

bool KisTagResourceModel::setResourceMetaData(KoResourceSP resource, QMap<QString, QVariant> metadata)
{
    KisResourceModel resourceModel(d->resourceType);
    return resourceModel.setResourceMetaData(resource, metadata);
}
