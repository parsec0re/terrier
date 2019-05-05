#include "storage/index/index_manager.h"
#include <memory>
#include <string>
#include <vector>

namespace terrier::storage::index {
Index *IndexManager::GetEmptyIndex(transaction::TransactionContext *txn, catalog::db_oid_t db_oid,
                                   catalog::table_oid_t table_oid, catalog::index_oid_t index_oid, bool unique_index,
                                   const std::vector<std::string> &key_attrs, catalog::Catalog *catalog) {
  // Setup the oid and constraint type for the index
  IndexFactory index_factory;
  ConstraintType constraint = (unique_index) ? ConstraintType::UNIQUE : ConstraintType::DEFAULT;
  index_factory.SetOid(index_oid);
  index_factory.SetConstraintType(constraint);

  // Setup the key schema for the index
  IndexKeySchema key_schema;
  for (const auto &key_name : key_attrs) {
    // Get the catalog entry for each attribute
    auto entry =
        catalog->GetDatabaseHandle().GetAttributeHandle(txn, db_oid).GetAttributeEntry(txn, table_oid, key_name);

    // If there is no corresponding entry in the catalog, return nullptr.
    if (entry == nullptr) return nullptr;

    // Fill in the key schema with information from catalog entry
    type::TypeId type_id = static_cast<type::TypeId>(entry->GetIntegerColumn("atttypid"));
    if (type::TypeUtil::GetTypeSize(type_id) == VARLEN_COLUMN)
      key_schema.emplace_back(catalog::indexkeycol_oid_t(entry->GetIntegerColumn("oid")), type_id,
                              entry->ColumnIsNull(key_name));
    else
      key_schema.emplace_back(catalog::indexkeycol_oid_t(entry->GetIntegerColumn("oid")), type_id,
                              entry->ColumnIsNull(key_name), entry->GetIntegerColumn("attlen"));
  }
  index_factory.SetKeySchema(key_schema);

  // Build an empty index
  return index_factory.Build();
}

void IndexManager::CreateConcurrently(catalog::db_oid_t db_oid, catalog::namespace_oid_t ns_oid,
                                      catalog::table_oid_t table_oid, parser::IndexType index_type, bool unique_index,
                                      const std::string &index_name, const std::vector<std::string> &index_attrs,
                                      const std::vector<std::string> &key_attrs,
                                      transaction::TransactionManager *txn_mgr, catalog::Catalog *catalog) {
  // First transaction to insert an entry for the index in the catalog
  transaction::TransactionContext *txn1 = txn_mgr->BeginTransaction();
  catalog::SqlTableHelper *sql_table_helper = catalog->GetUserTable(txn1, db_oid, ns_oid, table_oid);
  // user table does not exist
  if (sql_table_helper == nullptr) {
    txn_mgr->Abort(txn1);
    return;
  }
  std::shared_ptr<SqlTable> sql_table = sql_table_helper->GetSqlTable();
  catalog::IndexHandle index_handle = catalog->GetDatabaseHandle().GetIndexHandle(txn1, db_oid);

  // placeholder args
  catalog::index_oid_t index_oid(catalog->GetNextOid());
  auto indnatts = static_cast<int32_t>(index_attrs.size());
  auto indnkeyatts = static_cast<int32_t>(key_attrs.size());
  bool indisunique = unique_index;
  bool indisprimary = false;
  bool indisvalid = false;
  bool indisready = true;
  bool indislive = false;

  // Intialize the index
  Index *index = GetEmptyIndex(txn1, db_oid, table_oid, index_oid, indisunique, key_attrs, catalog);
  // Initializing the index fails
  if (index == nullptr) {
    txn_mgr->Abort(txn1);
    return;
  }

  // Add IndexEntry
  index_handle.AddEntry(txn1, index, index_oid, table_oid, indnatts, indnkeyatts, indisunique, indisprimary, indisvalid,
                        indisready, indislive);

  // initialize the building flag to false
  auto index_id = make_index_id(db_oid, ns_oid, index_oid);
  SetIndexBuildingFlag(index_id, false);

  // Commit first transaction
  transaction::timestamp_t commit_time = txn_mgr->Commit(txn1, nullptr, nullptr);

  // Wait for all transactions older than the timestamp of previous transaction commit
  // TODO(jiaqizuo): use more efficient way to wait for all previous transactions to complete.
  while (txn_mgr->OldestTransactionStartTime() < commit_time) {
  }

  // Start the second transaction to insert all keys into the index.
  // The second transaction set the building flag to true in the critical section.
  transaction::TransactionContext *build_txn =
      txn_mgr->BeginTransactionWithAction([&]() { SetIndexBuildingFlag(index_id, true); });
  // Change "indisready" to false and "indisvalid" to the result of populating the index in the catalog entry
  index_handle.SetEntryColumn(build_txn, index_oid, "indisready", type::TransientValueFactory::GetBoolean(false));
  index_handle.SetEntryColumn(
      build_txn, index_oid, "indisvalid",
      type::TransientValueFactory::GetBoolean(PopulateIndex(build_txn, *sql_table, index, unique_index)));
  // Commit the transaction
  txn_mgr->Commit(build_txn, nullptr, nullptr);
}

void IndexManager::Drop(catalog::db_oid_t db_oid, catalog::namespace_oid_t ns_oid, catalog::table_oid_t table_oid,
                        catalog::index_oid_t index_oid, const std::string &index_name,
                        transaction::TransactionManager *txn_mgr, catalog::Catalog *catalog) {
  // Start the transaction to delete the entry in the catalog
  transaction::TransactionContext *txn = txn_mgr->BeginTransaction();
  catalog::SqlTableHelper *sql_table_helper = catalog->GetUserTable(txn, db_oid, ns_oid, table_oid);
  // user table does not exist
  if (sql_table_helper == nullptr) {
    txn_mgr->Abort(txn);
    return;
  }
  std::shared_ptr<SqlTable> sql_table = sql_table_helper->GetSqlTable();
  catalog::IndexHandle index_handle = catalog->GetDatabaseHandle().GetIndexHandle(txn, db_oid);
  // Get the index entry to be deleted
  std::shared_ptr<catalog::IndexEntry> index_entry = index_handle.GetIndexEntry(txn, index_oid);
  // Delete the index entry from the index_handle
  index_handle.DeleteEntry(txn, index_entry);
  // Commit the transaction
  transaction::timestamp_t commit_time = txn_mgr->Commit(txn, nullptr, nullptr);

  // Wait for all transactions older than the timestamp of previous transaction commit
  // TODO(xueyuanz): use more efficient way to wait for all previous transactions to complete.
  while (txn_mgr->OldestTransactionStartTime() < commit_time) {
  }
  // Now we can safely destruct the index_entry
  Index *index = reinterpret_cast<Index *>(index_entry->GetBigIntColumn("indexptr"));
  delete index;
}

bool IndexManager::PopulateIndex(transaction::TransactionContext *txn, const SqlTable &sql_table, Index *index,
                                 bool unique_index) {
  // Create the projected row for the index
  const IndexMetadata &metadata = index->GetIndexMetadata();
  const IndexKeySchema &index_key_schema = metadata.GetKeySchema();
  const auto &index_pr_init = metadata.GetProjectedRowInitializer();
  auto *index_pr_buf = common::AllocationUtil::AllocateAligned(index_pr_init.ProjectedRowSize());
  ProjectedRow *indkey_pr = index_pr_init.InitializeRow(index_pr_buf);

  // Create the projected row for the sql table
  std::vector<catalog::col_oid_t> col_oids;
  for (const auto &it : index_key_schema) {
    col_oids.emplace_back(catalog::col_oid_t(!it.GetOid()));
  }
  auto table_pr_init = sql_table.InitializerForProjectedRow(col_oids).first;
  auto *table_pr_buf = common::AllocationUtil::AllocateAligned(table_pr_init.ProjectedRowSize());
  ProjectedRow *select_pr = table_pr_init.InitializeRow(table_pr_buf);

  // Record the col_id of each column
  const col_id_t *columns = select_pr->ColumnIds();
  uint16_t num_cols = select_pr->NumColumns();
  std::vector<col_id_t> sql_table_cols(columns, columns + num_cols);

  bool success = true;
  for (const auto &it : sql_table) {
    if (sql_table.Select(txn, it, select_pr)) {
      for (uint16_t i = 0; i < select_pr->NumColumns(); ++i) {
        select_pr->ColumnIds()[i] = indkey_pr->ColumnIds()[i];
      }
      // Check whether the insertion successes
      if (unique_index) {
        if (!index->InsertUnique(txn, *select_pr, it)) {
          success = false;
          break;
        }
      } else {
        if (!index->Insert(txn, *select_pr, it)) {
          success = false;
          break;
        }
      }

      for (uint16_t i = 0; i < select_pr->NumColumns(); ++i) {
        select_pr->ColumnIds()[i] = sql_table_cols[i];
      }
    }
  }
  delete[] index_pr_buf;
  delete[] table_pr_buf;
  return success;
}
}  // namespace terrier::storage::index