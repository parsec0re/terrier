#include "catalog/namespace_handle.h"
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include "catalog/catalog.h"
#include "catalog/schema.h"
#include "loggers/catalog_logger.h"
#include "storage/block_layout.h"
#include "storage/sql_table.h"
#include "storage/storage_defs.h"
#include "type/type_id.h"
namespace terrier::catalog {

std::shared_ptr<NamespaceHandle::NamespaceEntry> NamespaceHandle::GetNamespaceEntry(
    transaction::TransactionContext *txn, namespace_oid_t oid) {
  storage::ProjectedRow *p_row = pg_namespace_hrw_->FindRow(txn, 0, !oid);
  if (p_row == nullptr) {
    return nullptr;
  }

  return std::make_shared<NamespaceEntry>(oid, p_row, *pg_namespace_hrw_->GetPRMap(), pg_namespace_hrw_);
}

std::shared_ptr<NamespaceHandle::NamespaceEntry> NamespaceHandle::GetNamespaceEntry(
    transaction::TransactionContext *txn, const std::string &name) {
  storage::ProjectedRow *p_row = pg_namespace_hrw_->FindRow(txn, 1, name.c_str());
  if (p_row == nullptr) {
    return nullptr;
  }

  // now recover the oid
  namespace_oid_t oid(pg_namespace_hrw_->GetIntColInRow(0, p_row));
  return std::make_shared<NamespaceEntry>(oid, p_row, *pg_namespace_hrw_->GetPRMap(), pg_namespace_hrw_);
}

TableHandle NamespaceHandle::GetTableHandle(const std::string &nsp_name) {
  CATALOG_LOG_TRACE("Getting the table handle ...");
  std::string pg_class("pg_class");
  std::string pg_namespace("pg_namespace");
  std::string pg_tablespace("pg_tablespace");
  return TableHandle(nsp_name, catalog_->GetDatabaseCatalog(db_oid_, pg_class),
                     catalog_->GetDatabaseCatalog(db_oid_, pg_namespace),
                     catalog_->GetDatabaseCatalog(db_oid_, pg_tablespace));
}

}  // namespace terrier::catalog
