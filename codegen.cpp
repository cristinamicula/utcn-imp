// This file is part of the IMP project.

#include <cassert>
#include <cstring>
#include <iostream>

#include "codegen.h"
#include "ast.h"



// -----------------------------------------------------------------------------
Codegen::Scope::~Scope()
{
}

// -----------------------------------------------------------------------------
Codegen::Binding Codegen::GlobalScope::Lookup(const std::string &name) const
{
  // Find the name among functions.
  if (auto it = funcs_.find(name); it != funcs_.end()) {
    Binding b;
    b.Kind = Binding::Kind::FUNC;
    b.Entry = it->second;
    return b;
  }

  // Find the name among prototypes.
  if (auto it = protos_.find(name); it != protos_.end()) {
    Binding b;
    b.Kind = Binding::Kind::PROTO;
    b.Fn = it->second;
    return b;
  }

  // The verifier should assert all names are bound.
  assert(!"name not bound");
}

// -----------------------------------------------------------------------------
void Codegen::GlobalScope::AddVariable(const std::string &name, uint32_t loc)
{
  assert(!"cannot add local variable to global scope");
}

// -----------------------------------------------------------------------------
int Codegen::GlobalScope::GetNumberOfVariables() const {
  return 0;
}

// -----------------------------------------------------------------------------
uint32_t Codegen::GlobalScope::GetVariableLocation(std::string variable) const {
  assert(!"function scope does not have variables");
  return 0;
}

// -----------------------------------------------------------------------------
Codegen::Binding Codegen::FuncScope::Lookup(const std::string &name) const
{
  // Find the name among arguments.
  if (auto it = args_.find(name); it != args_.end()) {
    Binding b;
    b.Kind = Binding::Kind::ARG;
    b.Index = it->second;
    return b;
  }
  return parent_->Lookup(name);
}

// -----------------------------------------------------------------------------
void Codegen::FuncScope::AddVariable(const std::string &name, uint32_t loc)
{
  assert(!"cannot add local variable to function scope");
}

// -----------------------------------------------------------------------------
int Codegen::FuncScope::GetNumberOfVariables() const {
  return 0;
}

// -----------------------------------------------------------------------------
uint32_t Codegen::FuncScope::GetVariableLocation(std::string variable) const {
  assert(!"function scope does not have variables");
  return 0;
}

// -----------------------------------------------------------------------------
Codegen::Binding Codegen::BlockScope::Lookup(const std::string &name) const
{
  if (auto it = localVariables_.find(name); it != localVariables_.end()){
    Binding b;
    b.Kind = Binding::Kind::LOCAL_VAR;
    b.Index = it->second;
    return b;
  }
  return parent_->Lookup(name);
}

// -----------------------------------------------------------------------------
void Codegen::BlockScope::AddVariable(const std::string &name, uint32_t loc) 
{

  auto inserted = localVariables_.emplace(name, loc).second;
  assert(inserted && "Varible with that name already exists in scope");
  std::cout << "ADD\n";
}

// -----------------------------------------------------------------------------
int Codegen::BlockScope::GetNumberOfVariables() const {
  std::cout << "X: " << localVariables_.size() << "\n\n" << std::endl;
  return (int)(localVariables_.size());
}


// -----------------------------------------------------------------------------
uint32_t Codegen::BlockScope::GetVariableLocation(std::string variable) const {
  return localVariables_.at(variable);
}


// -----------------------------------------------------------------------------
std::unique_ptr<Program> Codegen::Translate(const Module &mod)
{
  assert(code_.empty() && "expected empty code section");

  // Traverse all the function & function prototype declarations and record
  // them in the global symbol table.
  std::map<std::string, RuntimeFn> protos;
  for (auto item : mod) {
    if (std::holds_alternative<std::shared_ptr<ProtoDecl>>(item)) {
      // The name of the prototype is mapped to the pointer
      // to the function implementing it.
      auto &proto = *std::get<1>(item);
      auto it = kRuntimeFns.find(proto.GetPrimitiveName());
      assert(it != kRuntimeFns.end() && "missing prototype");
      protos.emplace(proto.GetName(), it->second);
    }
    if (std::holds_alternative<std::shared_ptr<FuncDecl>>(item)) {
      // Map the function to a newly created label, which will be used
      // as the address to be invoked by call instructions.
      auto &func = *std::get<0>(item);
      funcs_.emplace(func.GetName(), MakeLabel());
    }
  }

  // Compile all top-level statements in the beginning, to ensure that the
  // instruction at the start of the bytecode stream starts the program.
  GlobalScope global(funcs_, protos);
  for (auto item : mod) {
    if (!std::holds_alternative<std::shared_ptr<Stmt>>(item)) {
      continue;
    }
    LowerStmt(global, *std::get<2>(item));
  }
  Emit<Opcode>(Opcode::STOP);

  // Emit code for all functions.
  for (auto item : mod) {
    if (!std::holds_alternative<std::shared_ptr<FuncDecl>>(item)) {
      continue;
    }
    LowerFuncDecl(global, *std::get<0>(item));
  }

  return std::make_unique<Program>(std::move(code_));
}


// -----------------------------------------------------------------------------
void Codegen::LowerStmt(Scope &scope, const Stmt &stmt)
{
  switch (stmt.GetKind()) {
    case Stmt::Kind::BLOCK: {
      return LowerBlockStmt(scope, static_cast<const BlockStmt &>(stmt));
    }
    case Stmt::Kind::WHILE: {
      return LowerWhileStmt(scope, static_cast<const WhileStmt &>(stmt));
    }
    case Stmt::Kind::IF: {
      return LowerIfStmt(scope, static_cast<const IfStmt &> (stmt));
    }
    case Stmt::Kind::LET: {
      return LowerLetStmt(scope, static_cast<const LetStmt &> (stmt));
    }
    case Stmt::Kind::EXPR: {
      return LowerExprStmt(scope, static_cast<const ExprStmt &>(stmt));
    }
    case Stmt::Kind::RETURN: {
      return LowerReturnStmt(scope, static_cast<const ReturnStmt &>(stmt));
    }
  }
}

// -----------------------------------------------------------------------------
void Codegen::LowerBlockStmt(Scope &scope, const BlockStmt &blockStmt)
{
  unsigned depthIn = depth_;

  BlockScope blockScope(&scope);
  for (auto &stmt : blockStmt) {
    LowerStmt(blockScope, *stmt);
  }


  int number = blockScope.GetNumberOfVariables();
  for(int i = 0; i < number; i++){
    
    EmitPop();
  }

  std::cout<<"depth_: " << depth_ << "   depthIn: " << depthIn << " " << number << " " << std::flush << std::endl; 
  assert(depth_ == depthIn && "mismatched block depth on exit");
}

// -----------------------------------------------------------------------------
void Codegen::LowerWhileStmt(Scope &scope, const WhileStmt &whileStmt)
{
  auto entry = MakeLabel();
  auto exit = MakeLabel();

  EmitLabel(entry);
  LowerExpr(scope, whileStmt.GetCond());
  EmitJumpFalse(exit);
  LowerStmt(scope, whileStmt.GetStmt());
  EmitJump(entry);
  EmitLabel(exit);
}

// -----------------------------------------------------------------------------
void Codegen::LowerIfStmt(Scope &scope, const IfStmt &ifStmt)
{

  auto true_branch = MakeLabel();
  auto false_branch = MakeLabel();
  auto exit = MakeLabel();

  LowerExpr(scope, ifStmt.GetCond());
  EmitJumpFalse(false_branch);
  EmitLabel(true_branch);
  LowerStmt(scope, ifStmt.GetStmt());
  EmitJump(exit);
  EmitLabel(false_branch);
  auto elseStmt = ifStmt.GetElseStmt();
  if(elseStmt){
    LowerStmt(scope, *elseStmt);
  }
  EmitLabel(exit);
}

// -----------------------------------------------------------------------------
void Codegen::LowerLetStmt(Scope &scope, const LetStmt &letStmt)
{
  auto name = letStmt.GetName();
  auto type = letStmt.GetType();
  const auto &initExpr = letStmt.GetInitExpr();
  
  LowerExpr(scope, initExpr);
  int loc = depth_;
  
  scope.AddVariable(name, loc);
}

// -----------------------------------------------------------------------------
void Codegen::LowerReturnStmt(Scope &scope, const ReturnStmt &retStmt)
{
  LowerExpr(scope, retStmt.GetExpr());
  EmitReturn();
}

// -----------------------------------------------------------------------------
void Codegen::LowerExprStmt(Scope &scope, const ExprStmt &exprStmt)
{
  LowerExpr(scope, exprStmt.GetExpr());
  EmitPop();
}

// -----------------------------------------------------------------------------
void Codegen::LowerExpr(const Scope &scope, const Expr &expr)
{
  switch (expr.GetKind()) {
    case Expr::Kind::REF: {
      return LowerRefExpr(scope, static_cast<const RefExpr &>(expr));
    }
    case Expr::Kind::BINARY: {
      return LowerBinaryExpr(scope, static_cast<const BinaryExpr &>(expr));
    }
    case Expr::Kind::CALL: {
      return LowerCallExpr(scope, static_cast<const CallExpr &>(expr));
    }
    case Expr::Kind::INT: {
      return LowerIntExpr(scope, static_cast<const IntExpr &>(expr));
    }
  }
}

// -----------------------------------------------------------------------------
void Codegen::LowerRefExpr(const Scope &scope, const RefExpr &expr)
{
  auto binding = scope.Lookup(expr.GetName());
  switch (binding.Kind) {
    case Binding::Kind::FUNC: {
      EmitPushFunc(binding.Entry);
      return;
    }
    case Binding::Kind::PROTO: {
      EmitPushProto(binding.Fn);
      return;
    }
    case Binding::Kind::ARG: {
      EmitPeek(depth_ + binding.Index + 1);
      return;
    }
    case Binding::Kind::LOCAL_VAR: {
      //emit peek depth_ - smt smt to get where 'a' is 
      EmitPeek(depth_ - scope.GetVariableLocation(expr.GetName()));
    }
  }
}

// -----------------------------------------------------------------------------
void Codegen::LowerIntExpr(const Scope &scope, const IntExpr &expr) 
{
  depth_ += 1;
  Emit<Opcode>(Opcode::PUSH_INT);
  Emit<int64_t>(expr.GetIntValue());
}

// -----------------------------------------------------------------------------
void Codegen::LowerBinaryExpr(const Scope &scope, const BinaryExpr &binary)
{
  LowerExpr(scope, binary.GetLHS());
  LowerExpr(scope, binary.GetRHS());
  switch (binary.GetKind()) {
    case BinaryExpr::Kind::ADD: {
      return EmitAdd();
    }
    case BinaryExpr::Kind::SUB: {
      return EmitSub();
    }
    case BinaryExpr::Kind::MULT: {
      return EmitMult();
    }
    case BinaryExpr::Kind::DIV: {
      return EmitDiv();
    }
    case BinaryExpr::Kind::MOD: {
      return EmitMod();
    }
    case BinaryExpr::Kind::D_EQUAL : {
      return EmitD_Equal();
    }
  }
}

// -----------------------------------------------------------------------------
void Codegen::LowerCallExpr(const Scope &scope, const CallExpr &call)
{
  for (auto it = call.arg_rbegin(), end = call.arg_rend(); it != end; ++it) {
    LowerExpr(scope, **it);
  }
  LowerExpr(scope, call.GetCallee());
  EmitCall(call.arg_size());
  depth_ -= call.arg_size();
}

// -----------------------------------------------------------------------------
void Codegen::LowerFuncDecl(const Scope &scope, const FuncDecl &decl)
{
  // Emit the entry label of the function.
  auto it = funcs_.find(decl.GetName());
  assert(it != funcs_.end() && "missing function label");
  EmitLabel(it->second);

  // Emit the function body.
  func_ = &decl;
  assert(depth_ == 0 && "invalid stack depth in global scope");
  {
    std::map<std::string, uint32_t> args;
    for (auto it = decl.arg_begin(), end = decl.arg_end(); it != end; ++it) {
      args[it->first] = args.size();
    }

    FuncScope fnScope(&scope, args);
    LowerBlockStmt(fnScope, decl.GetBody());
  }

  assert(depth_ == 0 && "invalid stack depth on function exit");
  func_ = nullptr;
}

// -----------------------------------------------------------------------------
Codegen::Label Codegen::MakeLabel()
{
  return Label(++nextLabel_);
}

// -----------------------------------------------------------------------------
template<typename T>
void Codegen::Emit(const T &t)
{
  size_t offset = code_.size();
  code_.resize(offset + sizeof(T));
  memcpy(code_.data() + offset, &t, sizeof(T));
}

// -----------------------------------------------------------------------------
void Codegen::EmitLabel(Label label)
{
  size_t address = code_.size();
  for (auto loc : fixups_[label]) {
    memcpy(code_.data() + loc, &address, sizeof(unsigned));
  }
  labelToAddress_.emplace(label, code_.size());
}

// -----------------------------------------------------------------------------
void Codegen::EmitFixup(Label label)
{
  if (auto it = labelToAddress_.find(label); it != labelToAddress_.end()) {
    Emit<size_t>(it->second);
  } else {
    fixups_[label].push_back(code_.size());
    Emit<size_t>(0);
  }
}

// -----------------------------------------------------------------------------
void Codegen::EmitPop()
{
  assert(depth_ > 0 && "no elements on stack");
  depth_ -= 1;
  Emit<Opcode>(Opcode::POP);
}

// -----------------------------------------------------------------------------
void Codegen::EmitCall(unsigned nargs)
{
  Emit<Opcode>(Opcode::CALL);
}

// -----------------------------------------------------------------------------
void Codegen::EmitPushFunc(Label entry)
{
  depth_ += 1;
  Emit<Opcode>(Opcode::PUSH_FUNC);
  EmitFixup(entry);
}

// -----------------------------------------------------------------------------
void Codegen::EmitPushProto(RuntimeFn fn)
{
  depth_ += 1;
  Emit<Opcode>(Opcode::PUSH_PROTO);
  Emit<RuntimeFn>(fn);
}

// -----------------------------------------------------------------------------
void Codegen::EmitPeek(uint32_t index)
{
  depth_ += 1;
  Emit<Opcode>(Opcode::PEEK);
  Emit<uint32_t>(index);
}

// -----------------------------------------------------------------------------
void Codegen::EmitReturn()
{
  assert(depth_ > 0 && "no elements on stack");
  depth_ -= 1;
  Emit<Opcode>(Opcode::RET);
  Emit<unsigned>(depth_);
  Emit<unsigned>(func_ ? func_->arg_size() : 0);
}

// -----------------------------------------------------------------------------
void Codegen::EmitAdd()
{
  assert(depth_ > 0 && "no elements on stack");
  depth_ -= 1;
  Emit<Opcode>(Opcode::ADD);
}

// -----------------------------------------------------------------------------
void Codegen::EmitSub()
{
  assert(depth_ > 0 && "no elements on stack");
  depth_ -= 1;
  Emit<Opcode>(Opcode::SUB);
}

// -----------------------------------------------------------------------------
void Codegen::EmitMult()
{
  assert(depth_ > 0 && "no elements on stack");
  depth_ -= 1;
  Emit<Opcode>(Opcode::MULT);
}

// -----------------------------------------------------------------------------
void Codegen::EmitDiv()
{
  assert(depth_ > 0 && "no elements on stack");
  depth_ -= 1;
  Emit<Opcode>(Opcode::DIV);
}

// -----------------------------------------------------------------------------
void Codegen::EmitMod()
{
  assert(depth_ > 0 && "no elements on stack");
  depth_ -= 1;
  Emit<Opcode>(Opcode::MOD);
}
// -----------------------------------------------------------------------------
void Codegen::EmitD_Equal()
{
  assert(depth_ > 0 && "no elements on stack");
  depth_ -= 1;
  Emit<Opcode>(Opcode::D_EQUAL);
}

// -----------------------------------------------------------------------------
void Codegen::EmitJumpFalse(Label label)
{
  assert(depth_ > 0 && "no elements on stack");
  depth_ -= 1;
  Emit<Opcode>(Opcode::JUMP_FALSE);
  EmitFixup(label);
}

// -----------------------------------------------------------------------------
void Codegen::EmitJump(Label label)
{
  Emit<Opcode>(Opcode::JUMP);
  EmitFixup(label);
}
