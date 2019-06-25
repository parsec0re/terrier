#include "execution/sema/sema.h"

#include "execution/ast/ast_node_factory.h"
#include "execution/ast/context.h"
#include "execution/ast/type.h"

namespace tpl::sema {

namespace {

bool IsPointerToSpecificBuiltin(ast::Type *type, ast::BuiltinType::Kind kind) {
  if (auto *pointee_type = type->GetPointeeType()) {
    return pointee_type->IsSpecificBuiltin(kind);
  }
  return false;
}

bool IsPointerToSQLValue(ast::Type *type) {
  if (auto *pointee_type = type->GetPointeeType()) {
    return pointee_type->IsSqlValueType();
  }
  return false;
}

bool IsPointerToAggregatorValue(ast::Type *type) {
  if (auto *pointee_type = type->GetPointeeType()) {
    return pointee_type->IsSqlAggregatorType();
  }
  return false;
}

template <typename... ArgTypes>
bool AreAllFunctions(const ArgTypes... type) {
  return (true && ... && type->IsFunctionType());
}

}  // namespace

void Sema::CheckBuiltinMapCall(UNUSED ast::CallExpr *call) {}

void Sema::CheckBuiltinSqlConversionCall(ast::CallExpr *call,
                                         ast::Builtin builtin) {
  if (!CheckArgCount(call, 1)) {
    return;
  }
  auto input_type = call->arguments()[0]->type();
  switch (builtin) {
    case ast::Builtin::BoolToSql: {
      if (!input_type->IsSpecificBuiltin(ast::BuiltinType::Bool)) {
        error_reporter()->Report(
            call->position(), ErrorMessages::kInvalidSqlCastToBool, input_type);
        return;
      }
      call->set_type(GetBuiltinType(ast::BuiltinType::Boolean));
      break;
    }
    case ast::Builtin::IntToSql: {
      if (!input_type->IsIntegerType()) {
        error_reporter()->Report(
            call->position(), ErrorMessages::kInvalidSqlCastToBool, input_type);
        return;
      }
      call->set_type(GetBuiltinType(ast::BuiltinType::Integer));
      break;
    }
    case ast::Builtin::FloatToSql: {
      if (!input_type->IsFloatType()) {
        error_reporter()->Report(
            call->position(), ErrorMessages::kInvalidSqlCastToBool, input_type);
        return;
      }
      call->set_type(GetBuiltinType(ast::BuiltinType::Real));
      break;
    }
    case ast::Builtin::SqlToBool: {
      if (!input_type->IsSpecificBuiltin(ast::BuiltinType::Boolean)) {
        error_reporter()->Report(
            call->position(), ErrorMessages::kInvalidSqlCastToBool, input_type);
        return;
      }
      call->set_type(GetBuiltinType(ast::BuiltinType::Bool));
      break;
    }
    default: { UNREACHABLE("Impossible SQL conversion call"); }
  }
}

void Sema::CheckBuiltinFilterCall(ast::CallExpr *call) {
  if (!CheckArgCount(call, 3)) {
    return;
  }

  const auto &args = call->arguments();

  // The first call argument must be a pointer to a ProjectedColumnsIterator
  const auto pci_kind = ast::BuiltinType::ProjectedColumnsIterator;
  if (!IsPointerToSpecificBuiltin(args[0]->type(), pci_kind)) {
    ReportIncorrectCallArg(call, 0, GetBuiltinType(pci_kind)->PointerTo());
    return;
  }

  // The second call argument must an integer for the column index
  auto int32_kind = ast::BuiltinType::Int32;
  if (!args[1]->type()->IsSpecificBuiltin(int32_kind)) {
    ReportIncorrectCallArg(call, 1, GetBuiltinType(int32_kind));
    return;
  }

  // Set return type
  call->set_type(GetBuiltinType(ast::BuiltinType::Int32));
}

void Sema::CheckBuiltinAggHashTableCall(ast::CallExpr *call,
                                        ast::Builtin builtin) {
  if (!CheckArgCountAtLeast(call, 1)) {
    return;
  }

  const auto &args = call->arguments();

  const auto agg_ht_kind = ast::BuiltinType::AggregationHashTable;
  if (!IsPointerToSpecificBuiltin(args[0]->type(), agg_ht_kind)) {
    ReportIncorrectCallArg(call, 0, GetBuiltinType(agg_ht_kind)->PointerTo());
    return;
  }

  switch (builtin) {
    case ast::Builtin::AggHashTableInit: {
      if (!CheckArgCount(call, 3)) {
        return;
      }
      // Second argument is a memory pool pointer
      const auto mem_pool_kind = ast::BuiltinType::MemoryPool;
      if (!IsPointerToSpecificBuiltin(args[1]->type(), mem_pool_kind)) {
        ReportIncorrectCallArg(call, 1,
                               GetBuiltinType(mem_pool_kind)->PointerTo());
        return;
      }
      // Third argument is the payload size, a 32-bit value
      const auto uint_kind = ast::BuiltinType::Uint32;
      if (!args[2]->type()->IsSpecificBuiltin(uint_kind)) {
        ReportIncorrectCallArg(call, 2, GetBuiltinType(uint_kind));
        return;
      }
      // Nil return
      call->set_type(GetBuiltinType(ast::BuiltinType::Nil));
      break;
    }
    case ast::Builtin::AggHashTableInsert: {
      if (!CheckArgCount(call, 2)) {
        return;
      }
      // Second argument is the hash value
      const auto hash_val_kind = ast::BuiltinType::Uint64;
      if (!args[1]->type()->IsSpecificBuiltin(hash_val_kind)) {
        ReportIncorrectCallArg(call, 1, GetBuiltinType(hash_val_kind));
        return;
      }
      // Return a byte pointer
      call->set_type(GetBuiltinType(ast::BuiltinType::Uint8)->PointerTo());
      break;
    }
    case ast::Builtin::AggHashTableLookup: {
      if (!CheckArgCount(call, 4)) {
        return;
      }
      // Second argument is the hash value
      const auto hash_val_kind = ast::BuiltinType::Uint64;
      if (!args[1]->type()->IsSpecificBuiltin(hash_val_kind)) {
        ReportIncorrectCallArg(call, 1, GetBuiltinType(hash_val_kind));
        return;
      }
      // Third argument is the key equality function
      if (!args[2]->type()->IsFunctionType()) {
        ReportIncorrectCallArg(call, 2, GetBuiltinType(hash_val_kind));
        return;
      }
      // Fourth argument is the probe tuple, but any pointer will do
      call->set_type(GetBuiltinType(ast::BuiltinType::Uint8)->PointerTo());
      break;
    }
    case ast::Builtin::AggHashTableProcessBatch: {
      if (!CheckArgCount(call, 7)) {
        return;
      }
      // Second argument is the PCIs
      const auto pci_kind = ast::BuiltinType::Uint64;
      if (!args[1]->type()->IsPointerType() ||
          IsPointerToSpecificBuiltin(args[1]->type()->GetPointeeType(),
                                     pci_kind)) {
        ReportIncorrectCallArg(call, 1, GetBuiltinType(pci_kind)->PointerTo());
        return;
      }
      // Third, fourth, fifth, and sixth are all functions
      if (!AreAllFunctions(args[2]->type(), args[3]->type(), args[4]->type(),
                           args[5]->type())) {
        ReportIncorrectCallArg(call, 2, "function");
        return;
      }
      // Last arg must be a boolean
      if (!args[6]->type()->IsBoolType()) {
        ReportIncorrectCallArg(call, 6, GetBuiltinType(ast::BuiltinType::Bool));
        return;
      }
      call->set_type(GetBuiltinType(ast::BuiltinType::Nil));
      break;
    }
    case ast::Builtin::AggHashTableMovePartitions: {
      if (!CheckArgCount(call, 4)) {
        return;
      }
      // Second argument is the thread state container pointer
      const auto tls_kind = ast::BuiltinType::ThreadStateContainer;
      if (!IsPointerToSpecificBuiltin(args[1]->type(), tls_kind)) {
        ReportIncorrectCallArg(call, 1, GetBuiltinType(tls_kind)->PointerTo());
        return;
      }
      // Third argument is the offset of the hash table in thread local state
      const auto uint32_kind = ast::BuiltinType::Uint32;
      if (!args[2]->type()->IsSpecificBuiltin(uint32_kind)) {
        ReportIncorrectCallArg(call, 2, GetBuiltinType(uint32_kind));
        return;
      }
      // Fourth argument is the merging function
      if (!args[3]->type()->IsFunctionType()) {
        ReportIncorrectCallArg(call, 3, GetBuiltinType(uint32_kind));
        return;
      }

      call->set_type(GetBuiltinType(ast::BuiltinType::Nil));
      break;
    }
    case ast::Builtin::AggHashTableParallelPartitionedScan: {
      if (!CheckArgCount(call, 4)) {
        return;
      }
      // Second argument is an opaque context pointer
      if (!args[1]->type()->IsPointerType()) {
        ReportIncorrectCallArg(call, 1, GetBuiltinType(agg_ht_kind));
        return;
      }
      // Third argument is the thread state container pointer
      const auto tls_kind = ast::BuiltinType::ThreadStateContainer;
      if (!IsPointerToSpecificBuiltin(args[2]->type(), tls_kind)) {
        ReportIncorrectCallArg(call, 2, GetBuiltinType(tls_kind)->PointerTo());
        return;
      }
      // Fourth argument is the scanning function
      if (!args[3]->type()->IsFunctionType()) {
        ReportIncorrectCallArg(call, 3, GetBuiltinType(tls_kind));
        return;
      }

      call->set_type(GetBuiltinType(ast::BuiltinType::Nil));
      break;
    }
    case ast::Builtin::AggHashTableFree: {
      call->set_type(GetBuiltinType(ast::BuiltinType::Nil));
      break;
    }
    default: { UNREACHABLE("Impossible aggregation hash table call"); }
  }
}

void Sema::CheckBuiltinAggHashTableIterCall(ast::CallExpr *call,
                                            ast::Builtin builtin) {
  if (!CheckArgCountAtLeast(call, 1)) {
    return;
  }

  const auto &args = call->arguments();

  const auto agg_ht_iter_kind = ast::BuiltinType::AggregationHashTableIterator;
  if (!IsPointerToSpecificBuiltin(args[0]->type(), agg_ht_iter_kind)) {
    ReportIncorrectCallArg(call, 0,
                           GetBuiltinType(agg_ht_iter_kind)->PointerTo());
    return;
  }

  switch (builtin) {
    case ast::Builtin::AggHashTableIterInit: {
      if (!CheckArgCount(call, 2)) {
        return;
      }
      const auto agg_ht_kind = ast::BuiltinType::AggregationHashTable;
      if (!IsPointerToSpecificBuiltin(args[1]->type(), agg_ht_kind)) {
        ReportIncorrectCallArg(call, 1,
                               GetBuiltinType(agg_ht_kind)->PointerTo());
        return;
      }
      call->set_type(GetBuiltinType(ast::BuiltinType::Nil));
      break;
    }
    case ast::Builtin::AggHashTableIterHasNext: {
      if (!CheckArgCount(call, 1)) {
        return;
      }
      call->set_type(GetBuiltinType(ast::BuiltinType::Bool));
      break;
    }
    case ast::Builtin::AggHashTableIterNext: {
      if (!CheckArgCount(call, 1)) {
        return;
      }
      call->set_type(GetBuiltinType(ast::BuiltinType::Nil));
      break;
    }
    case ast::Builtin::AggHashTableIterGetRow: {
      if (!CheckArgCount(call, 1)) {
        return;
      }
      const auto byte_kind = ast::BuiltinType::Uint8;
      call->set_type(GetBuiltinType(byte_kind)->PointerTo());
      break;
    }
    case ast::Builtin::AggHashTableIterClose: {
      if (!CheckArgCount(call, 1)) {
        return;
      }
      call->set_type(GetBuiltinType(ast::BuiltinType::Nil));
      break;
    }
    default: { UNREACHABLE("Impossible aggregation hash table iterator call"); }
  }
}

void Sema::CheckBuiltinAggPartIterCall(ast::CallExpr *call,
                                       ast::Builtin builtin) {
  if (!CheckArgCount(call, 1)) {
    return;
  }

  const auto &args = call->arguments();

  const auto part_iter_kind = ast::BuiltinType::AggOverflowPartIter;
  if (!IsPointerToSpecificBuiltin(args[0]->type(), part_iter_kind)) {
    ReportIncorrectCallArg(call, 0,
                           GetBuiltinType(part_iter_kind)->PointerTo());
    return;
  }

  switch (builtin) {
    case ast::Builtin::AggPartIterHasNext: {
      call->set_type(GetBuiltinType(ast::BuiltinType::Bool));
      break;
    }
    case ast::Builtin::AggPartIterNext: {
      call->set_type(GetBuiltinType(ast::BuiltinType::Nil));
      break;
    }
    case ast::Builtin::AggPartIterGetRow: {
      const auto byte_kind = ast::BuiltinType::Uint8;
      call->set_type(GetBuiltinType(byte_kind)->PointerTo());
      break;
    }
    case ast::Builtin::AggPartIterGetHash: {
      const auto hash_val_kind = ast::BuiltinType::Uint64;
      call->set_type(GetBuiltinType(hash_val_kind));
      break;
    }
    default: { UNREACHABLE("Impossible aggregation partition iterator call"); }
  }
}

void Sema::CheckBuiltinAggregatorCall(ast::CallExpr *call,
                                      ast::Builtin builtin) {
  const auto &args = call->arguments();
  switch (builtin) {
    case ast::Builtin::AggInit:
    case ast::Builtin::AggReset: {
      // All arguments to @aggInit() or @aggReset() must be SQL aggregators
      for (u32 idx = 0; idx < call->num_args(); idx++) {
        if (!IsPointerToAggregatorValue(args[idx]->type())) {
          error_reporter()->Report(call->position(),
                                   ErrorMessages::kNotASQLAggregate,
                                   args[idx]->type());
          return;
        }
      }
      // Init returns nil
      call->set_type(GetBuiltinType(ast::BuiltinType::Nil));
      break;
    }
    case ast::Builtin::AggAdvance: {
      if (!CheckArgCount(call, 2)) {
        return;
      }
      // First argument to @aggAdvance() must be a SQL aggregator, second must
      // be a SQL value
      if (!IsPointerToAggregatorValue(args[0]->type())) {
        error_reporter()->Report(call->position(),
                                 ErrorMessages::kNotASQLAggregate,
                                 args[0]->type());
        return;
      }
      if (!IsPointerToSQLValue(args[1]->type())) {
        error_reporter()->Report(call->position(),
                                 ErrorMessages::kNotASQLAggregate,
                                 args[1]->type());
        return;
      }
      // Advance returns nil
      call->set_type(GetBuiltinType(ast::BuiltinType::Nil));
      break;
    }
    case ast::Builtin::AggMerge: {
      if (!CheckArgCount(call, 2)) {
        return;
      }
      // Both arguments must be SQL aggregators
      bool arg0_is_agg = IsPointerToAggregatorValue(args[0]->type());
      bool arg1_is_agg = IsPointerToAggregatorValue(args[1]->type());
      if (!arg0_is_agg || !arg1_is_agg) {
        error_reporter()->Report(
            call->position(), ErrorMessages::kNotASQLAggregate,
            (!arg0_is_agg ? args[0]->type() : args[1]->type()));
        return;
      }
      // Merge returns nil
      call->set_type(GetBuiltinType(ast::BuiltinType::Nil));
      break;
    }
    case ast::Builtin::AggResult: {
      if (!CheckArgCount(call, 1)) {
        return;
      }
      // Argument must be a SQL aggregator
      if (!IsPointerToAggregatorValue(args[0]->type())) {
        error_reporter()->Report(call->position(),
                                 ErrorMessages::kNotASQLAggregate,
                                 args[0]->type());
        return;
      }
      // TODO(pmenon): Fix me!
      call->set_type(GetBuiltinType(ast::BuiltinType::Integer));
      break;
    }
    default: { UNREACHABLE("Impossible aggregator call"); }
  }
}

void Sema::CheckBuiltinJoinHashTableInit(ast::CallExpr *call) {
  if (!CheckArgCount(call, 3)) {
    return;
  }

  const auto &args = call->arguments();

  // First argument must be a pointer to a JoinHashTable
  const auto jht_kind = ast::BuiltinType::JoinHashTable;
  if (!IsPointerToSpecificBuiltin(args[0]->type(), jht_kind)) {
    ReportIncorrectCallArg(call, 0, GetBuiltinType(jht_kind)->PointerTo());
    return;
  }

  // Second argument must be a pointer to a MemoryPool
  const auto region_kind = ast::BuiltinType::MemoryPool;
  if (!IsPointerToSpecificBuiltin(args[1]->type(), region_kind)) {
    ReportIncorrectCallArg(call, 1, GetBuiltinType(region_kind)->PointerTo());
    return;
  }

  // Third and last argument must be a 32-bit number representing the tuple size
  if (!args[2]->type()->IsIntegerType()) {
    ReportIncorrectCallArg(call, 2, GetBuiltinType(ast::BuiltinType::Uint32));
    return;
  }

  // This call returns nothing
  call->set_type(GetBuiltinType(ast::BuiltinType::Nil));
}

void Sema::CheckBuiltinJoinHashTableInsert(ast::CallExpr *call) {
  if (!CheckArgCount(call, 2)) {
    return;
  }

  const auto &args = call->arguments();

  // First argument is a pointer to a JoinHashTable
  const auto jht_kind = ast::BuiltinType::JoinHashTable;
  if (!IsPointerToSpecificBuiltin(args[0]->type(), jht_kind)) {
    ReportIncorrectCallArg(call, 0, GetBuiltinType(jht_kind)->PointerTo());
    return;
  }

  // Second argument is a 64-bit unsigned hash value
  if (!args[1]->type()->IsSpecificBuiltin(ast::BuiltinType::Uint64)) {
    ReportIncorrectCallArg(call, 1, GetBuiltinType(ast::BuiltinType::Uint64));
    return;
  }

  // This call returns a byte pointer
  const auto byte_kind = ast::BuiltinType::Uint8;
  call->set_type(GetBuiltinType(byte_kind)->PointerTo());
}

void Sema::CheckBuiltinJoinHashTableBuild(ast::CallExpr *call,
                                          ast::Builtin builtin) {
  if (!CheckArgCountAtLeast(call, 1)) {
    return;
  }

  const auto &call_args = call->arguments();

  // The first and only argument must be a pointer to a JoinHashTable
  const auto jht_kind = ast::BuiltinType::JoinHashTable;
  if (!IsPointerToSpecificBuiltin(call_args[0]->type(), jht_kind)) {
    ReportIncorrectCallArg(call, 0, GetBuiltinType(jht_kind)->PointerTo());
    return;
  }

  switch (builtin) {
    case ast::Builtin::JoinHashTableBuild: {
      break;
    }
    case ast::Builtin::JoinHashTableBuildParallel: {
      if (!CheckArgCount(call, 3)) {
        return;
      }
      // Second argument must be a thread state container pointer
      const auto tls_kind = ast::BuiltinType::ThreadStateContainer;
      if (!IsPointerToSpecificBuiltin(call_args[1]->type(), tls_kind)) {
        ReportIncorrectCallArg(call, 1, GetBuiltinType(tls_kind)->PointerTo());
        return;
      }
      // Third argument must be a 32-bit integer representing the offset
      const auto uint32_kind = ast::BuiltinType::Uint32;
      if (!call_args[2]->type()->IsSpecificBuiltin(uint32_kind)) {
        ReportIncorrectCallArg(call, 2, GetBuiltinType(uint32_kind));
        return;
      }
      break;
    }
    default: { UNREACHABLE("Impossible join hash table build call"); }
  }

  // This call returns nothing
  call->set_type(GetBuiltinType(ast::BuiltinType::Nil));
}

void Sema::CheckBuiltinJoinHashTableFree(ast::CallExpr *call) {
  if (!CheckArgCount(call, 1)) {
    return;
  }

  const auto &args = call->arguments();

  // The first and only argument must be a pointer to a JoinHashTable
  const auto jht_kind = ast::BuiltinType::JoinHashTable;
  if (!IsPointerToSpecificBuiltin(args[0]->type(), jht_kind)) {
    ReportIncorrectCallArg(call, 0, GetBuiltinType(jht_kind)->PointerTo());
    return;
  }

  // This call returns nothing
  call->set_type(GetBuiltinType(ast::BuiltinType::Nil));
}

void Sema::CheckBuiltinJoinHashTableIterInit(ast::CallExpr *call) {
  if (!CheckArgCount(call, 3)) {
    return;
  }

  const auto &args = call->arguments();

  // First argument is a pointer to a JoinHashTableIterator
  const auto jht_iterator_kind = ast::BuiltinType::JoinHashTableIterator;
  if (!IsPointerToSpecificBuiltin(args[0]->type(), jht_iterator_kind)) {
    ReportIncorrectCallArg(call, 0, GetBuiltinType(jht_iterator_kind)->PointerTo());
    return;
  }

  // Second argument is a pointer to a JoinHashTable
  const auto jht_kind = ast::BuiltinType::JoinHashTable;
  if (!IsPointerToSpecificBuiltin(args[1]->type(), jht_kind)) {
    ReportIncorrectCallArg(call, 1, GetBuiltinType(jht_kind)->PointerTo());
    return;
  }

  // Third argument is a 64-bit unsigned hash value
  if (!args[2]->type()->IsSpecificBuiltin(ast::BuiltinType::Uint64)) {
    ReportIncorrectCallArg(call, 2, GetBuiltinType(ast::BuiltinType::Uint64));
    return;
  }

  // This call returns nothing
  call->set_type(GetBuiltinType(ast::BuiltinType::Nil));
}

void Sema::CheckBuiltinJoinHashTableIterHasNext(ast::CallExpr *call) {
  if (!CheckArgCount(call, 4)) {
    return;
  }

  const auto &args = call->arguments();

  // First argument is a pointer to a JoinHashTableIterator
  const auto jht_iterator_kind = ast::BuiltinType::JoinHashTableIterator;
  if (!IsPointerToSpecificBuiltin(args[0]->type(), jht_iterator_kind)) {
    ReportIncorrectCallArg(call, 0, GetBuiltinType(jht_iterator_kind)->PointerTo());
    return;
  }

  // Second argument is a key equality function
  auto *const key_eq_type = args[1]->type()->SafeAs<ast::FunctionType>();
  if (key_eq_type == nullptr || key_eq_type->num_params() != 3 ||
      !key_eq_type->return_type()->IsSpecificBuiltin(ast::BuiltinType::Bool) ||
      !key_eq_type->params()[0].type->IsPointerType() || !key_eq_type->params()[1].type->IsPointerType() ||
      !key_eq_type->params()[2].type->IsPointerType()) {
    error_reporter()->Report(call->position(), ErrorMessages::kBadEqualityFunctionForJHTGetNext, args[1]->type(), 1);
    return;
  }

  // Third argument is an arbitrary pointer
  if (!args[2]->type()->IsPointerType()) {
    error_reporter()->Report(call->position(), ErrorMessages::kBadPointerForJHTGetNext, args[2]->type(), 2);
    return;
  }

  // Fourth argument is an arbitrary pointer
  if (!args[3]->type()->IsPointerType()) {
    error_reporter()->Report(call->position(), ErrorMessages::kBadPointerForJHTGetNext, args[3]->type(), 3);
    return;
  }

  // This call returns a bool
  call->set_type(GetBuiltinType(ast::BuiltinType::Bool));
}

void Sema::CheckBuiltinJoinHashTableIterGetRow(tpl::ast::CallExpr *call) {
  if (!CheckArgCount(call, 1)) {
    return;
  }

  const auto &args = call->arguments();

  // The first argument is a pointer to a JoinHashTableIterator
  const auto jht_iterator_kind = ast::BuiltinType::JoinHashTableIterator;
  if (!IsPointerToSpecificBuiltin(args[0]->type(), jht_iterator_kind)) {
    ReportIncorrectCallArg(call, 0, GetBuiltinType(jht_iterator_kind)->PointerTo());
    return;
  }

  // This call returns a byte pointer
  const auto byte_kind = ast::BuiltinType::Uint8;
  call->set_type(ast::BuiltinType::Get(context(), byte_kind)->PointerTo());
}

void Sema::CheckBuiltinJoinHashTableIterClose(tpl::ast::CallExpr *call) {
  if (!CheckArgCount(call, 1)) {
    return;
  }

  const auto &args = call->arguments();

  // The first argument is a pointer to a JoinHashTableIterator
  const auto jht_iterator_kind = ast::BuiltinType::JoinHashTableIterator;
  if (!IsPointerToSpecificBuiltin(args[0]->type(), jht_iterator_kind)) {
    ReportIncorrectCallArg(call, 0, GetBuiltinType(jht_iterator_kind)->PointerTo());
    return;
  }

  // This call returns nothing
  call->set_type(GetBuiltinType(ast::BuiltinType::Nil));
}

void Sema::CheckBuiltinExecutionContextCall(ast::CallExpr *call,
                                            UNUSED ast::Builtin builtin) {
  if (!CheckArgCount(call, 1)) {
    return;
  }

  const auto &call_args = call->arguments();

  auto exec_ctx_kind = ast::BuiltinType::ExecutionContext;
  if (!IsPointerToSpecificBuiltin(call_args[0]->type(), exec_ctx_kind)) {
    ReportIncorrectCallArg(call, 0, GetBuiltinType(exec_ctx_kind)->PointerTo());
    return;
  }

  auto mem_pool_kind = ast::BuiltinType::MemoryPool;
  call->set_type(GetBuiltinType(mem_pool_kind)->PointerTo());
}

void Sema::CheckBuiltinThreadStateContainerCall(ast::CallExpr *call,
                                                ast::Builtin builtin) {
  if (!CheckArgCountAtLeast(call, 1)) {
    return;
  }

  const auto &call_args = call->arguments();

  // First argument must be thread state container pointer
  auto tls_kind = ast::BuiltinType::ThreadStateContainer;
  if (!IsPointerToSpecificBuiltin(call_args[0]->type(), tls_kind)) {
    ReportIncorrectCallArg(call, 0, GetBuiltinType(tls_kind)->PointerTo());
    return;
  }

  switch (builtin) {
    case ast::Builtin::ThreadStateContainerInit: {
      if (!CheckArgCount(call, 2)) {
        return;
      }

      // Second argument is a MemoryPool
      auto mem_pool_kind = ast::BuiltinType::MemoryPool;
      if (!IsPointerToSpecificBuiltin(call_args[1]->type(), mem_pool_kind)) {
        ReportIncorrectCallArg(call, 1,
                               GetBuiltinType(mem_pool_kind)->PointerTo());
        return;
      }
      break;
    }
    case ast::Builtin::ThreadStateContainerFree: {
      break;
    }
    case ast::Builtin::ThreadStateContainerReset: {
      if (!CheckArgCount(call, 5)) {
        return;
      }
      // Second argument must be an integer size of the state
      const auto uint_kind = ast::BuiltinType::Uint32;
      if (!call_args[1]->type()->IsSpecificBuiltin(uint_kind)) {
        ReportIncorrectCallArg(call, 1, GetBuiltinType(uint_kind));
        return;
      }
      // Third and fourth arguments must be functions
      // TODO(pmenon): More thorough check
      if (!call_args[2]->type()->IsFunctionType() ||
          !call_args[3]->type()->IsFunctionType()) {
        ReportIncorrectCallArg(call, 2,
                               GetBuiltinType(ast::BuiltinType::Uint32));
        return;
      }
      // Fifth argument must be a pointer to something or nil
      if (!call_args[4]->type()->IsPointerType() &&
          !call_args[4]->type()->IsNilType()) {
        ReportIncorrectCallArg(call, 4,
                               GetBuiltinType(ast::BuiltinType::Uint32));
        return;
      }
      break;
    }
    case ast::Builtin::ThreadStateContainerIterate: {
      if (!CheckArgCount(call, 3)) {
        return;
      }
      // Second argument is a pointer to some context
      if (!call_args[1]->type()->IsPointerType()) {
        ReportIncorrectCallArg(call, 1,
                               GetBuiltinType(ast::BuiltinType::Uint32));
        return;
      }
      // Third argument is the iteration function callback
      if (!call_args[2]->type()->IsFunctionType()) {
        ReportIncorrectCallArg(call, 2,
                               GetBuiltinType(ast::BuiltinType::Uint32));
        return;
      }
      break;
    }
    default: { UNREACHABLE("Impossible table iteration call"); }
  }

  // All these calls return nil
  call->set_type(GetBuiltinType(ast::BuiltinType::Nil));
}

void Sema::CheckBuiltinTableIterCall(ast::CallExpr *call, ast::Builtin builtin) {
  const auto &call_args = call->arguments();

  const auto tvi_kind = ast::BuiltinType::TableVectorIterator;
  if (!IsPointerToSpecificBuiltin(call_args[0]->type(), tvi_kind)) {
    ReportIncorrectCallArg(call, 0, GetBuiltinType(tvi_kind)->PointerTo());
    return;
  }

  switch (builtin) {
    case ast::Builtin::TableIterInit: {
      if (!CheckArgCount(call, 3)) {
        return;
      }
      // The second argument is the table name as a literal string
      if (!call_args[1]->IsStringLiteral()) {
        ReportIncorrectCallArg(call, 1, ast::StringType::Get(context()));
        return;
      }
      // The third argument is the execution context
      auto exec_ctx_kind = ast::BuiltinType::ExecutionContext;
      if (!IsPointerToSpecificBuiltin(call_args[2]->type(), exec_ctx_kind)) {
        ReportIncorrectCallArg(call, 2, GetBuiltinType(exec_ctx_kind)->PointerTo());
        return;
      }
      call->set_type(GetBuiltinType(ast::BuiltinType::Nil));
      break;
    }
    case ast::Builtin::TableIterAdvance: {
      // A single-arg builtin returning a boolean
      call->set_type(GetBuiltinType(ast::BuiltinType::Bool));
      break;
    }
    case ast::Builtin::TableIterGetPCI: {
      // A single-arg builtin return a pointer to the current PCI
      const auto pci_kind = ast::BuiltinType::ProjectedColumnsIterator;
      call->set_type(GetBuiltinType(pci_kind)->PointerTo());
      break;
    }
    case ast::Builtin::TableIterClose: {
      // A single-arg builtin returning void
      call->set_type(GetBuiltinType(ast::BuiltinType::Nil));
      break;
    }
    default: { UNREACHABLE("Impossible table iteration call"); }
  }
}

void Sema::CheckBuiltinTableIterParCall(ast::CallExpr *call) {
  if (!CheckArgCount(call, 4)) {
    return;
  }

  const auto &call_args = call->arguments();

  // First argument is table name as a string literal
  if (!call_args[0]->IsStringLiteral()) {
    ReportIncorrectCallArg(call, 0, ast::StringType::Get(context()));
    return;
  }

  // Second argument is an opaque query state. For now, check it's a pointer.
  const auto void_kind = ast::BuiltinType::Nil;
  if (!call_args[1]->type()->IsPointerType()) {
    ReportIncorrectCallArg(call, 1, GetBuiltinType(void_kind)->PointerTo());
    return;
  }

  // Third argument is the thread state container
  const auto tls_kind = ast::BuiltinType::ThreadStateContainer;
  if (!IsPointerToSpecificBuiltin(call_args[2]->type(), tls_kind)) {
    ReportIncorrectCallArg(call, 2, GetBuiltinType(tls_kind)->PointerTo());
    return;
  }

  // Third argument is scanner function
  auto *scan_fn_type = call_args[3]->type()->SafeAs<ast::FunctionType>();
  if (scan_fn_type == nullptr) {
    error_reporter()->Report(call->position(), ErrorMessages::kBadParallelScanFunction, call_args[3]->type());
    return;
  }
  // Check type
  const auto tvi_kind = ast::BuiltinType::TableVectorIterator;
  const auto &params = scan_fn_type->params();
  if (params.size() != 3 || !params[0].type->IsPointerType() || !params[1].type->IsPointerType() ||
      !IsPointerToSpecificBuiltin(params[2].type, tvi_kind)) {
    error_reporter()->Report(call->position(), ErrorMessages::kBadParallelScanFunction, call_args[3]->type());
    return;
  }

  // Nil
  call->set_type(GetBuiltinType(ast::BuiltinType::Nil));
}

void Sema::CheckBuiltinPCICall(ast::CallExpr *call, ast::Builtin builtin) {
  if (!CheckArgCountAtLeast(call, 1)) {
    return;
  }

  // The first argument must be a *PCI
  const auto pci_kind = ast::BuiltinType::ProjectedColumnsIterator;
  if (!IsPointerToSpecificBuiltin(call->arguments()[0]->type(), pci_kind)) {
    ReportIncorrectCallArg(call, 0, GetBuiltinType(pci_kind)->PointerTo());
    return;
  }

  switch (builtin) {
    case ast::Builtin::PCIIsFiltered:
    case ast::Builtin::PCIHasNext:
    case ast::Builtin::PCIHasNextFiltered:
    case ast::Builtin::PCIAdvance:
    case ast::Builtin::PCIAdvanceFiltered:
    case ast::Builtin::PCIReset:
    case ast::Builtin::PCIResetFiltered: {
      call->set_type(GetBuiltinType(ast::BuiltinType::Bool));
      break;
    }
    case ast::Builtin::PCIMatch: {
      if (!CheckArgCount(call, 2)) {
        return;
      }
      // If the match argument is a SQL boolean, implicitly cast to native
      ast::Expr *match_arg = call->arguments()[1];
      if (match_arg->type()->IsSpecificBuiltin(ast::BuiltinType::Boolean)) {
        match_arg = ImplCastExprToType(match_arg,
                                       GetBuiltinType(ast::BuiltinType::Bool),
                                       ast::CastKind::SqlBoolToBool);
        call->set_argument(1, match_arg);
      }
      // If the match argument isn't a native boolean , error
      if (!match_arg->type()->IsBoolType()) {
        ReportIncorrectCallArg(call, 1, GetBuiltinType(ast::BuiltinType::Bool));
        return;
      }
      call->set_type(GetBuiltinType(ast::BuiltinType::Nil));
      break;
    }
    case ast::Builtin::PCIGetSmallInt:
    case ast::Builtin::PCIGetInt:
    case ast::Builtin::PCIGetBigInt: {
      call->set_type(GetBuiltinType(ast::BuiltinType::Integer));
      break;
    }
    case ast::Builtin::PCIGetReal:
    case ast::Builtin::PCIGetDouble: {
      call->set_type(GetBuiltinType(ast::BuiltinType::Real));
      break;
    }
    default: { UNREACHABLE("Impossible PCI call"); }
  }
}

void Sema::CheckBuiltinHashCall(ast::CallExpr *call, UNUSED ast::Builtin builtin) {
  if (!CheckArgCountAtLeast(call, 1)) {
    return;
  }

  // All arguments must be SQL types
  for (const auto &arg : call->arguments()) {
    if (!arg->type()->IsSqlValueType()) {
      error_reporter()->Report(arg->position(), ErrorMessages::kBadHashArg, arg->type());
      return;
    }
  }

  // Result is a hash value
  call->set_type(GetBuiltinType(ast::BuiltinType::Uint64));
}

void Sema::CheckBuiltinFilterManagerCall(ast::CallExpr *const call, const ast::Builtin builtin) {
  if (!CheckArgCountAtLeast(call, 1)) {
    return;
  }

  // The first argument must be a *FilterManagerBuilder
  const auto fm_kind = ast::BuiltinType::FilterManager;
  if (!IsPointerToSpecificBuiltin(call->arguments()[0]->type(), fm_kind)) {
    ReportIncorrectCallArg(call, 0, GetBuiltinType(fm_kind)->PointerTo());
    return;
  }

  switch (builtin) {
    case ast::Builtin::FilterManagerInit:
    case ast::Builtin::FilterManagerFinalize:
    case ast::Builtin::FilterManagerFree: {
      call->set_type(GetBuiltinType(ast::BuiltinType::Nil));
      break;
    }
    case ast::Builtin::FilterManagerInsertFilter: {
      for (u32 arg_idx = 1; arg_idx < call->num_args(); arg_idx++) {
        // clang-format off
        auto *arg_type = call->arguments()[arg_idx]->type()->SafeAs<ast::FunctionType>();
        if (arg_type == nullptr ||                                              // not a function
            !arg_type->return_type()->IsIntegerType() ||                        // doesn't return an integer
            arg_type->num_params() != 1 ||                                      // isn't a single-arg func
            arg_type->params()[0].type->GetPointeeType() == nullptr ||          // first arg isn't a *PCI
            !arg_type->params()[0].type->GetPointeeType()->IsSpecificBuiltin(
                ast::BuiltinType::ProjectedColumnsIterator)) {
          // error
          error_reporter()->Report(
              call->position(), ErrorMessages::kIncorrectCallArgType,
              call->GetFuncName(),
              ast::BuiltinType::Get(context(), fm_kind)->PointerTo(), arg_idx,
              call->arguments()[arg_idx]->type());
          return;
        }
        // clang-format on
      }
      call->set_type(GetBuiltinType(ast::BuiltinType::Nil));
      break;
    }
    case ast::Builtin::FilterManagerRunFilters: {
      const auto pci_kind = ast::BuiltinType::ProjectedColumnsIterator;
      if (!IsPointerToSpecificBuiltin(call->arguments()[1]->type(), pci_kind)) {
        ReportIncorrectCallArg(call, 1, GetBuiltinType(pci_kind)->PointerTo());
        return;
      }
      call->set_type(GetBuiltinType(ast::BuiltinType::Nil));
      break;
    }
    default: { UNREACHABLE("Impossible FilterManager call"); }
  }
}

void Sema::CheckMathTrigCall(ast::CallExpr *call, ast::Builtin builtin) {
  const auto real_kind = ast::BuiltinType::Real;

  const auto &call_args = call->arguments();
  switch (builtin) {
    case ast::Builtin::ATan2: {
      if (!CheckArgCount(call, 2)) {
        return;
      }
      if (!call_args[0]->type()->IsSpecificBuiltin(real_kind) ||
          !call_args[1]->type()->IsSpecificBuiltin(real_kind)) {
        ReportIncorrectCallArg(call, 1, GetBuiltinType(real_kind));
        return;
      }
      break;
    }
    case ast::Builtin::Cos:
    case ast::Builtin::Cot:
    case ast::Builtin::Sin:
    case ast::Builtin::Tan:
    case ast::Builtin::ACos:
    case ast::Builtin::ASin:
    case ast::Builtin::ATan: {
      if (!CheckArgCount(call, 1)) {
        return;
      }
      if (!call_args[0]->type()->IsSpecificBuiltin(real_kind)) {
        ReportIncorrectCallArg(call, 0, GetBuiltinType(real_kind));
        return;
      }
      break;
    }
    default: { UNREACHABLE("Impossible math trig function call"); }
  }

  // Trig functions return real values
  call->set_type(GetBuiltinType(real_kind));
}

void Sema::CheckBuiltinSizeOfCall(ast::CallExpr *call) {
  if (!CheckArgCount(call, 1)) {
    return;
  }

  // This call returns an unsigned 32-bit value for the size of the type
  call->set_type(GetBuiltinType(ast::BuiltinType::Uint32));
}

void Sema::CheckBuiltinPtrCastCall(ast::CallExpr *call) {
  if (!CheckArgCount(call, 2)) {
    return;
  }

  // The first argument will be a UnaryOpExpr with the '*' (star) op. This is
  // because parsing function calls assumes expression arguments, not types. So,
  // something like '*Type', which would be the first argument to @ptrCast, will
  // get parsed as a dereference expression before a type expression.
  // TODO(pmenon): Fix the above to parse correctly

  auto unary_op = call->arguments()[0]->SafeAs<ast::UnaryOpExpr>();
  if (unary_op == nullptr || unary_op->op() != parsing::Token::Type::STAR) {
    error_reporter()->Report(call->position(), ErrorMessages::kBadArgToPtrCast, call->arguments()[0]->type(), 1);
    return;
  }

  // Replace the unary with a PointerTypeRepr node and resolve it
  call->set_argument(0, context()->node_factory()->NewPointerType(call->arguments()[0]->position(), unary_op->expr()));

  for (auto *arg : call->arguments()) {
    auto *resolved_type = Resolve(arg);
    if (resolved_type == nullptr) {
      return;
    }
  }

  // Both arguments must be pointer types
  if (!call->arguments()[0]->type()->IsPointerType() || !call->arguments()[1]->type()->IsPointerType()) {
    error_reporter()->Report(call->position(), ErrorMessages::kBadArgToPtrCast, call->arguments()[0]->type(), 1);
    return;
  }

  // Apply the cast
  call->set_type(call->arguments()[0]->type());
}

void Sema::CheckBuiltinSorterInit(ast::CallExpr *call) {
  if (!CheckArgCount(call, 4)) {
    return;
  }

  const auto &args = call->arguments();

  // First argument must be a pointer to a Sorter
  const auto sorter_kind = ast::BuiltinType::Sorter;
  if (!IsPointerToSpecificBuiltin(args[0]->type(), sorter_kind)) {
    ReportIncorrectCallArg(call, 0, GetBuiltinType(sorter_kind)->PointerTo());
    return;
  }

  // Second argument must be a pointer to a MemoryPool
  const auto mem_kind = ast::BuiltinType::MemoryPool;
  if (!IsPointerToSpecificBuiltin(args[1]->type(), mem_kind)) {
    ReportIncorrectCallArg(call, 1, GetBuiltinType(mem_kind)->PointerTo());
    return;
  }

  // Second argument must be a function
  auto *const cmp_func_type = args[2]->type()->SafeAs<ast::FunctionType>();
  if (cmp_func_type == nullptr || cmp_func_type->num_params() != 2 ||
      !cmp_func_type->return_type()->IsSpecificBuiltin(ast::BuiltinType::Int32) ||
      !cmp_func_type->params()[0].type->IsPointerType() || !cmp_func_type->params()[1].type->IsPointerType()) {
    error_reporter()->Report(call->position(), ErrorMessages::kBadComparisonFunctionForSorter, args[2]->type());
    return;
  }

  // Third and last argument must be a 32-bit number representing the tuple size
  const auto uint_kind = ast::BuiltinType::Uint32;
  if (!args[3]->type()->IsSpecificBuiltin(uint_kind)) {
    ReportIncorrectCallArg(call, 3, GetBuiltinType(uint_kind));
    return;
  }

  // This call returns nothing
  call->set_type(GetBuiltinType(ast::BuiltinType::Nil));
}

void Sema::CheckBuiltinSorterInsert(ast::CallExpr *call) {
  if (!CheckArgCount(call, 1)) {
    return;
  }

  // First argument must be a pointer to a Sorter
  const auto sorter_kind = ast::BuiltinType::Sorter;
  if (!IsPointerToSpecificBuiltin(call->arguments()[0]->type(), sorter_kind)) {
    ReportIncorrectCallArg(call, 0, GetBuiltinType(sorter_kind)->PointerTo());
    return;
  }

  // This call returns nothing
  call->set_type(GetBuiltinType(ast::BuiltinType::Uint8)->PointerTo());
}

void Sema::CheckBuiltinSorterSort(ast::CallExpr *call, ast::Builtin builtin) {
  if (!CheckArgCountAtLeast(call, 1)) {
    return;
  }

  const auto &call_args = call->arguments();

  // First argument must be a pointer to a Sorter
  const auto sorter_kind = ast::BuiltinType::Sorter;
  if (!IsPointerToSpecificBuiltin(call_args[0]->type(), sorter_kind)) {
    ReportIncorrectCallArg(call, 0, GetBuiltinType(sorter_kind)->PointerTo());
    return;
  }

  switch (builtin) {
    case ast::Builtin::SorterSort: {
      if (!CheckArgCount(call, 1)) {
        return;
      }
      break;
    }
    case ast::Builtin::SorterSortParallel:
    case ast::Builtin::SorterSortTopKParallel: {
      const auto tls_kind = ast::BuiltinType::ThreadStateContainer;
      if (!IsPointerToSpecificBuiltin(call_args[1]->type(), tls_kind)) {
        ReportIncorrectCallArg(call, 1, GetBuiltinType(tls_kind)->PointerTo());
        return;
      }
      // Third argument must be a 32-bit integer representing the offset
      const auto uint32_kind = ast::BuiltinType::Uint32;
      if (!call_args[2]->type()->IsSpecificBuiltin(uint32_kind)) {
        ReportIncorrectCallArg(call, 2, GetBuiltinType(uint32_kind));
        return;
      }

      if (builtin == ast::Builtin::SorterSortTopKParallel) {
        if (!CheckArgCount(call, 4)) {
          return;
        }

        // Last argument must be the TopK value
        const auto uint64_kind = ast::BuiltinType::Uint64;
        if (!call_args[3]->type()->IsSpecificBuiltin(uint64_kind)) {
          ReportIncorrectCallArg(call, 3, GetBuiltinType(uint64_kind));
          return;
        }
      }
      break;
    }
    default: { UNREACHABLE("Impossible sorter sort call"); }
  }

  // This call returns nothing
  call->set_type(GetBuiltinType(ast::BuiltinType::Nil));
}

void Sema::CheckBuiltinSorterFree(ast::CallExpr *call) {
  if (!CheckArgCount(call, 1)) {
    return;
  }

  // First argument must be a pointer to a Sorter
  const auto sorter_kind = ast::BuiltinType::Sorter;
  if (!IsPointerToSpecificBuiltin(call->arguments()[0]->type(), sorter_kind)) {
    ReportIncorrectCallArg(call, 0, GetBuiltinType(sorter_kind)->PointerTo());
    return;
  }

  // This call returns nothing
  call->set_type(GetBuiltinType(ast::BuiltinType::Nil));
}

void Sema::CheckBuiltinSorterIterCall(ast::CallExpr *call, ast::Builtin builtin) {
  if (!CheckArgCountAtLeast(call, 1)) {
    return;
  }

  const auto &args = call->arguments();

  const auto sorter_iter_kind = ast::BuiltinType::SorterIterator;
  if (!IsPointerToSpecificBuiltin(args[0]->type(), sorter_iter_kind)) {
    ReportIncorrectCallArg(call, 0, GetBuiltinType(sorter_iter_kind)->PointerTo());
    return;
  }

  switch (builtin) {
    case ast::Builtin::SorterIterInit: {
      if (!CheckArgCount(call, 2)) {
        return;
      }

      // The second argument is the sorter instance to iterate over
      const auto sorter_kind = ast::BuiltinType::Sorter;
      if (!IsPointerToSpecificBuiltin(args[1]->type(), sorter_kind)) {
        ReportIncorrectCallArg(call, 1, GetBuiltinType(sorter_kind)->PointerTo());
        return;
      }
      call->set_type(GetBuiltinType(ast::BuiltinType::Nil));
      break;
    }
    case ast::Builtin::SorterIterHasNext: {
      call->set_type(GetBuiltinType(ast::BuiltinType::Bool));
      break;
    }
    case ast::Builtin::SorterIterNext: {
      call->set_type(GetBuiltinType(ast::BuiltinType::Nil));
      break;
    }
    case ast::Builtin::SorterIterGetRow: {
      call->set_type(GetBuiltinType(ast::BuiltinType::Uint8)->PointerTo());
      break;
    }
    case ast::Builtin::SorterIterClose: {
      call->set_type(GetBuiltinType(ast::BuiltinType::Nil));
      break;
    }
    default: { UNREACHABLE("Impossible table iteration call"); }
  }
}

void Sema::CheckBuiltinOutputAlloc(tpl::ast::CallExpr *call) {
  if (!CheckArgCount(call, 1)) {
    return;
  }

  // The first call argument must an execution context
  auto exec_ctx_kind = ast::BuiltinType::ExecutionContext;
  if (!IsPointerToSpecificBuiltin(call->arguments()[0]->type(), exec_ctx_kind)) {
    ReportIncorrectCallArg(call, 0, GetBuiltinType(exec_ctx_kind)->PointerTo());
    return;
  }

  // Return a byte*
  ast::Type *ret_type = ast::BuiltinType::Get(context(), ast::BuiltinType::Uint8)->PointerTo();
  call->set_type(ret_type);
}

void Sema::CheckBuiltinOutputAdvance(tpl::ast::CallExpr *call) {
  if (!CheckArgCount(call, 1)) {
    return;
  }

  // The first call argument must an execution context
  auto exec_ctx_kind = ast::BuiltinType::ExecutionContext;
  if (!IsPointerToSpecificBuiltin(call->arguments()[0]->type(), exec_ctx_kind)) {
    ReportIncorrectCallArg(call, 0, GetBuiltinType(exec_ctx_kind)->PointerTo());
    return;
  }

  // Return nothing
  call->set_type(GetBuiltinType(ast::BuiltinType::Nil));
}

void Sema::CheckBuiltinOutputFinalize(tpl::ast::CallExpr *call) {
  if (!CheckArgCount(call, 1)) {
    return;
  }

  // The first call argument must an execution context
  auto exec_ctx_kind = ast::BuiltinType::ExecutionContext;
  if (!IsPointerToSpecificBuiltin(call->arguments()[0]->type(), exec_ctx_kind)) {
    ReportIncorrectCallArg(call, 0, GetBuiltinType(exec_ctx_kind)->PointerTo());
    return;
  }

  // Return nothing
  call->set_type(GetBuiltinType(ast::BuiltinType::Nil));
}

void Sema::CheckBuiltinInsert(tpl::ast::CallExpr *call) {
  if (!CheckArgCount(call, 3)) {
    return;
  }
  // Return nothing
  call->set_type(GetBuiltinType(ast::BuiltinType::Nil));
}

void Sema::CheckBuiltinOutputSetNull(tpl::ast::CallExpr *call) {
  if (!CheckArgCount(call, 2)) {
    return;
  }

  // The first call argument must an execution context
  auto exec_ctx_kind = ast::BuiltinType::ExecutionContext;
  if (!IsPointerToSpecificBuiltin(call->arguments()[0]->type(), exec_ctx_kind)) {
    ReportIncorrectCallArg(call, 0, GetBuiltinType(exec_ctx_kind)->PointerTo());
    return;
  }

  // The argument should be an integer
  auto *entry_size_type = call->arguments()[1]->type();
  if (!entry_size_type->IsIntegerType()) {
    ReportIncorrectCallArg(call, 1, GetBuiltinType(ast::BuiltinType::Uint32));
    return;
  }
  // Return nothing
  call->set_type(GetBuiltinType(ast::BuiltinType::Nil));
}

void Sema::CheckBuiltinIndexIteratorInit(tpl::ast::CallExpr *call) {
  if (!CheckArgCount(call, 2)) {
    return;
  }

  // First argument must be a pointer to a IndexIterator
  const auto index_kind = ast::BuiltinType::IndexIterator;
  if (!IsPointerToSpecificBuiltin(call->arguments()[0]->type(), index_kind)) {
    ReportIncorrectCallArg(call, 0, GetBuiltinType(index_kind)->PointerTo());
    return;
  }
  // The second call argument must be a string
  if (!call->arguments()[1]->type()->IsStringType()) {
    ReportIncorrectCallArg(call, 1, ast::StringType::Get(context()));
    return;
  }

  // The third call argument must an execution context
  auto exec_ctx_kind = ast::BuiltinType::ExecutionContext;
  if (!IsPointerToSpecificBuiltin(call->arguments()[2]->type(), exec_ctx_kind)) {
    ReportIncorrectCallArg(call, 2, GetBuiltinType(exec_ctx_kind)->PointerTo());
    return;
  }
  call->set_type(GetBuiltinType(ast::BuiltinType::Nil));

  // Return nothing
  call->set_type(GetBuiltinType(ast::BuiltinType::Nil));
}

void Sema::CheckBuiltinIndexIteratorScanKey(tpl::ast::CallExpr *call) {
  if (call->num_args() != 2) {
    error_reporter()->Report(call->position(), ErrorMessages::kMismatchedCallArgs, call->GetFuncName(), 2,
                             call->num_args());
    return;
  }
  // First argument must be a pointer to a IndexIterator
  auto *index_type = call->arguments()[0]->type()->GetPointeeType();
  if (index_type == nullptr || !index_type->IsSpecificBuiltin(ast::BuiltinType::IndexIterator)) {
    error_reporter()->Report(call->position(), ErrorMessages::kBadArgToIndexIteratorScanKey,
                             call->arguments()[0]->type(), 0);
    return;
  }
  // Second argument should be a byte array
  auto *byte_type = call->arguments()[1]->type()->GetPointeeType();
  if (byte_type == nullptr || !byte_type->IsSpecificBuiltin(ast::BuiltinType::Int8)) {
    error_reporter()->Report(call->position(), ErrorMessages::kBadArgToIndexIteratorScanKey,
                             call->arguments()[1]->type(), 1);
    return;
  }
  // Return nothing
  call->set_type(GetBuiltinType(ast::BuiltinType::Nil));
}

void Sema::CheckBuiltinIndexIteratorFree(tpl::ast::CallExpr *call) {
  if (call->num_args() != 1) {
    error_reporter()->Report(call->position(), ErrorMessages::kMismatchedCallArgs, call->GetFuncName(), 1,
                             call->num_args());
    return;
  }
  // First argument must be a pointer to a IndexIterator
  auto *index_type = call->arguments()[0]->type()->GetPointeeType();
  if (index_type == nullptr || !index_type->IsSpecificBuiltin(ast::BuiltinType::IndexIterator)) {
    error_reporter()->Report(call->position(), ErrorMessages::kBadArgToIndexIteratorFree, call->arguments()[0]->type(),
                             0);
    return;
  }
  // Return nothing
  call->set_type(GetBuiltinType(ast::BuiltinType::Nil));
}

void Sema::CheckBuiltinCall(ast::CallExpr *call) {
  ast::Builtin builtin;
  if (!context()->IsBuiltinFunction(call->GetFuncName(), &builtin)) {
    error_reporter()->Report(call->function()->position(), ErrorMessages::kInvalidBuiltinFunction, call->GetFuncName());
    return;
  }

  if (builtin == ast::Builtin::PtrCast) {
    CheckBuiltinPtrCastCall(call);
    return;
  }

  // First, resolve all call arguments. If any fail, exit immediately.
  for (auto *arg : call->arguments()) {
    auto *resolved_type = Resolve(arg);
    if (resolved_type == nullptr) {
      return;
    }
  }

  switch (builtin) {
    case ast::Builtin::BoolToSql:
    case ast::Builtin::IntToSql:
    case ast::Builtin::FloatToSql:
    case ast::Builtin::SqlToBool: {
      CheckBuiltinSqlConversionCall(call, builtin);
      break;
    }
    case ast::Builtin::FilterEq:
    case ast::Builtin::FilterGe:
    case ast::Builtin::FilterGt:
    case ast::Builtin::FilterLt:
    case ast::Builtin::FilterNe:
    case ast::Builtin::FilterLe: {
      CheckBuiltinFilterCall(call);
      break;
    }
    case ast::Builtin::ExecutionContextGetMemoryPool: {
      CheckBuiltinExecutionContextCall(call, builtin);
      break;
    }
    case ast::Builtin::ThreadStateContainerInit:
    case ast::Builtin::ThreadStateContainerReset:
    case ast::Builtin::ThreadStateContainerIterate:
    case ast::Builtin::ThreadStateContainerFree: {
      CheckBuiltinThreadStateContainerCall(call, builtin);
      break;
    }
    case ast::Builtin::TableIterInit:
    case ast::Builtin::TableIterAdvance:
    case ast::Builtin::TableIterGetPCI:
    case ast::Builtin::TableIterClose: {
      CheckBuiltinTableIterCall(call, builtin);
      break;
    }
    case ast::Builtin::TableIterParallel: {
      CheckBuiltinTableIterParCall(call);
      break;
    }
    case ast::Builtin::PCIIsFiltered:
    case ast::Builtin::PCIHasNext:
    case ast::Builtin::PCIHasNextFiltered:
    case ast::Builtin::PCIAdvance:
    case ast::Builtin::PCIAdvanceFiltered:
    case ast::Builtin::PCIMatch:
    case ast::Builtin::PCIReset:
    case ast::Builtin::PCIResetFiltered:
    case ast::Builtin::PCIGetSmallInt:
    case ast::Builtin::PCIGetInt:
    case ast::Builtin::PCIGetBigInt:
    case ast::Builtin::PCIGetReal:
    case ast::Builtin::PCIGetDouble: {
      CheckBuiltinPCICall(call, builtin);
      break;
    }
    case ast::Builtin::Hash: {
      CheckBuiltinHashCall(call, builtin);
      break;
    }
    case ast::Builtin::FilterManagerInit:
    case ast::Builtin::FilterManagerInsertFilter:
    case ast::Builtin::FilterManagerFinalize:
    case ast::Builtin::FilterManagerRunFilters:
    case ast::Builtin::FilterManagerFree: {
      CheckBuiltinFilterManagerCall(call, builtin);
      break;
    }
    case ast::Builtin::AggHashTableInit:
    case ast::Builtin::AggHashTableInsert:
    case ast::Builtin::AggHashTableLookup:
    case ast::Builtin::AggHashTableProcessBatch:
    case ast::Builtin::AggHashTableMovePartitions:
    case ast::Builtin::AggHashTableParallelPartitionedScan:
    case ast::Builtin::AggHashTableFree: {
      CheckBuiltinAggHashTableCall(call, builtin);
      break;
    }
    case ast::Builtin::AggHashTableIterInit:
    case ast::Builtin::AggHashTableIterHasNext:
    case ast::Builtin::AggHashTableIterNext:
    case ast::Builtin::AggHashTableIterGetRow:
    case ast::Builtin::AggHashTableIterClose: {
      CheckBuiltinAggHashTableIterCall(call, builtin);
      break;
    }
    case ast::Builtin::AggPartIterHasNext:
    case ast::Builtin::AggPartIterNext:
    case ast::Builtin::AggPartIterGetRow:
    case ast::Builtin::AggPartIterGetHash: {
      CheckBuiltinAggPartIterCall(call, builtin);
      break;
    }
    case ast::Builtin::AggInit:
    case ast::Builtin::AggAdvance:
    case ast::Builtin::AggMerge:
    case ast::Builtin::AggReset:
    case ast::Builtin::AggResult: {
      CheckBuiltinAggregatorCall(call, builtin);
      break;
    }
    case ast::Builtin::JoinHashTableInit: {
      CheckBuiltinJoinHashTableInit(call);
      break;
    }
    case ast::Builtin::JoinHashTableInsert: {
      CheckBuiltinJoinHashTableInsert(call);
      break;
    }
    case ast::Builtin::JoinHashTableIterInit: {
      CheckBuiltinJoinHashTableIterInit(call);
      break;
    }
    case ast::Builtin::JoinHashTableIterHasNext: {
      CheckBuiltinJoinHashTableIterHasNext(call);
      break;
    }
    case ast::Builtin::JoinHashTableIterGetRow: {
      CheckBuiltinJoinHashTableIterGetRow(call);
      break;
    }
    case ast::Builtin::JoinHashTableIterClose: {
      CheckBuiltinJoinHashTableIterClose(call);
      break;
    }
    case ast::Builtin::JoinHashTableBuild:
    case ast::Builtin::JoinHashTableBuildParallel: {
      CheckBuiltinJoinHashTableBuild(call, builtin);
      break;
    }
    case ast::Builtin::JoinHashTableFree: {
      CheckBuiltinJoinHashTableFree(call);
      break;
    }
    case ast::Builtin::SorterInit: {
      CheckBuiltinSorterInit(call);
      break;
    }
    case ast::Builtin::SorterInsert: {
      CheckBuiltinSorterInsert(call);
      break;
    }
    case ast::Builtin::SorterSort:
    case ast::Builtin::SorterSortParallel:
    case ast::Builtin::SorterSortTopKParallel: {
      CheckBuiltinSorterSort(call, builtin);
      break;
    }
    case ast::Builtin::SorterFree: {
      CheckBuiltinSorterFree(call);
      break;
    }
    case ast::Builtin::SorterIterInit:
    case ast::Builtin::SorterIterHasNext:
    case ast::Builtin::SorterIterNext:
    case ast::Builtin::SorterIterGetRow:
    case ast::Builtin::SorterIterClose: {
      CheckBuiltinSorterIterCall(call, builtin);
      break;
    }
    case ast::Builtin::SizeOf: {
      CheckBuiltinSizeOfCall(call);
      break;
    }
    case ast::Builtin::OutputAlloc: {
      CheckBuiltinOutputAlloc(call);
      break;
    }
    case ast::Builtin::OutputAdvance: {
      CheckBuiltinOutputAdvance(call);
      break;
    }
    case ast::Builtin::OutputSetNull: {
      CheckBuiltinOutputSetNull(call);
      break;
    }
    case ast::Builtin::OutputFinalize: {
      CheckBuiltinOutputFinalize(call);
      break;
    }
    case ast::Builtin::Insert: {
      CheckBuiltinInsert(call);
      break;
    }
    case ast::Builtin::IndexIteratorInit: {
      CheckBuiltinIndexIteratorInit(call);
      break;
    }
    case ast::Builtin::IndexIteratorScanKey: {
      CheckBuiltinIndexIteratorScanKey(call);
      break;
    }
    case ast::Builtin::IndexIteratorFree: {
      CheckBuiltinIndexIteratorFree(call);
      break;
    }
    case ast::Builtin::ACos:
    case ast::Builtin::ASin:
    case ast::Builtin::ATan:
    case ast::Builtin::ATan2:
    case ast::Builtin::Cos:
    case ast::Builtin::Cot:
    case ast::Builtin::Sin:
    case ast::Builtin::Tan: {
      CheckMathTrigCall(call, builtin);
      break;
    }
    default: { UNREACHABLE("Unhandled builtin!"); }
  }
}

}  // namespace tpl::sema