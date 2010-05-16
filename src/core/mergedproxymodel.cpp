/* This file is part of Clementine.

   Clementine is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   Clementine is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Clementine.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "mergedproxymodel.h"

#include <QStringList>
#include <QtDebug>

#include <limits>

std::size_t hash_value(const QModelIndex& index) {
  return qHash(index);
}

MergedProxyModel::MergedProxyModel(QObject* parent)
  : QAbstractProxyModel(parent),
    resetting_model_(NULL)
{
}

MergedProxyModel::~MergedProxyModel() {
  DeleteAllMappings();
}

void MergedProxyModel::DeleteAllMappings() {
  MappingContainer::index<tag_by_pointer>::type::iterator begin =
      mappings_.get<tag_by_pointer>().begin();
  MappingContainer::index<tag_by_pointer>::type::iterator end =
      mappings_.get<tag_by_pointer>().end();
  qDeleteAll(begin, end);
}

void MergedProxyModel::AddSubModel(const QModelIndex& source_parent,
                                   QAbstractItemModel* submodel) {
  merge_points_.insert(submodel, source_parent);

  connect(submodel, SIGNAL(modelReset()), this, SLOT(SubModelReset()));
  connect(submodel, SIGNAL(rowsAboutToBeInserted(QModelIndex,int,int)),
          this, SLOT(RowsAboutToBeInserted(QModelIndex,int,int)));
  connect(submodel, SIGNAL(rowsAboutToBeRemoved(QModelIndex,int,int)),
          this, SLOT(RowsAboutToBeRemoved(QModelIndex,int,int)));
  connect(submodel, SIGNAL(rowsInserted(QModelIndex,int,int)),
          this, SLOT(RowsInserted(QModelIndex,int,int)));
  connect(submodel, SIGNAL(rowsRemoved(QModelIndex,int,int)),
          this, SLOT(RowsRemoved(QModelIndex,int,int)));
}

void MergedProxyModel::setSourceModel(QAbstractItemModel* source_model) {
  if (sourceModel()) {
    disconnect(sourceModel(), SIGNAL(modelReset()), this, SLOT(SourceModelReset()));
    disconnect(sourceModel(), SIGNAL(rowsAboutToBeInserted(QModelIndex,int,int)),
               this, SLOT(RowsAboutToBeInserted(QModelIndex,int,int)));
    disconnect(sourceModel(), SIGNAL(rowsAboutToBeRemoved(QModelIndex,int,int)),
               this, SLOT(RowsAboutToBeRemoved(QModelIndex,int,int)));
    disconnect(sourceModel(), SIGNAL(rowsInserted(QModelIndex,int,int)),
               this, SLOT(RowsInserted(QModelIndex,int,int)));
    disconnect(sourceModel(), SIGNAL(rowsRemoved(QModelIndex,int,int)),
               this, SLOT(RowsRemoved(QModelIndex,int,int)));
  }

  QAbstractProxyModel::setSourceModel(source_model);

  connect(sourceModel(), SIGNAL(modelReset()), this, SLOT(SourceModelReset()));
  connect(sourceModel(), SIGNAL(rowsAboutToBeInserted(QModelIndex,int,int)),
          this, SLOT(RowsAboutToBeInserted(QModelIndex,int,int)));
  connect(sourceModel(), SIGNAL(rowsAboutToBeRemoved(QModelIndex,int,int)),
          this, SLOT(RowsAboutToBeRemoved(QModelIndex,int,int)));
  connect(sourceModel(), SIGNAL(rowsInserted(QModelIndex,int,int)),
          this, SLOT(RowsInserted(QModelIndex,int,int)));
  connect(sourceModel(), SIGNAL(rowsRemoved(QModelIndex,int,int)),
          this, SLOT(RowsRemoved(QModelIndex,int,int)));
}

void MergedProxyModel::SourceModelReset() {
  // Delete all mappings
  DeleteAllMappings();

  // Clear the containers
  mappings_.clear();
  merge_points_.clear();

  // Reset the proxy
  reset();
}

void MergedProxyModel::SubModelReset() {
  QAbstractItemModel* submodel = static_cast<QAbstractItemModel*>(sender());

  // TODO: When we require Qt 4.6, use beginResetModel() and endResetModel()
  // in LibraryModel and catch those here - that will let us do away with this
  // std::numeric_limits<int>::max() hack.

  // Remove all the children of the item that got deleted
  QModelIndex source_parent = merge_points_.value(submodel);
  QModelIndex proxy_parent = mapFromSource(source_parent);

  // We can't know how many children it had, since it's already disappeared...
  resetting_model_ = submodel;
  beginRemoveRows(proxy_parent, 0, std::numeric_limits<int>::max() - 1);
  endRemoveRows();
  resetting_model_ = NULL;

  // Delete all the mappings that reference the submodel
  MappingContainer::index<tag_by_pointer>::type::iterator it =
      mappings_.get<tag_by_pointer>().begin();
  MappingContainer::index<tag_by_pointer>::type::iterator end =
      mappings_.get<tag_by_pointer>().end();
  while (it != end) {
    if ((*it)->source_index.model() == submodel) {
      delete *it;
      it = mappings_.get<tag_by_pointer>().erase(it);
    } else {
      ++it;
    }
  }

  // "Insert" items from the newly reset submodel
  int count = submodel->rowCount();
  if (count) {
    beginInsertRows(proxy_parent, 0, count-1);
    endInsertRows();
  }

  emit SubModelReset(proxy_parent, submodel);
}

QModelIndex MergedProxyModel::GetActualSourceParent(const QModelIndex& source_parent,
                                                    QAbstractItemModel* model) const {
  if (!source_parent.isValid() && model != sourceModel())
    return merge_points_.value(model);
  return source_parent;
}

void MergedProxyModel::RowsAboutToBeInserted(const QModelIndex& source_parent,
                                             int start, int end) {
  beginInsertRows(mapFromSource(GetActualSourceParent(
      source_parent, static_cast<QAbstractItemModel*>(sender()))),
      start, end);
}

void MergedProxyModel::RowsInserted(const QModelIndex&, int, int) {
  endInsertRows();
}

void MergedProxyModel::RowsAboutToBeRemoved(const QModelIndex& source_parent,
                                            int start, int end) {
  beginRemoveRows(mapFromSource(GetActualSourceParent(
      source_parent, static_cast<QAbstractItemModel*>(sender()))),
      start, end);
}

void MergedProxyModel::RowsRemoved(const QModelIndex&, int, int) {
  endRemoveRows();
}

QModelIndex MergedProxyModel::mapToSource(const QModelIndex& proxy_index) const {
  if (!proxy_index.isValid())
    return QModelIndex();

  Mapping* mapping = static_cast<Mapping*>(proxy_index.internalPointer());
  if (mappings_.get<tag_by_pointer>().find(mapping) ==
      mappings_.get<tag_by_pointer>().end())
    return QModelIndex();
  if (mapping->source_index.model() == resetting_model_)
    return QModelIndex();

  return mapping->source_index;
}

QModelIndex MergedProxyModel::mapFromSource(const QModelIndex& source_index) const {
  if (!source_index.isValid())
    return QModelIndex();
  if (source_index.model() == resetting_model_)
    return QModelIndex();

  // Add a mapping if we don't have one already
  MappingContainer::index<tag_by_source>::type::iterator it =
      mappings_.get<tag_by_source>().find(source_index);
  Mapping* mapping;
  if (it != mappings_.get<tag_by_source>().end()) {
    mapping = *it;
  } else {
    mapping = new Mapping(source_index);
    const_cast<MergedProxyModel*>(this)->mappings_.insert(mapping);
  }

  return createIndex(source_index.row(), source_index.column(), mapping);
}

QModelIndex MergedProxyModel::index(int row, int column, const QModelIndex &parent) const {
  QModelIndex source_index;

  if (!parent.isValid()) {
    source_index = sourceModel()->index(row, column, QModelIndex());
  } else {
    QModelIndex source_parent = mapToSource(parent);
    const QAbstractItemModel* child_model = merge_points_.key(source_parent);

    if (child_model)
      source_index = child_model->index(row, column, QModelIndex());
    else
      source_index = source_parent.model()->index(row, column, source_parent);
  }

  return mapFromSource(source_index);
}

QModelIndex MergedProxyModel::parent(const QModelIndex &child) const {
  QModelIndex source_child = mapToSource(child);
  if (source_child.model() == sourceModel())
    return mapFromSource(source_child.parent());

  if (!source_child.parent().isValid())
    return mapFromSource(merge_points_.value(GetModel(source_child)));
  return mapFromSource(source_child.parent());
}

int MergedProxyModel::rowCount(const QModelIndex &parent) const {
  if (!parent.isValid())
    return sourceModel()->rowCount(QModelIndex());

  QModelIndex source_parent = mapToSource(parent);
  const QAbstractItemModel* child_model = merge_points_.key(source_parent);
  if (child_model) {
    // Query the source model but disregard what it says, so it gets a chance
    // to lazy load
    source_parent.model()->rowCount(source_parent);

    return child_model->rowCount(QModelIndex());
  }

  return source_parent.model()->rowCount(source_parent);
}

int MergedProxyModel::columnCount(const QModelIndex &parent) const {
  if (!parent.isValid())
    return sourceModel()->columnCount(QModelIndex());

  QModelIndex source_parent = mapToSource(parent);
  const QAbstractItemModel* child_model = merge_points_.key(source_parent);
  if (child_model)
    return child_model->columnCount(QModelIndex());
  return source_parent.model()->columnCount(source_parent);
}

bool MergedProxyModel::hasChildren(const QModelIndex &parent) const {
  if (!parent.isValid())
    return sourceModel()->hasChildren(QModelIndex());

  QModelIndex source_parent = mapToSource(parent);
  const QAbstractItemModel* child_model = merge_points_.key(source_parent);

  if (child_model)
    return child_model->hasChildren(QModelIndex()) ||
           source_parent.model()->hasChildren(source_parent);
  return source_parent.model()->hasChildren(source_parent);
}

QVariant MergedProxyModel::data(const QModelIndex &proxyIndex, int role) const {
  QModelIndex source_index = mapToSource(proxyIndex);
  return source_index.model()->data(source_index, role);
}

QMap<int, QVariant> MergedProxyModel::itemData(const QModelIndex& proxy_index) const {
  QModelIndex source_index = mapToSource(proxy_index);

  if (!source_index.isValid())
    return sourceModel()->itemData(QModelIndex());
  return source_index.model()->itemData(source_index);
}

Qt::ItemFlags MergedProxyModel::flags(const QModelIndex &index) const {
  QModelIndex source_index = mapToSource(index);

  if (!source_index.isValid())
    return sourceModel()->flags(QModelIndex());
  return source_index.model()->flags(source_index);
}

bool MergedProxyModel::setData(const QModelIndex &index, const QVariant &value,
                               int role) {
  QModelIndex source_index = mapToSource(index);

  if (!source_index.isValid())
    return sourceModel()->setData(index, value, role);
  return GetModel(index)->setData(index, value, role);
}

QStringList MergedProxyModel::mimeTypes() const {
  QStringList ret;
  ret << sourceModel()->mimeTypes();

  foreach (const QAbstractItemModel* model, merge_points_.keys()) {
    ret << model->mimeTypes();
  }

  return ret;
}

QMimeData* MergedProxyModel::mimeData(const QModelIndexList &indexes) const {
  if (indexes.isEmpty())
    return 0;

  // Only ask the first index's model
  const QAbstractItemModel* model = mapToSource(indexes[0]).model();

  // Only ask about the indexes that are actually in that model
  QModelIndexList indexes_in_model;

  foreach (const QModelIndex& proxy_index, indexes) {
    QModelIndex source_index = mapToSource(proxy_index);
    if (source_index.model() != model)
      continue;
    indexes_in_model << source_index;
  }

  return model->mimeData(indexes_in_model);
}

QModelIndex MergedProxyModel::FindSourceParent(const QModelIndex& proxy_index) const {
  if (!proxy_index.isValid())
    return QModelIndex();

  QModelIndex source_index = mapToSource(proxy_index);
  if (source_index.model() == sourceModel())
    return source_index;
  return merge_points_.value(GetModel(source_index));
}

bool MergedProxyModel::canFetchMore(const QModelIndex &parent) const {
  QModelIndex source_index = mapToSource(parent);

  if (!source_index.isValid())
    return sourceModel()->canFetchMore(QModelIndex());
  return source_index.model()->canFetchMore(source_index);
}

void MergedProxyModel::fetchMore(const QModelIndex& parent) {
  QModelIndex source_index = mapToSource(parent);

  if (!source_index.isValid())
    sourceModel()->fetchMore(QModelIndex());
  else
    GetModel(source_index)->fetchMore(source_index);
}

QAbstractItemModel* MergedProxyModel::GetModel(const QModelIndex& source_index) const {
  // This is essentially const_cast<QAbstractItemModel*>(source_index.model()),
  // but without the const_cast
  const QAbstractItemModel* const_model = source_index.model();
  if (const_model == sourceModel())
    return sourceModel();
  foreach (QAbstractItemModel* submodel, merge_points_.keys()) {
    if (submodel == const_model)
      return submodel;
  }
  return NULL;
}
