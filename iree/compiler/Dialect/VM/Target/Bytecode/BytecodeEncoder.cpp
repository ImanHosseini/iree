// Copyright 2019 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/compiler/Dialect/VM/Target/Bytecode/BytecodeEncoder.h"

#include "iree/compiler/Dialect/IREE/IR/IREETypes.h"
#include "iree/compiler/Dialect/VM/Analysis/RegisterAllocation.h"
#include "iree/compiler/Dialect/VM/IR/VMDialect.h"
#include "llvm/ADT/STLExtras.h"
#include "mlir/IR/Attributes.h"
#include "mlir/IR/Diagnostics.h"

namespace mlir {
namespace iree_compiler {
namespace IREE {
namespace VM {

namespace {

// v0 bytecode spec. This is in extreme flux and not guaranteed to be a stable
// representation. Always generate this from source in tooling and never check
// in any emitted files!
class V0BytecodeEncoder : public BytecodeEncoder {
 public:
  V0BytecodeEncoder(llvm::DenseMap<Type, int> *typeTable,
                    RegisterAllocation *registerAllocation)
      : typeTable_(typeTable), registerAllocation_(registerAllocation) {}
  ~V0BytecodeEncoder() = default;

  LogicalResult beginBlock(Block *block) override {
    blockOffsets_[block] = bytecode_.size();
    return success();
  }

  LogicalResult endBlock(Block *block) override { return success(); }

  LogicalResult beginOp(Operation *op) override {
    currentOp_ = op;
    // TODO(benvanik): encode source information (start).
    return success();
  }

  LogicalResult endOp(Operation *op) override {
    // TODO(benvanik): encode source information (end).
    currentOp_ = nullptr;
    return success();
  }

  LogicalResult encodeI8(int value) override { return writeUint8(value); }

  LogicalResult encodeOpcode(StringRef name, int opcode) override {
    return writeUint8(opcode);
  }

  LogicalResult encodeSymbolOrdinal(SymbolTable &syms,
                                    StringRef name) override {
    auto *symbolOp = syms.lookup(name);
    if (!symbolOp) {
      return currentOp_->emitOpError() << "target symbol not found: " << name;
    }
    auto ordinalAttr = symbolOp->getAttrOfType<IntegerAttr>("ordinal");
    if (!ordinalAttr) {
      return symbolOp->emitOpError() << "missing ordinal";
    }
    int32_t ordinal = ordinalAttr.getInt();
    if (isa<IREE::VM::ImportOp>(symbolOp)) {
      // Imported functions have their MSB set.
      ordinal |= 0x80000000u;
    }
    return writeInt32(ordinal);
  }

  LogicalResult encodeType(Value value) override {
    auto refPtrType = value.getType().dyn_cast<IREE::VM::RefType>();
    if (!refPtrType) {
      return currentOp_->emitOpError()
             << "type " << value.getType()
             << " is not supported as a serialized type kind";
    }
    return encodeType(refPtrType.getObjectType());
  }

  LogicalResult encodeType(Type type) override {
    // HACK: it'd be nice to remove the implicit ref wrapper hiding.
    if (auto refType = type.dyn_cast<IREE::VM::RefType>()) {
      if (refType.getObjectType().isa<IREE::VM::ListType>()) {
        type = refType.getObjectType();
      }
    }
    auto it = typeTable_->find(type);
    if (it == typeTable_->end()) {
      return currentOp_->emitOpError()
             << "type " << type
             << " cannot be encoded; not registered in type table";
    }
    int typeOrdinal = it->second;
    return writeUint32(typeOrdinal);
  }

  LogicalResult encodePrimitiveAttr(Attribute attr) override {
    unsigned int bitWidth = attr.getType().getIntOrFloatBitWidth();
    if (auto integerAttr = attr.dyn_cast<IntegerAttr>()) {
      uint64_t limitedValue =
          integerAttr.getValue().extractBitsAsZExtValue(bitWidth, 0);
      switch (bitWidth) {
        case 8:
          return writeUint8(static_cast<uint8_t>(limitedValue));
        case 16:
          return writeUint16(static_cast<uint16_t>(limitedValue));
        case 32:
          return writeUint32(static_cast<uint32_t>(limitedValue));
        case 64:
          return writeUint64(static_cast<uint64_t>(limitedValue));
        default:
          return currentOp_->emitOpError()
                 << "attribute of bitwidth " << bitWidth << " not supported";
      }
    } else if (auto floatAttr = attr.dyn_cast<FloatAttr>()) {
      switch (bitWidth) {
        case 32: {
          union {
            float f32;
            uint32_t u32;
          } value;
          value.f32 = floatAttr.getValue().convertToFloat();
          return writeUint32(value.u32);
        }
        case 64: {
          union {
            double f64;
            uint64_t u64;
          } value;
          value.f64 = floatAttr.getValue().convertToDouble();
          return writeUint64(value.u64);
        }
        default:
          return currentOp_->emitOpError()
                 << "attribute of bitwidth " << bitWidth << " not supported";
      }
    } else {
      return currentOp_->emitOpError()
             << "attribute type not supported for primitive serialization: "
             << attr;
    }
  }

  LogicalResult encodePrimitiveArrayAttr(DenseElementsAttr value) override {
    if (value.getNumElements() > UINT16_MAX || failed(ensureAlignment(2)) ||
        failed(writeUint16(value.getNumElements()))) {
      return currentOp_->emitOpError() << "integer array size out of bounds";
    }
    for (auto el : value.getAttributeValues()) {
      if (failed(encodePrimitiveAttr(el))) {
        return currentOp_->emitOpError() << "failed to encode element " << el;
      }
    }
    return success();
  }

  LogicalResult encodeStrAttr(StringAttr value) override {
    if (!value) {
      return writeUint16(0);
    }
    auto stringValue = value.getValue();
    if (stringValue.size() > UINT16_MAX) {
      return currentOp_->emitOpError()
             << "string attribute too large for 16-bit p-string (needs "
             << stringValue.size() << " bytes)";
    }
    return failure(failed(writeUint16(stringValue.size())) ||
                   failed(writeBytes(stringValue.data(), stringValue.size())));
  }

  LogicalResult encodeBranch(Block *targetBlock,
                             Operation::operand_range operands,
                             int successorIndex) override {
    // Reserve space for the block offset. It will get fixed up when we are all
    // done and know all of the block offsets.
    blockOffsetFixups_.push_back({targetBlock, bytecode_.size()});
    bytecode_.resize(bytecode_.size() + sizeof(int32_t));

    // Compute required remappings - we only need to emit them when the source
    // and dest registers differ. Hopefully the allocator did a good job and
    // this list is small :)
    auto srcDstRegs = registerAllocation_->remapSuccessorRegisters(
        currentOp_, successorIndex);
    if (failed(ensureAlignment(2)) || failed(writeUint16(srcDstRegs.size()))) {
      return failure();
    }
    for (auto srcDstReg : srcDstRegs) {
      if (failed(writeUint16(srcDstReg.first.encode())) ||
          failed(writeUint16(srcDstReg.second.encode()))) {
        return failure();
      }
    }

    return success();
  }

  LogicalResult encodeOperand(Value value, int ordinal) override {
    uint16_t reg =
        registerAllocation_->mapUseToRegister(value, currentOp_, ordinal)
            .encode();
    return writeUint16(reg);
  }

  LogicalResult encodeOperands(Operation::operand_range values) override {
    if (failed(ensureAlignment(2)) ||
        failed(writeUint16(std::distance(values.begin(), values.end())))) {
      return failure();
    }
    for (auto it : llvm::enumerate(values)) {
      uint16_t reg = registerAllocation_
                         ->mapUseToRegister(it.value(), currentOp_, it.index())
                         .encode();
      if (failed(writeUint16(reg))) {
        return failure();
      }
    }
    return success();
  }

  LogicalResult encodeResult(Value value) override {
    uint16_t reg = registerAllocation_->mapToRegister(value).encode();
    return writeUint16(reg);
  }

  LogicalResult encodeResults(Operation::result_range values) override {
    if (failed(ensureAlignment(2)) ||
        failed(writeUint16(std::distance(values.begin(), values.end())))) {
      return failure();
    }
    for (auto value : values) {
      uint16_t reg = registerAllocation_->mapToRegister(value).encode();
      if (failed(writeUint16(reg))) {
        return failure();
      }
    }
    return success();
  }

  Optional<std::vector<uint8_t>> finish() {
    if (failed(fixupOffsets())) {
      return llvm::None;
    }
    return std::move(bytecode_);
  }

  LogicalResult ensureAlignment(size_t alignment) {
    size_t paddedSize = (bytecode_.size() + (alignment - 1)) & ~(alignment - 1);
    size_t padding = paddedSize - bytecode_.size();
    if (padding == 0) return success();
    static const uint8_t kZeros[32] = {0};
    if (padding > sizeof(kZeros)) return failure();
    return writeBytes(kZeros, padding);
  }

 private:
  // TODO(benvanik): replace this with something not using an ever-expanding
  // vector. I'm sure LLVM has something.

  MutableArrayRef<uint8_t> reserveBytes(size_t dataLength) {
    int offset = bytecode_.size();
    bytecode_.resize(offset + dataLength);
    return MutableArrayRef<uint8_t>(
        reinterpret_cast<uint8_t *>(bytecode_.data()) + offset, dataLength);
  }

  LogicalResult writeBytes(const void *data, size_t dataLength) {
    auto dst = reserveBytes(dataLength);
    if (dataLength != dst.size()) {
      return failure();
    }
    std::memcpy(dst.data(), data, dst.size());
    return success();
  }

  LogicalResult writeUint8(uint8_t value) {
    return writeBytes(&value, sizeof(value));
  }

  LogicalResult writeUint16(uint16_t value) {
    return writeBytes(&value, sizeof(value));
  }

  LogicalResult writeInt32(int32_t value) {
    return writeBytes(&value, sizeof(value));
  }

  LogicalResult writeUint32(uint32_t value) {
    return writeBytes(&value, sizeof(value));
  }

  LogicalResult writeUint64(uint64_t value) {
    return writeBytes(&value, sizeof(value));
  }

  LogicalResult fixupOffsets() {
    for (const auto &fixup : blockOffsetFixups_) {
      auto blockOffset = blockOffsets_.find(fixup.first);
      if (blockOffset == blockOffsets_.end()) {
        emitError(fixup.first->front().getLoc()) << "Block offset not found";
        return failure();
      }
      std::memcpy(bytecode_.data() + fixup.second, &blockOffset->second,
                  sizeof(int32_t));
    }
    blockOffsetFixups_.clear();
    return success();
  }

  llvm::DenseMap<Type, int> *typeTable_;
  RegisterAllocation *registerAllocation_;

  Operation *currentOp_ = nullptr;

  std::vector<uint8_t> bytecode_;
  llvm::DenseMap<Block *, size_t> blockOffsets_;
  std::vector<std::pair<Block *, size_t>> blockOffsetFixups_;
};

}  // namespace

// static
Optional<EncodedBytecodeFunction> BytecodeEncoder::encodeFunction(
    IREE::VM::FuncOp funcOp, llvm::DenseMap<Type, int> &typeTable,
    SymbolTable &symbolTable) {
  EncodedBytecodeFunction result;

  // Perform register allocation first so that we can quickly lookup values as
  // we encode the operands/results.
  RegisterAllocation registerAllocation;
  if (failed(registerAllocation.recalculate(funcOp))) {
    funcOp.emitError() << "register allocation failed";
    return llvm::None;
  }

  V0BytecodeEncoder encoder(&typeTable, &registerAllocation);
  for (auto &block : funcOp.getBlocks()) {
    if (failed(encoder.beginBlock(&block))) {
      funcOp.emitError() << "failed to begin block";
      return llvm::None;
    }

    for (auto &op : block.getOperations()) {
      auto serializableOp = dyn_cast<IREE::VM::VMSerializableOp>(op);
      if (!serializableOp) {
        op.emitOpError() << "is not serializable";
        return llvm::None;
      }
      if (failed(encoder.beginOp(&op)) ||
          failed(serializableOp.encode(symbolTable, encoder)) ||
          failed(encoder.endOp(&op))) {
        op.emitOpError() << "failed to encode";
        return llvm::None;
      }
    }

    if (failed(encoder.endBlock(&block))) {
      funcOp.emitError() << "failed to end block";
      return llvm::None;
    }
  }

  if (failed(encoder.ensureAlignment(8))) {
    funcOp.emitError() << "failed to pad function";
    return llvm::None;
  }
  auto bytecodeData = encoder.finish();
  if (!bytecodeData.hasValue()) {
    funcOp.emitError() << "failed to fixup and finish encoding";
    return llvm::None;
  }
  result.bytecodeData = bytecodeData.getValue();
  result.i32RegisterCount = registerAllocation.getMaxI32RegisterOrdinal() + 1;
  result.refRegisterCount = registerAllocation.getMaxRefRegisterOrdinal() + 1;
  return result;
}

}  // namespace VM
}  // namespace IREE
}  // namespace iree_compiler
}  // namespace mlir
