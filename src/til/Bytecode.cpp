//===- Serialize.cpp -------------------------------------------*- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License.  See LICENSE.TXT in the LLVM repository for details.
//
//===----------------------------------------------------------------------===//


#include "Bytecode.h"

namespace ohmu {
namespace til {


void ByteStreamWriterBase::flush() {
  if (Pos > 0)
    writeData(Buffer.data(), Pos);
  Pos = 0;
}


void ByteStreamReaderBase::refill() {
  if (Eof)
    return;

  if (Pos > 0) {  // Move remaining contents to start of buffer.
    assert(Pos > length() && "Cannot refill a nearly full buffer.");

    int len = length();
    if (len > 0)
      memcpy(Buffer.data(), Buffer.data() + Pos, len);
    Pos = 0;
    BufferLen = len;
  }

  int read = readData(Buffer.data() + BufferLen, BufferSize - BufferLen);
  BufferLen += read;
  if (BufferLen < BufferSize)
    Eof = true;
}


void ByteStreamWriterBase::endAtom() {
  if (length() <= BytecodeBase::MaxAtomSize)
    flush();
}


void ByteStreamReaderBase::endAtom() {
  if (length() <= BytecodeBase::MaxAtomSize)
    refill();
}


void ByteStreamWriterBase::writeBytes(const void *Data, int64_t Size) {
  if (Size >= (BufferSize >> 1)) {   // Don't buffer large writes.
    flush();                  // Flush current data to disk.
    writeData(Data, Size);    // Directly write the bytes to disk.
    return;
  }
  // Flush buffer if the write would fill it up.
  if (length() - Size <= BytecodeBase::MaxAtomSize)
    flush();

  memcpy(Buffer.data() + Pos, Data, Size);
  Pos += Size;
  // Size < BufferSize/2, so we have at least half the buffer left.
}


void ByteStreamReaderBase::readBytes(void *Data, int64_t Size) {
  int len = length();
  if (Size > len) {
    memcpy(Data, Buffer.data() + Pos, len);   // Copy out current buffer.
    Size = Size - len;
    Data = reinterpret_cast<char*>(Data) + len;

    if (Size >= (BufferSize >> 1)) {   // Don't buffer large reads.
      if (Eof) {
        Error = true;
        return;
      }
      int64_t L = readData(Data, Size);   // Read more data.
      if (L < Size)
        Eof = true;
      refill();                        // Refill buffer
      return;
    }

    refill();
    if (Size > length()) {
      Error = true;
      return;
    }
  }

  // Size < length() at this point.
  memcpy(Data, Buffer.data() + Pos, Size);
  Pos += Size;
  if (length() < BytecodeBase::MaxAtomSize)
    refill();
}


void ByteStreamWriterBase::writeBits32(uint32_t V, int Nbits) {
  while (Nbits > 0) {
    Buffer[Pos++] = V & 0xFF;
    V = V >> 8;
    Nbits -= 8;
  }
}


void ByteStreamWriterBase::writeBits64(uint64_t V, int Nbits) {
  while (Nbits > 0) {
    Buffer[Pos++] = V & 0xFF;
    V = V >> 8;
    Nbits -= 8;
  }
}


uint32_t ByteStreamReaderBase::readBits32(int Nbits) {
  assert(Nbits <= 32 && "Invalid number of bits.");
  uint32_t V = 0;
  int B = 0;
  while (true) {
    uint32_t Byt = Buffer[Pos++];
    V = V | (Byt << B);
    B += 8;
    if (B >= Nbits)
      break;
  }
  return V;
}


uint64_t ByteStreamReaderBase::readBits64(int Nbits) {
  assert(Nbits <= 64 && "Invalid number of bits.");
  uint64_t V = 0;
  int B = 0;
  while (true) {
    uint64_t Byt = Buffer[Pos++];
    V = V | (Byt << B);
    B += 8;
    if (B >= Nbits)
      break;
  }
  return V;
}


void ByteStreamWriterBase::writeUInt32_Vbr(uint32_t V) {
  if (V == 0) {
    Buffer[Pos++] = 0;
    return;
  }
  while (V > 0) {
    uint32_t V2 = V >> 7;
    uint8_t Hibit = (V2 == 0) ? 0 : 0x80;
    // Write lower 7 bits.  The 8th bit is high if there's more to write.
    Buffer[Pos++] = static_cast<uint8_t>((V & 0x7F) | Hibit);
    V = V2;
  }
}


void ByteStreamWriterBase::writeUInt64_Vbr(uint64_t V) {
  if (V == 0) {
    Buffer[Pos++] = 0;
    return;
  }
  while (V > 0) {
    uint64_t V2 = V >> 7;
    uint8_t Hibit = (V2 == 0) ? 0 : 0x80;
    // Write lower 7 bits.  The 8th bit is high if there's more to write.
    Buffer[Pos++] = static_cast<uint8_t>((V & 0x7Fu) | Hibit);
    V = V2;
  }
}


uint32_t ByteStreamReaderBase::readUInt32_Vbr() {
  uint32_t V = 0;
  for (unsigned B = 0; B < 32; B += 7) {
    uint32_t Byt = Buffer[Pos++];
    V = V | ((Byt & 0x7Fu) << B);
    if ((Byt & 0x80) == 0)
      break;
  }
  return V;
}


uint64_t ByteStreamReaderBase::readUInt64_Vbr() {
  uint64_t V = 0;
  for (unsigned B = 0; B < 64; B += 7) {
    uint64_t Byt = Buffer[Pos++];
    V = V | ((Byt & 0x7Fu) << B);
    if ((Byt & 0x80) == 0)
      break;
  }
  return V;
}


void ByteStreamWriterBase::writeFloat(float f) {
  // TODO: works only on machines which use in-memory IEEE format.
  union { float Fnum; uint32_t Inum; } U;
  U.Fnum = f;
  writeUInt32(U.Inum);
}


void ByteStreamWriterBase::writeDouble(double d) {
  // TODO: works only on machines which use in-memory IEEE format.
  union { double Fnum; uint64_t Inum; } U;
  U.Fnum = d;
  writeUInt64(U.Inum);
}


float ByteStreamReaderBase::readFloat() {
  // TODO: works only on machines which use in-memory IEEE format.
  union { float Fnum; uint32_t Inum; } U;
  U.Inum = readUInt32();
  return U.Fnum;
}


double ByteStreamReaderBase::readDouble() {
  // TODO: works only on machines which use in-memory IEEE format.
  union { double Fnum; uint64_t Inum; } U;
  U.Inum = readUInt64();
  return U.Fnum;
}


void ByteStreamWriterBase::writeString(StringRef S) {
  writeUInt32(S.size());
  writeBytes(S.data(), S.size());
}


StringRef ByteStreamReaderBase::readString() {
  uint32_t Sz = readUInt32();
  char* S = allocStringData(Sz);
  if (!S) {
    Error = true;
    return StringRef(nullptr, 0);
  }
  readBytes(S, Sz);
  return StringRef(S, Sz);
}


/** BytecodeWriter and BytecodeReader **/

VarDecl *BytecodeReader::getVarDecl(unsigned Vidx) {
  if (Vidx >= Vars.size()) {
    fail("Invalid variable ID.");
    return nullptr;
  }
  return Vars[Vidx];
}


BasicBlock *BytecodeReader::getBlock(int Bid, int Nargs) {
  if (Bid == BasicBlock::InvalidBlockID)
    return nullptr;

  if (Bid >= static_cast<int>(Blocks.size())) {
    fail("Invalid block ID.");
    return nullptr;
  }

  auto *Bb = Blocks[Bid];
  if (!Bb) {
    Bb = Builder.newBlock(Nargs);
    Blocks[Bid] = Bb;
  }
  else if (Bb->numArguments() != Nargs) {
    fail("Block has wrong number of arguments.");
  }
  return Bb;
}



void BytecodeWriter::enterScope(VarDecl *Vd) {
  writePseudoOpcode(PSOP_EnterScope);
}

void BytecodeWriter::exitScope(VarDecl *Vd) {
  writePseudoOpcode(PSOP_ExitScope);
}

void BytecodeReader::enterScope() {
  auto *Vd = dyn_cast<VarDecl>(arg(0));
  if (!Vd || Vars.size() != Vd->varIndex()) {
    fail("Invalid variable declaration.");
    return;
  }
  Vars.push_back(Vd);
}

void BytecodeReader::exitScope() {
  Vars.pop_back();
}


void BytecodeWriter::enterBlock(BasicBlock *B) {
  writePseudoOpcode(PSOP_EnterBlock);
  Writer->writeUInt32(B->blockID());
  Writer->writeUInt32(B->firstInstrID());
  Writer->writeUInt32(B->numArguments());
}

void BytecodeReader::enterBlock() {
  assert(Stack.size() == CFGStackSize && "Internal error.");

  unsigned Bid = Reader->readUInt32();
  CurrentInstrID = Reader->readUInt32();
  unsigned Nargs = Reader->readUInt32();
  Builder.beginBlock( getBlock(Bid, Nargs) );

  // Register phi nodes in Instrs array.
  auto *Bb = Builder.currentBB();
  for (unsigned i = 0, n = Bb->numArguments(); i < n; ++i)
    Instrs[CurrentInstrID++] = Bb->arguments()[i];

  // Reset current argument
  CurrentArg = 0;
}


void BytecodeWriter::enterCFG(SCFG *Cfg) {
  writePseudoOpcode(PSOP_EnterCFG);
  Writer->writeUInt32(Cfg->numBlocks());
  Writer->writeUInt32(Cfg->numInstructions());
  Writer->writeUInt32(Cfg->entry()->blockID());
  Writer->writeUInt32(Cfg->exit()->blockID());
}

void BytecodeReader::enterCFG() {
  unsigned Nb  = Reader->readUInt32();
  unsigned Ni  = Reader->readUInt32();
  unsigned Eid = Reader->readUInt32();
  unsigned Xid = Reader->readUInt32();
  Builder.beginCFG(nullptr);
  Blocks.resize(Nb, nullptr);
  Instrs.resize(Ni, nullptr);
  Blocks[Eid] = Builder.currentCFG()->entry();
  Blocks[Xid] = Builder.currentCFG()->exit();
  CFGStackSize = Stack.size();
}


void BytecodeWriter::reduceBasicBlock(BasicBlock *E) {
  writeOpcode(COP_BasicBlock);
}


void BytecodeReader::readBasicBlock() {
  assert(Stack.size() == CFGStackSize && "Internal error.");

  if (Builder.currentBB())
    Builder.endBlock(nullptr);
}


void BytecodeWriter::reduceSCFG(SCFG *E) {
  writeOpcode(COP_SCFG);
}


void BytecodeReader::readSCFG() {
  assert(Stack.size() == CFGStackSize && "Internal error.");
  CFGStackSize = 0;

  auto *Cfg = Builder.currentCFG();

  // We added the blocks in an arbitrary order, so renumber blocks to match
  // the original.
  if (Cfg->numBlocks() != Blocks.size()) {
    fail("Failed to read all blocks.");
    return;
  }

  for (int i = 0, n = Blocks.size(); i < n; ++i) {
    auto* B = Blocks[i];
    Cfg->blocks()[i].reset(B);
    B->setBlockID(i);
  }

  Builder.endCFG();
  Blocks.clear();
  Instrs.clear();
  push(Cfg);
}



void BytecodeWriter::reduceNull() {
  writePseudoOpcode(PSOP_Null);
}

void BytecodeReader::readNull() {
  push(nullptr);
}


void BytecodeWriter::reduceWeak(Instruction *I) {
  writePseudoOpcode(PSOP_WeakInstrRef);
  Writer->writeUInt32(I->instrID());
}

void BytecodeReader::readWeak() {
  unsigned i = Reader->readUInt32();
  if (i >= Instrs.size()) {
    fail("Invalid instruction ID.");
    return;
  }
  push(Instrs[i]);
}


void BytecodeWriter::reduceBBArgument(Phi *E) {
  writePseudoOpcode(PSOP_BBArgument);
}

void BytecodeReader::readBBArgument() {
  ++CurrentArg;
  drop(1);        // Arguments should already have been added.
}


void BytecodeWriter::reduceBBInstruction(Instruction *E) {
  writePseudoOpcode(PSOP_BBInstruction);
}

void BytecodeReader::readBBInstruction() {
  if (Stack.size() <= CFGStackSize) {
    fail("Internal error: corrupted stack.");
    return;
  }
  Instruction *I = dyn_cast_or_null<Instruction>(arg(0));
  if (arg(0) && !I) {
    fail("Expected instruction.");
    return;
  }
  Instrs[CurrentInstrID++] = I;
  drop(1);
}


void BytecodeWriter::reduceVarDecl(VarDecl* E) {
  writeOpcode(COP_VarDecl);
  writeFlag(E->kind());
  Writer->writeUInt32(E->varIndex());
  Writer->writeString(E->varName());
}

void BytecodeReader::readVarDecl() {
  auto K = readFlag<VarDecl::VariableKind>();
  unsigned Id = Reader->readUInt32();
  StringRef Nm = Reader->readString();
  auto *E = Builder.newVarDecl(K, Nm, arg(0));  // TODO: enter Scope?
  E->setVarIndex(Id);
  drop(1);
  push(E);
}


void BytecodeWriter::reduceFunction(Function *E) {
  writeOpcode(COP_Function);
}

void BytecodeReader::readFunction() {
  auto *E = Builder.newFunction(dyn_cast<VarDecl>(arg(1)), arg(0));
  drop(2);
  push(E);
}


void BytecodeWriter::reduceCode(Code *E) {
  writeOpcode(COP_Code);
  writeFlag(E->callingConvention());
}

void BytecodeReader::readCode() {
  auto Cc = readFlag<Code::CallingConvention>();
  auto *E = Builder.newCode(arg(1), arg(0));
  E->setCallingConvention(Cc);
  drop(2);
  push(E);
}


void BytecodeWriter::reduceField(Field *E) {
  writeOpcode(COP_Field);
}

void BytecodeReader::readField() {
  auto *E = Builder.newField(arg(1), arg(0));
  drop(2);
  push(E);
}


void BytecodeWriter::reduceSlot(Slot *E) {
  writeOpcode(COP_Slot);
  Writer->writeUInt16(E->modifiers());
  Writer->writeString(E->slotName());
}

void BytecodeReader::readSlot() {
  uint16_t Mods = Reader->readUInt16();
  StringRef S = Reader->readString();
  auto *E = Builder.newSlot(S, arg(0));
  E->setModifiers(Mods);
  drop(1);
  push(E);
}


void BytecodeWriter::reduceRecord(Record *E) {
  writeOpcode(COP_Record);
  Writer->writeUInt32(E->slots().size());
}

void BytecodeReader::readRecord() {
  unsigned Ns = Reader->readUInt32();
  auto *E = Builder.newRecord(Ns, arg(Ns));
  for (int i = Ns-1; i >= 0; --i) {
    auto *Slt = dyn_cast<Slot>(arg(i));
    E->addSlot(Builder.arena(), Slt);
  }
  drop(Ns+1);
  push(E);
}


void BytecodeWriter::reduceArray(Array *E) {
  writeOpcode(COP_Array);
  Writer->writeUInt64(E->numElements());
}

void BytecodeReader::readArray() {
  uint64_t Ne = Reader->readUInt64();
  Array *E;
  if (Ne == 0) {
    E = Builder.newArray(arg(1), arg(0));
  }
  else {
    E = Builder.newArray(arg(Ne + 1), Ne);
    for (uint64_t i = 0; i < Ne; ++i) {
      E->elements()[i].reset(arg(Ne-1 - i));
    }
  }
  drop(Ne + 2);
  push(E);
}


void BytecodeWriter::reduceScalarType(ScalarType *E) {
  writeOpcode(COP_ScalarType);
  writeBaseType(E->baseType());
}

void BytecodeReader::readScalarType() {
  BaseType Bt = readBaseType();
  auto *E = Builder.newScalarType(Bt);
  push(E);
}


void BytecodeWriter::reduceLiteral(Literal *E) {
  writeOpcode(COP_Literal);
  writeBaseType(BaseType::getBaseType<void>());
}

template<class T>
class BytecodeReader::ReadLiteralFun {
public:
  typedef bool ReturnType;

  static bool defaultAction(BytecodeReader *R) {
    auto *E = R->Builder.newLiteralVoid();
    R->push(E);
    return false;
  }

  static bool action(BytecodeReader* R) {
    auto *E = R->Builder.newLiteralT<T>(
        R->readLitVal(static_cast<T*>(nullptr)) );
    R->push(E);
    return true;
  }
};

void BytecodeReader::readLiteral() {
  BaseType Bt = readBaseType();
  BtBr<ReadLiteralFun>::branch(Bt, this);
}


void BytecodeWriter::reduceVariable(Variable *E) {
  writeOpcode(COP_Variable);
  Writer->writeUInt32(E->variableDecl()->varIndex());
}

void BytecodeReader::readVariable() {
  unsigned Vidx = Reader->readUInt32();
  auto *E = Builder.newVariable( getVarDecl(Vidx) );
  push(E);
}


void BytecodeWriter::reduceApply(Apply *E) {
  writeOpcode(COP_Apply);
  writeFlag(E->applyKind());
}

void BytecodeReader::readApply() {
  auto Ak = readFlag<Apply::ApplyKind>();
  auto *E = Builder.newApply(arg(1), arg(0), Ak);
  drop(2);
  push(E);
}


void BytecodeWriter::reduceProject(Project *E) {
  writeOpcode(COP_Project);
  Writer->writeString(E->slotName());
}

void BytecodeReader::readProject() {
  StringRef Nm = Reader->readString();
  auto *E = Builder.newProject(arg(0), Nm);
  drop(1);
  push(E);
}


void BytecodeWriter::reduceCall(Call *E) {
  writeOpcode(COP_Call);
  writeBaseType(E->baseType());
}

void BytecodeReader::readCall() {
  auto Bt = readBaseType();
  auto *E = Builder.newCall(arg(0));
  E->setBaseType(Bt);
  drop(1);
  push(E);
}


void BytecodeWriter::reduceAlloc(Alloc *E) {
  writeOpcode(COP_Alloc);
  writeFlag(E->allocKind());
}

void BytecodeReader::readAlloc() {
  auto Ak = readFlag<Alloc::AllocKind>();
  auto *E = Builder.newAlloc(arg(0), Ak);
  drop(1);
  push(E);
}


void BytecodeWriter::reduceLoad(Load *E) {
  writeOpcode(COP_Load);
  writeBaseType(E->baseType());
}

void BytecodeReader::readLoad() {
  auto Bt = readBaseType();
  auto *E = Builder.newLoad(arg(0));
  E->setBaseType(Bt);
  drop(1);
  push(E);
}


void BytecodeWriter::reduceStore(Store *E) {
  writeOpcode(COP_Store);
}

void BytecodeReader::readStore() {
  auto *E = Builder.newStore(arg(1), arg(0));
  drop(2);
  push(E);
}


void BytecodeWriter::reduceArrayIndex(ArrayIndex *E) {
  writeOpcode(COP_ArrayIndex);
}

void BytecodeReader::readArrayIndex() {
  auto *E = Builder.newArrayIndex(arg(1), arg(0));
  drop(2);
  push(E);
}


void BytecodeWriter::reduceArrayAdd(ArrayAdd *E) {
  writeOpcode(COP_ArrayAdd);
}

void BytecodeReader::readArrayAdd() {
  auto *E = Builder.newArrayAdd(arg(1), arg(0));
  drop(2);
  push(E);
}


void BytecodeWriter::reduceUnaryOp(UnaryOp *E) {
  writeOpcode(COP_UnaryOp);
  writeFlag(E->unaryOpcode());
  writeBaseType(E->baseType());
}

void BytecodeReader::readUnaryOp() {
  auto Uop = readFlag<TIL_UnaryOpcode>();
  auto Bt  = readBaseType();
  auto *E = Builder.newUnaryOp(Uop, arg(0));
  E->setBaseType(Bt);
  drop(1);
  push(E);
}


void BytecodeWriter::reduceBinaryOp(BinaryOp *E) {
  writeOpcode(COP_BinaryOp);
  writeFlag(E->binaryOpcode());
  writeBaseType(E->baseType());
}

void BytecodeReader::readBinaryOp() {
  auto Bop = readFlag<TIL_BinaryOpcode>();
  auto Bt  = readBaseType();
  auto *E = Builder.newBinaryOp(Bop, arg(1), arg(0));
  E->setBaseType(Bt);
  drop(2);
  push(E);
}


void BytecodeWriter::reduceCast(Cast *E) {
  writeOpcode(COP_Cast);
  writeFlag(E->castOpcode());
  writeBaseType(E->baseType());
}

void BytecodeReader::readCast() {
  auto Cop = readFlag<TIL_CastOpcode>();
  auto Bt  = readBaseType();
  auto *E = Builder.newCast(Cop, arg(0));
  E->setBaseType(Bt);
  drop(1);
  push(E);
}


void BytecodeWriter::reducePhi(Phi *E) {
  writeOpcode(COP_Phi);
}

void BytecodeReader::readPhi() {
  if (Builder.currentBB() && CurrentArg < Builder.currentBB()->numArguments())
    // Grab the current argument, which was previously created.
    // See also readBBArgument().
    push(Builder.currentBB()->arguments()[CurrentArg]);
  else
    // This should never happen -- all Phi nodes should be arguments.
    push(Builder.newPhi(0,false));
}


void BytecodeWriter::reduceGoto(Goto *E) {
  writeOpcode(COP_Goto);
  Writer->writeUInt32(E->targetBlock()->numArguments());
  Writer->writeUInt32(E->targetBlock()->blockID());
}

void BytecodeReader::readGoto() {
  unsigned Nargs = Reader->readUInt32();
  unsigned Bid   = Reader->readUInt32();
  BasicBlock *Bb = getBlock(Bid, Nargs);
  Builder.newGoto(Bb, lastArgs(Nargs));
  drop(Nargs);
  // No need to push terminator
}


void BytecodeWriter::reduceBranch(Branch *E) {
  writeOpcode(COP_Branch);
  unsigned Id1 = E->thenBlock() ?
      E->thenBlock()->blockID() : BasicBlock::InvalidBlockID;
  unsigned Id2 = E->elseBlock() ?
      E->elseBlock()->blockID() : BasicBlock::InvalidBlockID;
  Writer->writeUInt32(Id1);
  Writer->writeUInt32(Id2);
}

void BytecodeReader::readBranch() {
  unsigned ThenBid = Reader->readUInt32();
  unsigned ElseBid = Reader->readUInt32();
  BasicBlock *Bbt = getBlock(ThenBid, 0);
  BasicBlock *Bbe = getBlock(ElseBid, 0);
  Builder.newBranch(arg(0), Bbt, Bbe);
  drop(1);
  // No need to push terminator
}


void BytecodeWriter::reduceSwitch(Switch *E) {
  writeOpcode(COP_Switch);
  int Nc = E->numCases();
  Writer->writeUInt32(Nc);
  for (int i = 0; i < Nc; ++i) {
    auto* Cb = E->caseBlock(i);
    Writer->writeUInt32( Cb ? Cb->blockID() : BasicBlock::InvalidBlockID );
  }
}

void BytecodeReader::readSwitch() {
  int Nc = Reader->readUInt32();
  Switch* E = Builder.newSwitch(arg(Nc), Nc);
  for (int i = 0; i < Nc; ++i) {
    unsigned Bid = Reader->readUInt32();
    BasicBlock* Bb = getBlock(Bid, 0);
    Builder.addSwitchCase(E, arg(Nc-1-i), Bb);
  }
  drop(Nc+1);
  // No need to push terminator.
}


void BytecodeWriter::reduceReturn(Return *E) {
  writeOpcode(COP_Return);
}

void BytecodeReader::readReturn() {
  Builder.newReturn(arg(0));
  drop(1);
  // No need to push terminator
}


void BytecodeWriter::reduceUndefined(Undefined *E) {
  writeOpcode(COP_Undefined);
}

void BytecodeReader::readUndefined() {
  auto *E = Builder.newUndefined();
  push(E);
}


void BytecodeWriter::reduceWildcard(Wildcard *E) {
  writeOpcode(COP_Wildcard);
}

void BytecodeReader::readWildcard() {
  auto *E = Builder.newWildcard();
  push(E);
}


void BytecodeWriter::reduceIdentifier(Identifier *E) {
  writeOpcode(COP_Identifier);
  Writer->writeString(E->idString());
}

void BytecodeReader::readIdentifier() {
  StringRef S = Reader->readString();
  auto *E = Builder.newIdentifier(S);
  push(E);
}


void BytecodeWriter::reduceLet(Let *E) {
  writeOpcode(COP_Let);
}

void BytecodeReader::readLet() {
  auto* Vd = dyn_cast<VarDecl>(arg(1));
  auto *E = Builder.newLet(Vd, arg(0));
  drop(2);
  push(E);
}


void BytecodeWriter::reduceIfThenElse(IfThenElse *E) {
  writeOpcode(COP_IfThenElse);
}

void BytecodeReader::readIfThenElse() {
  auto *E = Builder.newIfThenElse(arg(2), arg(1), arg(0));
  drop(3);
  push(E);
}



void BytecodeReader::readSExpr() {
  auto Psop = readPseudoOpcode();
  switch (Psop) {
    case PSOP_Null:          readNull();          break;
    case PSOP_WeakInstrRef:  readWeak();          break;
    case PSOP_BBArgument:    readBBArgument();    break;
    case PSOP_BBInstruction: readBBInstruction(); break;
    case PSOP_EnterScope:    enterScope();        break;
    case PSOP_ExitScope:     exitScope();         break;
    case PSOP_EnterBlock:    enterBlock();        break;
    case PSOP_EnterCFG:      enterCFG();          break;
    case PSOP_Annotation:    readAnnotation();    break;
    default:
      readSExprByType(getOpcode(Psop));  break;
  }
  Reader->endAtom();
}

void BytecodeReader::readSExprByType(TIL_Opcode op) {
  switch(op) {
#define TIL_OPCODE_DEF(X) \
    case COP_##X: read##X(); break;
#include "TILOps.def"
  }
}


void BytecodeReader::readAnnotation() {
  TIL_AnnKind Ak = readFlag<TIL_AnnKind>();
  readAnnotationByKind(Ak);
}


void BytecodeReader::readAnnotationByKind(TIL_AnnKind Ak) {
  Annotation *A;
  switch(Ak) {
#define TIL_ANNKIND_DEF(X) \
    case ANNKIND_##X: A = X::deserialize(this); break;
#include "TILAnnKinds.def"
  }
  arg(0)->addAnnotation(A);
}


SExpr* BytecodeReader::read() {
  while (!Reader->empty() && success()) {
    readSExpr();
  }
  if (!success()) {
    return nullptr;
  }
  if (Stack.size() == 0) {
    fail("Empty stack.");
    return nullptr;
  }
  if (Stack.size() > 1) {
    fail("Too many arguments on stack.");
    return Stack[Stack.size() - 1];
  }
  return Stack[0];
}


void BytecodeStringWriter::dump() {
  std::string Str = Buffer.str();
  int Sz  = Str.size();

  for (int64_t i = 0; i < Sz; ++i) {
    unsigned val = static_cast<uint8_t>(Str[i]);
    std::cout << " " << val;
  }
  std::cout << "\n";
}


}  // end namespace til
}  // end namespace ohmu
