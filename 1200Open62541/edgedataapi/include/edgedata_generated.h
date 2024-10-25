// automatically generated by the FlatBuffers compiler, do not modify


#ifndef FLATBUFFERS_GENERATED_EDGEDATA_EDGEDATA_FLATBUFFERS_EDGEDATAINFO__H_
#define FLATBUFFERS_GENERATED_EDGEDATA_EDGEDATA_FLATBUFFERS_EDGEDATAINFO__H_

#include "flatbuffers/flatbuffers.h"

namespace edgedata_flatbuffers {

struct EdgeDiscoverMessage;

struct EdgeDataEventMessage;

struct EdgeDataInfo;

namespace EdgeDataInfo_ {

struct Anonymous0;

}  // namespace EdgeDataInfo_

enum EdgeDataType {
  EdgeDataType_Unknown = 0,
  EdgeDataType_Integer32 = 1,
  EdgeDataType_UnsignedInteger32 = 2,
  EdgeDataType_Integer64 = 3,
  EdgeDataType_UnsignedInteger64 = 4,
  EdgeDataType_Float32 = 5,
  EdgeDataType_Double64 = 6,
  EdgeDataType_MIN = EdgeDataType_Unknown,
  EdgeDataType_MAX = EdgeDataType_Double64
};

inline const EdgeDataType (&EnumValuesEdgeDataType())[7] {
  static const EdgeDataType values[] = {
    EdgeDataType_Unknown,
    EdgeDataType_Integer32,
    EdgeDataType_UnsignedInteger32,
    EdgeDataType_Integer64,
    EdgeDataType_UnsignedInteger64,
    EdgeDataType_Float32,
    EdgeDataType_Double64
  };
  return values;
}

inline const char * const *EnumNamesEdgeDataType() {
  static const char * const names[] = {
    "Unknown",
    "Integer32",
    "UnsignedInteger32",
    "Integer64",
    "UnsignedInteger64",
    "Float32",
    "Double64",
    nullptr
  };
  return names;
}

inline const char *EnumNameEdgeDataType(EdgeDataType e) {
  const size_t index = static_cast<int>(e);
  return EnumNamesEdgeDataType()[index];
}

struct EdgeDiscoverMessage FLATBUFFERS_FINAL_CLASS : private flatbuffers::Table {
  enum {
    VT_DISCOVERLIST = 4
  };
  const flatbuffers::Vector<flatbuffers::Offset<EdgeDataInfo>> *DiscoverList() const {
    return GetPointer<const flatbuffers::Vector<flatbuffers::Offset<EdgeDataInfo>> *>(VT_DISCOVERLIST);
  }
  bool Verify(flatbuffers::Verifier &verifier) const {
    return VerifyTableStart(verifier) &&
           VerifyOffset(verifier, VT_DISCOVERLIST) &&
           verifier.VerifyVector(DiscoverList()) &&
           verifier.VerifyVectorOfTables(DiscoverList()) &&
           verifier.EndTable();
  }
};
 
struct EdgeDiscoverMessageBuilder {
  flatbuffers::FlatBufferBuilder &fbb_;
  flatbuffers::uoffset_t start_;
  void add_DiscoverList(flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<EdgeDataInfo>>> DiscoverList) {
    fbb_.AddOffset(EdgeDiscoverMessage::VT_DISCOVERLIST, DiscoverList);
  }
  explicit EdgeDiscoverMessageBuilder(flatbuffers::FlatBufferBuilder &_fbb)
        : fbb_(_fbb) {
    start_ = fbb_.StartTable();
  }
  EdgeDiscoverMessageBuilder &operator=(const EdgeDiscoverMessageBuilder &);
  flatbuffers::Offset<EdgeDiscoverMessage> Finish() {
    const auto end = fbb_.EndTable(start_);
    auto o = flatbuffers::Offset<EdgeDiscoverMessage>(end);
    return o;
  }
};

inline flatbuffers::Offset<EdgeDiscoverMessage> CreateEdgeDiscoverMessage(
    flatbuffers::FlatBufferBuilder &_fbb,
    flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<EdgeDataInfo>>> DiscoverList = 0) {
  EdgeDiscoverMessageBuilder builder_(_fbb);
  builder_.add_DiscoverList(DiscoverList);
  return builder_.Finish();
}

inline flatbuffers::Offset<EdgeDiscoverMessage> CreateEdgeDiscoverMessageDirect(
    flatbuffers::FlatBufferBuilder &_fbb,
    const std::vector<flatbuffers::Offset<EdgeDataInfo>> *DiscoverList = nullptr) {
  return edgedata_flatbuffers::CreateEdgeDiscoverMessage(
      _fbb,
      DiscoverList ? _fbb.CreateVector<flatbuffers::Offset<EdgeDataInfo>>(*DiscoverList) : 0);
}

struct EdgeDataEventMessage FLATBUFFERS_FINAL_CLASS : private flatbuffers::Table {
  enum {
    VT_EVENT = 4
  };
  const EdgeDataInfo *event() const {
    return GetPointer<const EdgeDataInfo *>(VT_EVENT);
  }
  bool Verify(flatbuffers::Verifier &verifier) const {
    return VerifyTableStart(verifier) &&
           VerifyOffset(verifier, VT_EVENT) &&
           verifier.VerifyTable(event()) &&
           verifier.EndTable();
  }
};

struct EdgeDataEventMessageBuilder {
  flatbuffers::FlatBufferBuilder &fbb_;
  flatbuffers::uoffset_t start_;
  void add_event(flatbuffers::Offset<EdgeDataInfo> event) {
    fbb_.AddOffset(EdgeDataEventMessage::VT_EVENT, event);
  }
  explicit EdgeDataEventMessageBuilder(flatbuffers::FlatBufferBuilder &_fbb)
        : fbb_(_fbb) {
    start_ = fbb_.StartTable();
  }
  EdgeDataEventMessageBuilder &operator=(const EdgeDataEventMessageBuilder &);
  flatbuffers::Offset<EdgeDataEventMessage> Finish() {
    const auto end = fbb_.EndTable(start_);
    auto o = flatbuffers::Offset<EdgeDataEventMessage>(end);
    return o;
  }
};

inline flatbuffers::Offset<EdgeDataEventMessage> CreateEdgeDataEventMessage(
    flatbuffers::FlatBufferBuilder &_fbb,
    flatbuffers::Offset<EdgeDataInfo> event = 0) {
  EdgeDataEventMessageBuilder builder_(_fbb);
  builder_.add_event(event);
  return builder_.Finish();
}

struct EdgeDataInfo FLATBUFFERS_FINAL_CLASS : private flatbuffers::Table {
  enum {
    VT_TOPIC = 4,
    VT_HANDLE = 6,
    VT_TYPE = 8,
    VT_SOURCE = 10,
    VT_QUALITY = 12,
    VT_TIMESTAMP64 = 14,
    VT_VALUE = 16
  };
  const flatbuffers::String *topic() const {
    return GetPointer<const flatbuffers::String *>(VT_TOPIC);
  }
  uint32_t handle() const {
    return GetField<uint32_t>(VT_HANDLE, 0);
  }
  EdgeDataType type() const {
    return static_cast<EdgeDataType>(GetField<int32_t>(VT_TYPE, 0));
  }
  uint32_t source() const {
    return GetField<uint32_t>(VT_SOURCE, 0);
  }
  uint32_t quality() const {
    return GetField<uint32_t>(VT_QUALITY, 0);
  }
  int64_t timestamp64() const {
    return GetField<int64_t>(VT_TIMESTAMP64, 0);
  }
  const edgedata_flatbuffers::EdgeDataInfo_::Anonymous0 *value() const {
    return GetPointer<const edgedata_flatbuffers::EdgeDataInfo_::Anonymous0 *>(VT_VALUE);
  }
  bool Verify(flatbuffers::Verifier &verifier) const {
    return VerifyTableStart(verifier) &&
           VerifyOffset(verifier, VT_TOPIC) &&
           verifier.VerifyString(topic()) &&
           VerifyField<uint32_t>(verifier, VT_HANDLE) &&
           VerifyField<int32_t>(verifier, VT_TYPE) &&
           VerifyField<uint32_t>(verifier, VT_SOURCE) &&
           VerifyField<uint32_t>(verifier, VT_QUALITY) &&
           VerifyField<int64_t>(verifier, VT_TIMESTAMP64) &&
           VerifyOffset(verifier, VT_VALUE) &&
           verifier.VerifyTable(value()) &&
           verifier.EndTable();
  }
};

struct EdgeDataInfoBuilder {
  flatbuffers::FlatBufferBuilder &fbb_;
  flatbuffers::uoffset_t start_;
  void add_topic(flatbuffers::Offset<flatbuffers::String> topic) {
    fbb_.AddOffset(EdgeDataInfo::VT_TOPIC, topic);
  }
  void add_handle(uint32_t handle) {
    fbb_.AddElement<uint32_t>(EdgeDataInfo::VT_HANDLE, handle, 0);
  }
  void add_type(EdgeDataType type) {
    fbb_.AddElement<int32_t>(EdgeDataInfo::VT_TYPE, static_cast<int32_t>(type), 0);
  }
  void add_source(uint32_t source) {
    fbb_.AddElement<uint32_t>(EdgeDataInfo::VT_SOURCE, source, 0);
  }
  void add_quality(uint32_t quality) {
    fbb_.AddElement<uint32_t>(EdgeDataInfo::VT_QUALITY, quality, 0);
  }
  void add_timestamp64(int64_t timestamp64) {
    fbb_.AddElement<int64_t>(EdgeDataInfo::VT_TIMESTAMP64, timestamp64, 0);
  }
  void add_value(flatbuffers::Offset<edgedata_flatbuffers::EdgeDataInfo_::Anonymous0> value) {
    fbb_.AddOffset(EdgeDataInfo::VT_VALUE, value);
  }
  explicit EdgeDataInfoBuilder(flatbuffers::FlatBufferBuilder &_fbb)
        : fbb_(_fbb) {
    start_ = fbb_.StartTable();
  }
  EdgeDataInfoBuilder &operator=(const EdgeDataInfoBuilder &);
  flatbuffers::Offset<EdgeDataInfo> Finish() {
    const auto end = fbb_.EndTable(start_);
    auto o = flatbuffers::Offset<EdgeDataInfo>(end);
    return o;
  }
};

inline flatbuffers::Offset<EdgeDataInfo> CreateEdgeDataInfo(
    flatbuffers::FlatBufferBuilder &_fbb,
    flatbuffers::Offset<flatbuffers::String> topic = 0,
    uint32_t handle = 0,
    EdgeDataType type = EdgeDataType_Unknown,
    uint32_t source = 0,
    uint32_t quality = 0,
    int64_t timestamp64 = 0,
    flatbuffers::Offset<edgedata_flatbuffers::EdgeDataInfo_::Anonymous0> value = 0) {
  EdgeDataInfoBuilder builder_(_fbb);
  builder_.add_timestamp64(timestamp64);
  builder_.add_value(value);
  builder_.add_quality(quality);
  builder_.add_source(source);
  builder_.add_type(type);
  builder_.add_handle(handle);
  builder_.add_topic(topic);
  return builder_.Finish();
}

inline flatbuffers::Offset<EdgeDataInfo> CreateEdgeDataInfoDirect(
    flatbuffers::FlatBufferBuilder &_fbb,
    const char *topic = nullptr,
    uint32_t handle = 0,
    EdgeDataType type = EdgeDataType_Unknown,
    uint32_t source = 0,
    uint32_t quality = 0,
    int64_t timestamp64 = 0,
    flatbuffers::Offset<edgedata_flatbuffers::EdgeDataInfo_::Anonymous0> value = 0) {
  return edgedata_flatbuffers::CreateEdgeDataInfo(
      _fbb,
      topic ? _fbb.CreateString(topic) : 0,
      handle,
      type,
      source,
      quality,
      timestamp64,
      value);
}

namespace EdgeDataInfo_ {

struct Anonymous0 FLATBUFFERS_FINAL_CLASS : private flatbuffers::Table {
  enum {
    VT_INTEGER32 = 4,
    VT_UNSIGNEDINTEGER32 = 6,
    VT_INTEGER64 = 8,
    VT_UNSIGNEDINTEGER64 = 10,
    VT_FLOAT32 = 12,
    VT_DOUBLE64 = 14
  };
  int32_t integer32() const {
    return GetField<int32_t>(VT_INTEGER32, 0);
  }
  uint32_t unsignedInteger32() const {
    return GetField<uint32_t>(VT_UNSIGNEDINTEGER32, 0);
  }
  int64_t integer64() const {
    return GetField<int64_t>(VT_INTEGER64, 0);
  }
  uint64_t unsignedInteger64() const {
    return GetField<uint64_t>(VT_UNSIGNEDINTEGER64, 0);
  }
  float float32() const {
    return GetField<float>(VT_FLOAT32, 0.0f);
  }
  double double64() const {
    return GetField<double>(VT_DOUBLE64, 0.0);
  }
  bool Verify(flatbuffers::Verifier &verifier) const {
    return VerifyTableStart(verifier) &&
           VerifyField<int32_t>(verifier, VT_INTEGER32) &&
           VerifyField<uint32_t>(verifier, VT_UNSIGNEDINTEGER32) &&
           VerifyField<int64_t>(verifier, VT_INTEGER64) &&
           VerifyField<uint64_t>(verifier, VT_UNSIGNEDINTEGER64) &&
           VerifyField<float>(verifier, VT_FLOAT32) &&
           VerifyField<double>(verifier, VT_DOUBLE64) &&
           verifier.EndTable();
  }
};

struct Anonymous0Builder {
  flatbuffers::FlatBufferBuilder &fbb_;
  flatbuffers::uoffset_t start_;
  void add_integer32(int32_t integer32) {
    fbb_.AddElement<int32_t>(Anonymous0::VT_INTEGER32, integer32, 0);
  }
  void add_unsignedInteger32(uint32_t unsignedInteger32) {
    fbb_.AddElement<uint32_t>(Anonymous0::VT_UNSIGNEDINTEGER32, unsignedInteger32, 0);
  }
  void add_integer64(int64_t integer64) {
    fbb_.AddElement<int64_t>(Anonymous0::VT_INTEGER64, integer64, 0);
  }
  void add_unsignedInteger64(uint64_t unsignedInteger64) {
    fbb_.AddElement<uint64_t>(Anonymous0::VT_UNSIGNEDINTEGER64, unsignedInteger64, 0);
  }
  void add_float32(float float32) {
    fbb_.AddElement<float>(Anonymous0::VT_FLOAT32, float32, 0.0f);
  }
  void add_double64(double double64) {
    fbb_.AddElement<double>(Anonymous0::VT_DOUBLE64, double64, 0.0);
  }
  explicit Anonymous0Builder(flatbuffers::FlatBufferBuilder &_fbb)
        : fbb_(_fbb) {
    start_ = fbb_.StartTable();
  }
  Anonymous0Builder &operator=(const Anonymous0Builder &);
  flatbuffers::Offset<Anonymous0> Finish() {
    const auto end = fbb_.EndTable(start_);
    auto o = flatbuffers::Offset<Anonymous0>(end);
    return o;
  }
};

inline flatbuffers::Offset<Anonymous0> CreateAnonymous0(
    flatbuffers::FlatBufferBuilder &_fbb,
    int32_t integer32 = 0,
    uint32_t unsignedInteger32 = 0,
    int64_t integer64 = 0,
    uint64_t unsignedInteger64 = 0,
    float float32 = 0.0f,
    double double64 = 0.0) {
  Anonymous0Builder builder_(_fbb);
  builder_.add_double64(double64);
  builder_.add_unsignedInteger64(unsignedInteger64);
  builder_.add_integer64(integer64);
  builder_.add_float32(float32);
  builder_.add_unsignedInteger32(unsignedInteger32);
  builder_.add_integer32(integer32);
  return builder_.Finish();
}

}  // namespace EdgeDataInfo_

namespace EdgeDataInfo_ {

}  // namespace EdgeDataInfo_
}  // namespace edgedata_flatbuffers

#endif  // FLATBUFFERS_GENERATED_EDGEDATA_EDGEDATA_FLATBUFFERS_EDGEDATAINFO__H_
