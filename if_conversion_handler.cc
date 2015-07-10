#include <iostream>
#include "clang/Lex/Lexer.h"
#include "clang_utility_functions.h"
#include "if_conversion_handler.h"

using namespace clang;
using namespace clang::ast_matchers;

void IfConversionHandler::run(const MatchFinder::MatchResult & t_result) {
  const auto * function_decl = t_result.Nodes.getNodeAs<FunctionDecl>("functionDecl");
  assert(function_decl != nullptr);
  assert(function_decl->getBody() != nullptr);
  std::string current_stream = "";
  if_convert(current_stream, "", function_decl->getBody());
  std::cout << current_stream << "\n";
}

void IfConversionHandler::if_convert(std::string & current_stream,
                                     const std::string & predicate,
                                     const Stmt * stmt) const {
  // For unique renaming
  static uint8_t var_counter = 0;

  if (isa<CompoundStmt>(stmt)) {
    for (const auto & child : stmt->children()) {
      if_convert(current_stream, predicate, child);
    }
  } else if (isa<IfStmt>(stmt)) {
    const auto * if_stmt = dyn_cast<IfStmt>(stmt);

    if (if_stmt->getConditionVariableDeclStmt()) {
      throw std::logic_error("We don't yet handle declarations within the test portion of an if\n");
    }

    // Create temporary variable to hold the if condition
    const auto condition_type_name = if_stmt->getCond()->getType().getAsString();
    const auto cond_variable       = "tmp__" + std::to_string(var_counter++); // TODO: This is sleazy, fix this sometime
    const auto cond_var_assignment = cond_variable + " = " + clang_stmt_printer(if_stmt->getCond()) + ";";
    const auto cond_var_decl       = condition_type_name + " " + cond_variable + ";";

    // Add cond var decl to the very beginning, so that all decls accumulate there
    current_stream.insert(0, cond_var_decl);

    // Add assignment here, predicating it with the current predicate
    current_stream += cond_variable + " = (" + predicate + " ? (" + clang_stmt_printer(if_stmt->getCond()) + ") :  " + cond_variable + ")";


    // predicate = "" is the same as predicate = TRUE, TRUE and cond_variable = cond_variable
    auto pred_within_if_block = (predicate == "") ? cond_variable : ("(" + predicate + " && " + cond_variable + ")");
    auto pred_within_else_block = (predicate == "") ? "!" + cond_variable : ("(" + predicate + " && !" + cond_variable + ")");

    // If convert statements within getThen block to ternary operators.
    if_convert(current_stream, pred_within_if_block, if_stmt->getThen());

    // If there is a getElse block, handle it recursively again
    if (if_stmt->getElse() != nullptr) {
      if_convert(current_stream, pred_within_else_block, if_stmt->getElse());
    }
  } else if (isa<BinaryOperator>(stmt)) {
    current_stream += if_convert_atomic_stmt(dyn_cast<BinaryOperator>(stmt), predicate);
  } else if (isa<DeclStmt>(stmt)) {
    // Just append statement as is, but check that this only happens at the
    // top level i.e. when predicat = "" or true
    assert(predicate == "");
    current_stream += clang_stmt_printer(stmt) + ";";
    return;
  } else {
    assert(false);
  }
}

std::string IfConversionHandler::if_convert_atomic_stmt(const BinaryOperator * stmt,
                                                        const std::string & predicate) const {
  assert(stmt);
  assert(stmt->isAssignmentOp());
  assert(not stmt->isCompoundAssignmentOp());

  // Create predicated version of BinaryOperator
  const std::string lhs = clang_stmt_printer(dyn_cast<BinaryOperator>(stmt)->getLHS());
  std::string rhs;
  if (predicate != "") {
    // non-trivial predicate
    rhs = "(" + predicate + " ? (" + clang_stmt_printer(stmt->getRHS()) + ") :  " + lhs + ")";
  } else {
    rhs = clang_stmt_printer(stmt->getRHS());
  }
  return (lhs + " = " + rhs + ";");
}
