#include "execution/sql/index_iterator.h"
#include "execution/sql/value.h"

namespace terrier::execution::sql {

IndexIterator::IndexIterator(uint32_t table_oid, uint32_t index_oid, exec::ExecutionContext *exec_ctx)
    : exec_ctx_(exec_ctx),
      index_(exec_ctx_->GetAccessor()->GetIndex(catalog::index_oid_t(index_oid))),
      table_(exec_ctx_->GetAccessor()->GetTable(catalog::table_oid_t(table_oid))),
      schema_(exec_ctx_->GetAccessor()->GetSchema(catalog::table_oid_t(table_oid))) {}

void IndexIterator::Init() {
  // Initialize projected rows for the index and the table
  TERRIER_ASSERT(!col_oids_.empty(), "There must be at least one col oid!");
  auto pri_map = table_->InitializerForProjectedRow(col_oids_);
  // Table's PR
  auto &table_pri = pri_map.first;
  table_buffer_ = common::AllocationUtil::AllocateAligned(table_pri.ProjectedRowSize());
  table_pr_ = table_pri.InitializeRow(table_buffer_);

  // Index's PR
  auto &index_pri = index_->GetProjectedRowInitializer();
  index_buffer_ = common::AllocationUtil::AllocateAligned(index_pri.ProjectedRowSize());
  index_pr_ = index_pri.InitializeRow(index_buffer_);
}

IndexIterator::~IndexIterator() {
  // Free allocated buffers
  delete[] index_buffer_;
  delete[] table_buffer_;
}
}  // namespace terrier::execution::sql
