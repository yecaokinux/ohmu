//===- Parser.h ------------------------------------------------*- C++ --*-===//
// Copyright 2014  Google
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//===----------------------------------------------------------------------===//
//
// This file defines a library for constructing LL(k) parsers.  The library is
// based on the idea of "parser combinators", in which larger parsers are
// constructed from smaller ones.
//
//===----------------------------------------------------------------------===//

#ifndef OHMU_PARSER_H
#define OHMU_PARSER_H

#include "ASTNode.h"
#include "Lexer.h"

#include <ostream>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ohmu {

namespace parsing {

enum ParseRuleKind {
  PR_None,
  PR_Token,
  PR_Keyword,
  PR_Sequence,
  PR_Option,
  PR_RecurseLeft,
  PR_Reference,
  PR_NamedDefinition,
  PR_Action
};


class Parser;
class ParseNamedDefinition;


// Base class for parse rules
class ParseRule {
public:
  ParseRule(ParseRuleKind k) : kind_(k) { }
  virtual ~ParseRule() { }

  // Performs parser initialization associated with this rule.
  // tail is true for combinators in a tail-call position.
  virtual bool init(Parser& parser) = 0;

  // Return true if the rule accepts tok as the initial token.
  virtual bool accepts(const Token& tok) = 0;

  // Parse input using the current rule.  Returns the the next rule
  // that should be used to parse input.
  virtual ParseRule* parse(Parser& parser) = 0;

  virtual void prettyPrint(Parser& parser, std::ostream& out) = 0;

  inline ParseRuleKind getKind() const { return kind_; }

private:
  ParseRuleKind kind_;
};


// Matches the empty input.
// This can be used in an option, but it should only appear as the last option.
class ParseNone : public ParseRule {
public:
  ParseNone() : ParseRule(PR_None) { }
  ~ParseNone() { }

  bool       init(Parser& parser) override;
  bool       accepts(const Token& tok) override ;
  ParseRule* parse(Parser& parser) override;
  void       prettyPrint(Parser& parser, std::ostream& out) override;
};


// Matches a single token of input, with a type that is predefined
// by the lexer.  Does not alter parsing state.
class ParseToken : public ParseRule {
public:
  ParseToken(unsigned tid, bool skip=false)
    : ParseRule(PR_Token), tokenID_(tid), skip_(skip)
  { }
  ~ParseToken() { }

  bool       init(Parser& parser) override;
  bool       accepts(const Token& tok) override;
  ParseRule* parse(Parser& parser) override;
  void       prettyPrint(Parser& parser, std::ostream& out) override;

protected:
  ParseToken(ParseRuleKind k, unsigned tid, bool skip)
    : ParseRule(k), tokenID_(tid), skip_(skip)
  { }

  unsigned tokenID_;
  bool skip_;
};


// Matches a single keyword.  The keyword is registered with the lexer
// as a new token at the start of parsing.
class ParseKeyword : public ParseToken {
public:
  ParseKeyword(const std::string& s)
    : ParseToken(PR_Keyword, 0, true), keywordStr_(s)
  { }
  ~ParseKeyword() { }

  bool init(Parser& parser) override;
  void prettyPrint(Parser& parser, std::ostream& out) override;

private:
  const std::string keywordStr_;
};


// Matches a sequence of input.
class ParseSequence : public ParseRule {
public:
  ParseSequence(const std::string& letName,
                ParseRule *first, ParseRule *second)
    : ParseRule(PR_Sequence), letName_(letName),
      first_(first), second_(second)
  { }
  ~ParseSequence() {
    if (first_)  delete first_;
    if (second_) delete second_;
  }

  // Returns true if the head of this sequence has a name.
  inline bool hasLetName() { return letName_.length() > 0; }

  bool       init(Parser& parser) override;
  bool       accepts(const Token& tok) override;
  ParseRule* parse(Parser& parser) override;
  void       prettyPrint(Parser& parser, std::ostream& out) override;

 private:
  std::string letName_;
  ParseRule*  first_;
  ParseRule*  second_;
};


// Distinguishes between two options.
class ParseOption : public ParseRule {
public:
  ParseOption(ParseRule *left, ParseRule *right)
    : ParseRule(PR_Option), left_(left), right_(right)
  { }
  ~ParseOption() {
    if (left_)  delete left_;
    if (right_) delete right_;
  }

  bool       init(Parser& parser) override;
  bool       accepts(const Token& tok) override;
  ParseRule* parse(Parser& parser) override;
  void       prettyPrint(Parser& parser, std::ostream& out) override;

 private:
  ParseRule* left_;
  ParseRule* right_;
};


// Builds a left-recursive parse rule
class ParseRecurseLeft : public ParseRule {
public:
  ParseRecurseLeft(const std::string& letName,
                   ParseRule *base, ParseRule *rest)
    : ParseRule(PR_RecurseLeft), letName_(letName),
      base_(base), rest_(rest)
  { }
  ~ParseRecurseLeft() {
    if (base_) delete base_;
    if (rest_) delete rest_;
  }

  inline bool hasLetName() { return letName_.length() > 0; }

  virtual bool       init(Parser& parser);
  virtual bool       accepts(const Token& tok);
  virtual ParseRule* parse(Parser& parser);
  virtual void       prettyPrint(Parser& parser, std::ostream& out);

private:
  std::string letName_;
  ParseRule*  base_;
  ParseRule*  rest_;
};


// A top-level named definition.
// Named definitions allow mutually recursive rules to be defined.
class ParseNamedDefinition : public ParseRule {
public:
  ParseNamedDefinition(const std::string& name)
    : ParseRule(PR_NamedDefinition), name_(name), rule_(0)
  { }

  bool       init(Parser& parser) override;
  bool       accepts(const Token& tok) override;
  ParseRule* parse(Parser& parser) override;
  void       prettyPrint(Parser& parser, std::ostream& out) override;

  const std::string& name() const { return name_; }
  unsigned numArguments()   const { return argNames_.size(); }

  void addArgument(const std::string& s) { argNames_.push_back(s); }

  void setDefinition(ParseRule* rule) { rule_ = rule; }

private:
  std::string              name_;
  std::vector<std::string> argNames_;
  ParseRule*               rule_;
};


// Refers to another named top-level parse rule.
// Can "call" the named rule by passing arguments.
class ParseReference : public ParseRule {
public:
  ParseReference(ParseNamedDefinition* def)
    : ParseRule(PR_Reference), name_(def->name()), definition_(def),
      frameSize_(0), drop_(0)
  { }
  ParseReference(const std::string& name)
    : ParseRule(PR_Reference), name_(name), definition_(nullptr),
      frameSize_(0), drop_(0)
  { }

  bool       init(Parser& parser) override;
  bool       accepts(const Token& tok) override;
  ParseRule* parse(Parser& parser) override;
  void       prettyPrint(Parser& parser, std::ostream& out) override;

  inline void addArgument(const std::string& arg) {
    argNames_.push_back(arg);
  }
  void addArgumentIdx(unsigned i) {
    arguments_.push_back(i);
  }

 private:
  std::string              name_;
  ParseNamedDefinition*    definition_;
  std::vector<std::string> argNames_;   // argument names
  std::vector<unsigned>    arguments_;  // stack indices of arguments
  unsigned                 frameSize_;  // size of the stack frame
  unsigned                 drop_;       // num items to drop from the stack
};


// Constructs an expression in the target language.
// The ASTNode is interpreted to create the expression.
// Variables in the ASTNode refer to named results on the parser stack.
class ParseAction : public ParseRule {
public:
  ParseAction(ast::ASTNode *n)
    : ParseRule(PR_Action), node_(n), drop_(0)
  { }

  bool       init(Parser& parser) override;
  bool       accepts(const Token& tok) override;
  ParseRule* parse(Parser& parser) override;
  void       prettyPrint(Parser& parser, std::ostream& out) override;

private:
  ast::ASTNode* node_;    // ASTNode to interpret
  unsigned frameSize_;    // size of the stack frame
  unsigned drop_;         // num items to drop from the stack.
};


// The result of parsing a rule is a ParseResult, which consists of:
// (1) The id of the rule that created the result (if any).
// (2) One of the following:
//   (a) A token string, for simple tokens.
//   (b) A unique pointer to a user-defined AST Node type.
//   (c) A unique list (std::vector) of (b)
class ParseResult {
public:
  typedef std::vector<void*> ListType;

  enum ResultKind {
    PRS_None = 0,
    PRS_TokenStr = 1,
    PRS_UserDefined = 2    // user-defined AST node type
  };

  ParseResult()
      : ruleID_(0), resultKind_(PRS_None), isList_(false), result_(nullptr)
  { }
  explicit ParseResult(const Token& tok)
      : ruleID_(0), resultKind_(PRS_TokenStr), isList_(false),
        result_(tok.c_str(), tok.length())
  { }
  // A particular parser may have several different kinds of AST node;
  // kind distingushes between them.
  ParseResult(unsigned short kind, void* node)
      : ruleID_(0), resultKind_(kind), isList_(false), result_(node) {
    assert(kind >= PRS_UserDefined && "Invalid kind");
  }
  // Create a list of AST Nodes; kind specifies the kind of nodes in the list.
  ParseResult(unsigned short kind, ListType *pl)
      : ruleID_(0), resultKind_(kind), isList_(true), result_(pl) {
    assert(kind >= PRS_UserDefined && "Invalid kind");
  }
  ~ParseResult() {
    // All ParseResults must be used.
    assert(resultKind_ == PRS_None && "Unused ParseResult.");
  }

  ParseResult(ParseResult &&r)
      : ruleID_(r.ruleID_), resultKind_(r.resultKind_), isList_(r.isList_),
        result_(r.result_) {
    r.release();
  }

  void operator=(ParseResult &&r) {
    ruleID_ = r.ruleID_;
    resultKind_ = r.resultKind_;
    isList_ = r.isList_;
    result_ = r.result_;
    r.release();
  }

  unsigned short kind() const { return resultKind_; }

  bool empty() const { return resultKind_ == PRS_None; }

  unsigned ruleID() const { return ruleID_; }
  void setRuleID(unsigned id) { ruleID_ = id; }

  inline bool isUnique() const { return resultKind_ >= PRS_UserDefined; }

  StringRef tokenStr() {
    assert(resultKind_ == PRS_TokenStr);
    return result_.string();
  }

  // Return the node, and release ownership.
  void* getNode() {
    assert(resultKind_ >= PRS_UserDefined && isList_ == false);
    void *p = result_.ptr_;
    release();
    return p;
  }

  // Return the node list, and release ownership.
  ListType* getNodeList() {
    assert(resultKind_ >= PRS_UserDefined && isList_ == false);
    ListType* pl = result_.list();
    release();
    return pl;
  }

private:
  ParseResult(const ParseResult& r) = delete;
  void operator=(const ParseResult &f) = delete;

  inline void release() {  // release ownership of any data
    resultKind_ = PRS_None;
    isList_ = false;
    result_.ptr_ = nullptr;
  }

  struct ResultVal {
  public:
    void* ptr_;
    unsigned len_;

    explicit ResultVal(void* p) : ptr_(p), len_(0) { }
    ResultVal(const char *s, unsigned l)
        : ptr_(const_cast<char*>(s)), len_(l)
    { }

  public:
    StringRef string() {
      return StringRef(reinterpret_cast<const char*>(ptr_), len_);
    }
    ListType* list() {
      return reinterpret_cast<ListType*>(ptr_);
    }
  };

  unsigned       ruleID_;
  unsigned short resultKind_;
  bool           isList_;
  ResultVal      result_;
};



// The result stack maintains a stack of ParseResults.
// It functions much like a program stack.
class ResultStack {
public:
  ResultStack() : stack_(0) { }

  unsigned size() const { return stack_.size(); }

  // move the argument at index i onto the top of the stack.
  void moveAndPush(unsigned i) {
    stack_.emplace_back(std::move((*this)[i]));
  }

  void push_back(const Token& tok) {
    stack_.emplace_back(ParseResult(tok));
  }

  void push_back(ParseResult &&r) {
    stack_.emplace_back(std::move(r));
  }

  // Drop n items from the stack, but keep the nsave top-most items.
  void drop(unsigned n, unsigned nsave) {
    if (n == 0)
      return;
    assert(stack_.size() >= n + nsave && "Stack too small");
    stack_.erase(stack_.end()-nsave-n, stack_.end()-nsave);
  }

  ParseResult& operator[](unsigned i) {
    assert(i < stack_.size() && "Array index out of bounds.");
    return stack_[i];
  }

  ParseResult& back() { return stack_.back(); }

  void clear() {
    stack_.clear();
  }

private:
  std::vector<ParseResult> stack_;
};



// The abstract stack is used during initialization and validation.
// It mimics the behavior of ResultStack, but but holds the names of the
// results that will be produced during parsing.
// The abstract stack is used to validate the parser, and compute frame sizes
// and indices for named arguments.
class AbstractStack {
public:
  static const int InvalidIndex = 0xFFFF;
  typedef std::pair<unsigned, unsigned> FrameState;

  AbstractStack() : blockStart_(0) { }

  // Find the stack index for name s on the abstract stack.
  // Indices are computed with respect to the current frame.
  unsigned getIndex(const std::string &s) {
    for (unsigned i=0, n=stack_.size(); i<n; ++i) {
      if (*stack_[i] == s) return i;
    }
    return InvalidIndex;  // failure.
  }

  // Return the size of the current stack frame.
  // (I.e. the size of the stack for the current named, top-level rule.)
  unsigned size() {  return stack_.size(); }

  // Return the size of the stack for the local block.
  unsigned localSize() { return stack_.size() - blockStart_; }

  // Rewind the stack to the given local size.
  void rewind(unsigned lsize) {
    for (unsigned n = localSize(); n > lsize; --n)
      stack_.pop_back();
  }

  // Enter a new local block (i.e. new subrule)
  unsigned enterLocalBlock() {
    unsigned bs = blockStart_;
    blockStart_ = stack_.size();
    return bs;
  }

  // Exit the current local block.
  void exitLocalBlock(unsigned bs) {
    assert(bs <= stack_.size());
    blockStart_ = bs;
  }

  // get the ith value on the stack, starting from the current frame.
  std::string*& operator[](unsigned i) { return stack_[i]; }

  // push a new name onto the stack.
  void push_back(std::string *s) { stack_.push_back(s); }

  // pop a name off of the stack.
  void pop_back() {
    assert(localSize() > 0);
    stack_.pop_back();
  }

  // clear the stack
  void clear() {
    blockStart_ = 0;
    stack_.clear();
  }

private:
  unsigned blockStart_;
  std::vector<std::string*> stack_;
};



class Parser {
public:
  // Create a new parser.
  Parser(Lexer* lexer)
    : lexer_(lexer), parseError_(false), trace_(false), traceValidate_(false)
  { }

  // Override this to construct an expression in the target language.
  virtual ParseResult makeExpr(unsigned op, unsigned arity, ParseResult *prs) = 0;

  // Override this to look up the opcode for a string.
  virtual unsigned getLanguageOpcode(const std::string &s) = 0;

  // Initialize the parser, with rule as the starting point.
  bool init();

  // Parse rule and return the result.
  ParseResult parse(ParseNamedDefinition* start);

  // Add a new top-level named definition.
  void addDefinition(ParseNamedDefinition* def) {
    definitions_.push_back(def);
    const std::string& s = def->name();
    if (s.length() > 0)
      definitionDict_[s] = def;
  }

  // Find a top-level definition by name.
  ParseNamedDefinition* findDefinition(const std::string& s) {
    DefinitionDict::iterator it = definitionDict_.find(s);
    if (it == definitionDict_.end())
      return 0;
    else return it->second;
  }

  unsigned registerKeyword(const std::string& s) {
    return lexer_->registerKeyword(s);
  }

  const char* getTokenIDString(unsigned tid) {
    return lexer_->getTokenIDString(tid);
  }

  unsigned lookupTokenID(const std::string& s) {
    return lexer_->lookupTokenID(s);
  }

  void printSyntax(std::ostream& out);

  void setTrace(bool b)         { trace_ = b; }
  void setTraceValidate(bool b) { trace_ = b; }

protected:
  typedef std::vector<ParseNamedDefinition*>           DefinitionVect;
  typedef std::map<std::string, ParseNamedDefinition*> DefinitionDict;

  friend class ParseRule;
  friend class ParseNone;
  friend class ParseToken;
  friend class ParseKeyword;
  friend class ParseSequence;
  friend class ParseOption;
  friend class ParseRecurseLeft;
  friend class ParseReference;
  friend class ParseNamedDefinition;
  friend class ParseAction;
  friend class ASTIndexVisitor;
  friend class ASTInterpretReducer;

  // Initialize rule p.  This is used internally to make recursive calls.
  inline bool initRule(ParseRule* p);

  // Parse rule p.  This is invoked internally to make recursive calls.
  inline void parseRule(ParseRule *p);

  // look at the next token
  const Token& look(unsigned i = 0) {
    return lexer_->look(i);
  }

  // consume next token from lexer, and discard it
  void skip() {
    lexer_->consume();
  }

  // consume next token from lexer, and push it onto the stack
  void consume() {
    resultStack_.push_back(look());
    lexer_->consume();
  }

  // output a parser validation error.
  std::ostream& validationError();

  // output a parser syntax error.
  std::ostream& parseError(const SourceLocation& sloc);

private:
  Lexer*          lexer_;
  DefinitionVect  definitions_;
  DefinitionDict  definitionDict_;

  ResultStack     resultStack_;
  AbstractStack   abstractStack_;
  bool            parseError_;

  bool trace_;
  bool traceValidate_;
};


} // end namespace parser

} // end namespace ohmu

#endif  // OHMU_PARSER_H