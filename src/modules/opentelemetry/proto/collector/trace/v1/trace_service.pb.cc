// Generated by the protocol buffer compiler.  DO NOT EDIT!
// source: opentelemetry/proto/collector/trace/v1/trace_service.proto

#include "opentelemetry/proto/collector/trace/v1/trace_service.pb.h"

#include <algorithm>
#include "google/protobuf/io/coded_stream.h"
#include "google/protobuf/extension_set.h"
#include "google/protobuf/wire_format_lite.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/generated_message_reflection.h"
#include "google/protobuf/reflection_ops.h"
#include "google/protobuf/wire_format.h"
// @@protoc_insertion_point(includes)

// Must be included last.
#include "google/protobuf/port_def.inc"
PROTOBUF_PRAGMA_INIT_SEG
namespace _pb = ::PROTOBUF_NAMESPACE_ID;
namespace _pbi = ::PROTOBUF_NAMESPACE_ID::internal;
namespace opentelemetry {
namespace proto {
namespace collector {
namespace trace {
namespace v1 {
template <typename>
PROTOBUF_CONSTEXPR ExportTraceServiceRequest::ExportTraceServiceRequest(
    ::_pbi::ConstantInitialized): _impl_{
    /*decltype(_impl_.resource_spans_)*/{}
  , /*decltype(_impl_._cached_size_)*/{}} {}
struct ExportTraceServiceRequestDefaultTypeInternal {
  PROTOBUF_CONSTEXPR ExportTraceServiceRequestDefaultTypeInternal() : _instance(::_pbi::ConstantInitialized{}) {}
  ~ExportTraceServiceRequestDefaultTypeInternal() {}
  union {
    ExportTraceServiceRequest _instance;
  };
};

PROTOBUF_ATTRIBUTE_NO_DESTROY PROTOBUF_CONSTINIT
    PROTOBUF_ATTRIBUTE_INIT_PRIORITY1 ExportTraceServiceRequestDefaultTypeInternal _ExportTraceServiceRequest_default_instance_;
template <typename>
PROTOBUF_CONSTEXPR ExportTraceServiceResponse::ExportTraceServiceResponse(
    ::_pbi::ConstantInitialized): _impl_{
    /*decltype(_impl_._has_bits_)*/{}
  , /*decltype(_impl_._cached_size_)*/{}
  , /*decltype(_impl_.partial_success_)*/nullptr} {}
struct ExportTraceServiceResponseDefaultTypeInternal {
  PROTOBUF_CONSTEXPR ExportTraceServiceResponseDefaultTypeInternal() : _instance(::_pbi::ConstantInitialized{}) {}
  ~ExportTraceServiceResponseDefaultTypeInternal() {}
  union {
    ExportTraceServiceResponse _instance;
  };
};

PROTOBUF_ATTRIBUTE_NO_DESTROY PROTOBUF_CONSTINIT
    PROTOBUF_ATTRIBUTE_INIT_PRIORITY1 ExportTraceServiceResponseDefaultTypeInternal _ExportTraceServiceResponse_default_instance_;
template <typename>
PROTOBUF_CONSTEXPR ExportTracePartialSuccess::ExportTracePartialSuccess(
    ::_pbi::ConstantInitialized): _impl_{
    /*decltype(_impl_.error_message_)*/ {
    &::_pbi::fixed_address_empty_string, ::_pbi::ConstantInitialized {}
  }

  , /*decltype(_impl_.rejected_spans_)*/ ::int64_t{0}

  , /*decltype(_impl_._cached_size_)*/{}} {}
struct ExportTracePartialSuccessDefaultTypeInternal {
  PROTOBUF_CONSTEXPR ExportTracePartialSuccessDefaultTypeInternal() : _instance(::_pbi::ConstantInitialized{}) {}
  ~ExportTracePartialSuccessDefaultTypeInternal() {}
  union {
    ExportTracePartialSuccess _instance;
  };
};

PROTOBUF_ATTRIBUTE_NO_DESTROY PROTOBUF_CONSTINIT
    PROTOBUF_ATTRIBUTE_INIT_PRIORITY1 ExportTracePartialSuccessDefaultTypeInternal _ExportTracePartialSuccess_default_instance_;
}  // namespace v1
}  // namespace trace
}  // namespace collector
}  // namespace proto
}  // namespace opentelemetry
static ::_pb::Metadata file_level_metadata_opentelemetry_2fproto_2fcollector_2ftrace_2fv1_2ftrace_5fservice_2eproto[3];
static constexpr const ::_pb::EnumDescriptor**
    file_level_enum_descriptors_opentelemetry_2fproto_2fcollector_2ftrace_2fv1_2ftrace_5fservice_2eproto = nullptr;
static constexpr const ::_pb::ServiceDescriptor**
    file_level_service_descriptors_opentelemetry_2fproto_2fcollector_2ftrace_2fv1_2ftrace_5fservice_2eproto = nullptr;
const ::uint32_t TableStruct_opentelemetry_2fproto_2fcollector_2ftrace_2fv1_2ftrace_5fservice_2eproto::offsets[] PROTOBUF_SECTION_VARIABLE(
    protodesc_cold) = {
    ~0u,  // no _has_bits_
    PROTOBUF_FIELD_OFFSET(::opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest, _internal_metadata_),
    ~0u,  // no _extensions_
    ~0u,  // no _oneof_case_
    ~0u,  // no _weak_field_map_
    ~0u,  // no _inlined_string_donated_
    ~0u,  // no _split_
    ~0u,  // no sizeof(Split)
    PROTOBUF_FIELD_OFFSET(::opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest, _impl_.resource_spans_),
    PROTOBUF_FIELD_OFFSET(::opentelemetry::proto::collector::trace::v1::ExportTraceServiceResponse, _impl_._has_bits_),
    PROTOBUF_FIELD_OFFSET(::opentelemetry::proto::collector::trace::v1::ExportTraceServiceResponse, _internal_metadata_),
    ~0u,  // no _extensions_
    ~0u,  // no _oneof_case_
    ~0u,  // no _weak_field_map_
    ~0u,  // no _inlined_string_donated_
    ~0u,  // no _split_
    ~0u,  // no sizeof(Split)
    PROTOBUF_FIELD_OFFSET(::opentelemetry::proto::collector::trace::v1::ExportTraceServiceResponse, _impl_.partial_success_),
    0,
    ~0u,  // no _has_bits_
    PROTOBUF_FIELD_OFFSET(::opentelemetry::proto::collector::trace::v1::ExportTracePartialSuccess, _internal_metadata_),
    ~0u,  // no _extensions_
    ~0u,  // no _oneof_case_
    ~0u,  // no _weak_field_map_
    ~0u,  // no _inlined_string_donated_
    ~0u,  // no _split_
    ~0u,  // no sizeof(Split)
    PROTOBUF_FIELD_OFFSET(::opentelemetry::proto::collector::trace::v1::ExportTracePartialSuccess, _impl_.rejected_spans_),
    PROTOBUF_FIELD_OFFSET(::opentelemetry::proto::collector::trace::v1::ExportTracePartialSuccess, _impl_.error_message_),
};

static const ::_pbi::MigrationSchema
    schemas[] PROTOBUF_SECTION_VARIABLE(protodesc_cold) = {
        { 0, -1, -1, sizeof(::opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest)},
        { 9, 18, -1, sizeof(::opentelemetry::proto::collector::trace::v1::ExportTraceServiceResponse)},
        { 19, -1, -1, sizeof(::opentelemetry::proto::collector::trace::v1::ExportTracePartialSuccess)},
};

static const ::_pb::Message* const file_default_instances[] = {
    &::opentelemetry::proto::collector::trace::v1::_ExportTraceServiceRequest_default_instance_._instance,
    &::opentelemetry::proto::collector::trace::v1::_ExportTraceServiceResponse_default_instance_._instance,
    &::opentelemetry::proto::collector::trace::v1::_ExportTracePartialSuccess_default_instance_._instance,
};
const char descriptor_table_protodef_opentelemetry_2fproto_2fcollector_2ftrace_2fv1_2ftrace_5fservice_2eproto[] PROTOBUF_SECTION_VARIABLE(protodesc_cold) = {
    "\n:opentelemetry/proto/collector/trace/v1"
    "/trace_service.proto\022&opentelemetry.prot"
    "o.collector.trace.v1\032(opentelemetry/prot"
    "o/trace/v1/trace.proto\"`\n\031ExportTraceSer"
    "viceRequest\022C\n\016resource_spans\030\001 \003(\0132+.op"
    "entelemetry.proto.trace.v1.ResourceSpans"
    "\"x\n\032ExportTraceServiceResponse\022Z\n\017partia"
    "l_success\030\001 \001(\0132A.opentelemetry.proto.co"
    "llector.trace.v1.ExportTracePartialSucce"
    "ss\"J\n\031ExportTracePartialSuccess\022\026\n\016rejec"
    "ted_spans\030\001 \001(\003\022\025\n\rerror_message\030\002 \001(\t2\242"
    "\001\n\014TraceService\022\221\001\n\006Export\022A.opentelemet"
    "ry.proto.collector.trace.v1.ExportTraceS"
    "erviceRequest\032B.opentelemetry.proto.coll"
    "ector.trace.v1.ExportTraceServiceRespons"
    "e\"\000B\234\001\n)io.opentelemetry.proto.collector"
    ".trace.v1B\021TraceServiceProtoP\001Z1go.opent"
    "elemetry.io/proto/otlp/collector/trace/v"
    "1\252\002&OpenTelemetry.Proto.Collector.Trace."
    "V1b\006proto3"
};
static const ::_pbi::DescriptorTable* const descriptor_table_opentelemetry_2fproto_2fcollector_2ftrace_2fv1_2ftrace_5fservice_2eproto_deps[1] =
    {
        &::descriptor_table_opentelemetry_2fproto_2ftrace_2fv1_2ftrace_2eproto,
};
static ::absl::once_flag descriptor_table_opentelemetry_2fproto_2fcollector_2ftrace_2fv1_2ftrace_5fservice_2eproto_once;
const ::_pbi::DescriptorTable descriptor_table_opentelemetry_2fproto_2fcollector_2ftrace_2fv1_2ftrace_5fservice_2eproto = {
    false,
    false,
    770,
    descriptor_table_protodef_opentelemetry_2fproto_2fcollector_2ftrace_2fv1_2ftrace_5fservice_2eproto,
    "opentelemetry/proto/collector/trace/v1/trace_service.proto",
    &descriptor_table_opentelemetry_2fproto_2fcollector_2ftrace_2fv1_2ftrace_5fservice_2eproto_once,
    descriptor_table_opentelemetry_2fproto_2fcollector_2ftrace_2fv1_2ftrace_5fservice_2eproto_deps,
    1,
    3,
    schemas,
    file_default_instances,
    TableStruct_opentelemetry_2fproto_2fcollector_2ftrace_2fv1_2ftrace_5fservice_2eproto::offsets,
    file_level_metadata_opentelemetry_2fproto_2fcollector_2ftrace_2fv1_2ftrace_5fservice_2eproto,
    file_level_enum_descriptors_opentelemetry_2fproto_2fcollector_2ftrace_2fv1_2ftrace_5fservice_2eproto,
    file_level_service_descriptors_opentelemetry_2fproto_2fcollector_2ftrace_2fv1_2ftrace_5fservice_2eproto,
};

// This function exists to be marked as weak.
// It can significantly speed up compilation by breaking up LLVM's SCC
// in the .pb.cc translation units. Large translation units see a
// reduction of more than 35% of walltime for optimized builds. Without
// the weak attribute all the messages in the file, including all the
// vtables and everything they use become part of the same SCC through
// a cycle like:
// GetMetadata -> descriptor table -> default instances ->
//   vtables -> GetMetadata
// By adding a weak function here we break the connection from the
// individual vtables back into the descriptor table.
PROTOBUF_ATTRIBUTE_WEAK const ::_pbi::DescriptorTable* descriptor_table_opentelemetry_2fproto_2fcollector_2ftrace_2fv1_2ftrace_5fservice_2eproto_getter() {
  return &descriptor_table_opentelemetry_2fproto_2fcollector_2ftrace_2fv1_2ftrace_5fservice_2eproto;
}
// Force running AddDescriptors() at dynamic initialization time.
PROTOBUF_ATTRIBUTE_INIT_PRIORITY2
static ::_pbi::AddDescriptorsRunner dynamic_init_dummy_opentelemetry_2fproto_2fcollector_2ftrace_2fv1_2ftrace_5fservice_2eproto(&descriptor_table_opentelemetry_2fproto_2fcollector_2ftrace_2fv1_2ftrace_5fservice_2eproto);
namespace opentelemetry {
namespace proto {
namespace collector {
namespace trace {
namespace v1 {
// ===================================================================

class ExportTraceServiceRequest::_Internal {
 public:
};

void ExportTraceServiceRequest::clear_resource_spans() {
  _internal_mutable_resource_spans()->Clear();
}
ExportTraceServiceRequest::ExportTraceServiceRequest(::PROTOBUF_NAMESPACE_ID::Arena* arena)
  : ::PROTOBUF_NAMESPACE_ID::Message(arena) {
  SharedCtor(arena);
  // @@protoc_insertion_point(arena_constructor:opentelemetry.proto.collector.trace.v1.ExportTraceServiceRequest)
}
ExportTraceServiceRequest::ExportTraceServiceRequest(const ExportTraceServiceRequest& from)
  : ::PROTOBUF_NAMESPACE_ID::Message() {
  ExportTraceServiceRequest* const _this = this; (void)_this;
  new (&_impl_) Impl_{
      decltype(_impl_.resource_spans_){from._impl_.resource_spans_}
    , /*decltype(_impl_._cached_size_)*/{}};

  _internal_metadata_.MergeFrom<::PROTOBUF_NAMESPACE_ID::UnknownFieldSet>(from._internal_metadata_);
  // @@protoc_insertion_point(copy_constructor:opentelemetry.proto.collector.trace.v1.ExportTraceServiceRequest)
}

inline void ExportTraceServiceRequest::SharedCtor(::_pb::Arena* arena) {
  (void)arena;
  new (&_impl_) Impl_{
      decltype(_impl_.resource_spans_){arena}
    , /*decltype(_impl_._cached_size_)*/{}
  };
}

ExportTraceServiceRequest::~ExportTraceServiceRequest() {
  // @@protoc_insertion_point(destructor:opentelemetry.proto.collector.trace.v1.ExportTraceServiceRequest)
  if (auto *arena = _internal_metadata_.DeleteReturnArena<::PROTOBUF_NAMESPACE_ID::UnknownFieldSet>()) {
  (void)arena;
    return;
  }
  SharedDtor();
}

inline void ExportTraceServiceRequest::SharedDtor() {
  ABSL_DCHECK(GetArenaForAllocation() == nullptr);
  _internal_mutable_resource_spans()->~RepeatedPtrField();
}

void ExportTraceServiceRequest::SetCachedSize(int size) const {
  _impl_._cached_size_.Set(size);
}

void ExportTraceServiceRequest::Clear() {
// @@protoc_insertion_point(message_clear_start:opentelemetry.proto.collector.trace.v1.ExportTraceServiceRequest)
  ::uint32_t cached_has_bits = 0;
  // Prevent compiler warnings about cached_has_bits being unused
  (void) cached_has_bits;

  _internal_mutable_resource_spans()->Clear();
  _internal_metadata_.Clear<::PROTOBUF_NAMESPACE_ID::UnknownFieldSet>();
}

const char* ExportTraceServiceRequest::_InternalParse(const char* ptr, ::_pbi::ParseContext* ctx) {
#define CHK_(x) if (PROTOBUF_PREDICT_FALSE(!(x))) goto failure
  while (!ctx->Done(&ptr)) {
    ::uint32_t tag;
    ptr = ::_pbi::ReadTag(ptr, &tag);
    switch (tag >> 3) {
      // repeated .opentelemetry.proto.trace.v1.ResourceSpans resource_spans = 1;
      case 1:
        if (PROTOBUF_PREDICT_TRUE(static_cast<::uint8_t>(tag) == 10)) {
          ptr -= 1;
          do {
            ptr += 1;
            ptr = ctx->ParseMessage(_internal_add_resource_spans(), ptr);
            CHK_(ptr);
            if (!ctx->DataAvailable(ptr)) break;
          } while (::PROTOBUF_NAMESPACE_ID::internal::ExpectTag<10>(ptr));
        } else {
          goto handle_unusual;
        }
        continue;
      default:
        goto handle_unusual;
    }  // switch
  handle_unusual:
    if ((tag == 0) || ((tag & 7) == 4)) {
      CHK_(ptr);
      ctx->SetLastTag(tag);
      goto message_done;
    }
    ptr = UnknownFieldParse(
        tag,
        _internal_metadata_.mutable_unknown_fields<::PROTOBUF_NAMESPACE_ID::UnknownFieldSet>(),
        ptr, ctx);
    CHK_(ptr != nullptr);
  }  // while
message_done:
  return ptr;
failure:
  ptr = nullptr;
  goto message_done;
#undef CHK_
}

::uint8_t* ExportTraceServiceRequest::_InternalSerialize(
    ::uint8_t* target, ::PROTOBUF_NAMESPACE_ID::io::EpsCopyOutputStream* stream) const {
  // @@protoc_insertion_point(serialize_to_array_start:opentelemetry.proto.collector.trace.v1.ExportTraceServiceRequest)
  ::uint32_t cached_has_bits = 0;
  (void) cached_has_bits;

  // repeated .opentelemetry.proto.trace.v1.ResourceSpans resource_spans = 1;
  for (unsigned i = 0,
      n = static_cast<unsigned>(this->_internal_resource_spans_size()); i < n; i++) {
    const auto& repfield = this->_internal_resource_spans(i);
    target = ::PROTOBUF_NAMESPACE_ID::internal::WireFormatLite::
        InternalWriteMessage(1, repfield, repfield.GetCachedSize(), target, stream);
  }

  if (PROTOBUF_PREDICT_FALSE(_internal_metadata_.have_unknown_fields())) {
    target = ::_pbi::WireFormat::InternalSerializeUnknownFieldsToArray(
        _internal_metadata_.unknown_fields<::PROTOBUF_NAMESPACE_ID::UnknownFieldSet>(::PROTOBUF_NAMESPACE_ID::UnknownFieldSet::default_instance), target, stream);
  }
  // @@protoc_insertion_point(serialize_to_array_end:opentelemetry.proto.collector.trace.v1.ExportTraceServiceRequest)
  return target;
}

::size_t ExportTraceServiceRequest::ByteSizeLong() const {
// @@protoc_insertion_point(message_byte_size_start:opentelemetry.proto.collector.trace.v1.ExportTraceServiceRequest)
  ::size_t total_size = 0;

  ::uint32_t cached_has_bits = 0;
  // Prevent compiler warnings about cached_has_bits being unused
  (void) cached_has_bits;

  // repeated .opentelemetry.proto.trace.v1.ResourceSpans resource_spans = 1;
  total_size += 1UL * this->_internal_resource_spans_size();
  for (const auto& msg : this->_internal_resource_spans()) {
    total_size +=
      ::PROTOBUF_NAMESPACE_ID::internal::WireFormatLite::MessageSize(msg);
  }

  return MaybeComputeUnknownFieldsSize(total_size, &_impl_._cached_size_);
}

const ::PROTOBUF_NAMESPACE_ID::Message::ClassData ExportTraceServiceRequest::_class_data_ = {
    ::PROTOBUF_NAMESPACE_ID::Message::CopyWithSourceCheck,
    ExportTraceServiceRequest::MergeImpl
};
const ::PROTOBUF_NAMESPACE_ID::Message::ClassData*ExportTraceServiceRequest::GetClassData() const { return &_class_data_; }


void ExportTraceServiceRequest::MergeImpl(::PROTOBUF_NAMESPACE_ID::Message& to_msg, const ::PROTOBUF_NAMESPACE_ID::Message& from_msg) {
  auto* const _this = static_cast<ExportTraceServiceRequest*>(&to_msg);
  auto& from = static_cast<const ExportTraceServiceRequest&>(from_msg);
  // @@protoc_insertion_point(class_specific_merge_from_start:opentelemetry.proto.collector.trace.v1.ExportTraceServiceRequest)
  ABSL_DCHECK_NE(&from, _this);
  ::uint32_t cached_has_bits = 0;
  (void) cached_has_bits;

  _this->_internal_mutable_resource_spans()->MergeFrom(from._internal_resource_spans());
  _this->_internal_metadata_.MergeFrom<::PROTOBUF_NAMESPACE_ID::UnknownFieldSet>(from._internal_metadata_);
}

void ExportTraceServiceRequest::CopyFrom(const ExportTraceServiceRequest& from) {
// @@protoc_insertion_point(class_specific_copy_from_start:opentelemetry.proto.collector.trace.v1.ExportTraceServiceRequest)
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}

bool ExportTraceServiceRequest::IsInitialized() const {
  return true;
}

void ExportTraceServiceRequest::InternalSwap(ExportTraceServiceRequest* other) {
  using std::swap;
  _internal_metadata_.InternalSwap(&other->_internal_metadata_);
  _internal_mutable_resource_spans()->InternalSwap(other->_internal_mutable_resource_spans());
}

::PROTOBUF_NAMESPACE_ID::Metadata ExportTraceServiceRequest::GetMetadata() const {
  return ::_pbi::AssignDescriptors(
      &descriptor_table_opentelemetry_2fproto_2fcollector_2ftrace_2fv1_2ftrace_5fservice_2eproto_getter, &descriptor_table_opentelemetry_2fproto_2fcollector_2ftrace_2fv1_2ftrace_5fservice_2eproto_once,
      file_level_metadata_opentelemetry_2fproto_2fcollector_2ftrace_2fv1_2ftrace_5fservice_2eproto[0]);
}
// ===================================================================

class ExportTraceServiceResponse::_Internal {
 public:
  using HasBits = decltype(std::declval<ExportTraceServiceResponse>()._impl_._has_bits_);
  static constexpr ::int32_t kHasBitsOffset =
    8 * PROTOBUF_FIELD_OFFSET(ExportTraceServiceResponse, _impl_._has_bits_);
  static const ::opentelemetry::proto::collector::trace::v1::ExportTracePartialSuccess& partial_success(const ExportTraceServiceResponse* msg);
  static void set_has_partial_success(HasBits* has_bits) {
    (*has_bits)[0] |= 1u;
  }
};

const ::opentelemetry::proto::collector::trace::v1::ExportTracePartialSuccess&
ExportTraceServiceResponse::_Internal::partial_success(const ExportTraceServiceResponse* msg) {
  return *msg->_impl_.partial_success_;
}
ExportTraceServiceResponse::ExportTraceServiceResponse(::PROTOBUF_NAMESPACE_ID::Arena* arena)
  : ::PROTOBUF_NAMESPACE_ID::Message(arena) {
  SharedCtor(arena);
  // @@protoc_insertion_point(arena_constructor:opentelemetry.proto.collector.trace.v1.ExportTraceServiceResponse)
}
ExportTraceServiceResponse::ExportTraceServiceResponse(const ExportTraceServiceResponse& from)
  : ::PROTOBUF_NAMESPACE_ID::Message() {
  ExportTraceServiceResponse* const _this = this; (void)_this;
  new (&_impl_) Impl_{
      decltype(_impl_._has_bits_){from._impl_._has_bits_}
    , /*decltype(_impl_._cached_size_)*/{}
    , decltype(_impl_.partial_success_){nullptr}};

  _internal_metadata_.MergeFrom<::PROTOBUF_NAMESPACE_ID::UnknownFieldSet>(from._internal_metadata_);
  if ((from._impl_._has_bits_[0] & 0x00000001u) != 0) {
    _this->_impl_.partial_success_ = new ::opentelemetry::proto::collector::trace::v1::ExportTracePartialSuccess(*from._impl_.partial_success_);
  }
  // @@protoc_insertion_point(copy_constructor:opentelemetry.proto.collector.trace.v1.ExportTraceServiceResponse)
}

inline void ExportTraceServiceResponse::SharedCtor(::_pb::Arena* arena) {
  (void)arena;
  new (&_impl_) Impl_{
      decltype(_impl_._has_bits_){}
    , /*decltype(_impl_._cached_size_)*/{}
    , decltype(_impl_.partial_success_){nullptr}
  };
}

ExportTraceServiceResponse::~ExportTraceServiceResponse() {
  // @@protoc_insertion_point(destructor:opentelemetry.proto.collector.trace.v1.ExportTraceServiceResponse)
  if (auto *arena = _internal_metadata_.DeleteReturnArena<::PROTOBUF_NAMESPACE_ID::UnknownFieldSet>()) {
  (void)arena;
    return;
  }
  SharedDtor();
}

inline void ExportTraceServiceResponse::SharedDtor() {
  ABSL_DCHECK(GetArenaForAllocation() == nullptr);
  if (this != internal_default_instance()) delete _impl_.partial_success_;
}

void ExportTraceServiceResponse::SetCachedSize(int size) const {
  _impl_._cached_size_.Set(size);
}

void ExportTraceServiceResponse::Clear() {
// @@protoc_insertion_point(message_clear_start:opentelemetry.proto.collector.trace.v1.ExportTraceServiceResponse)
  ::uint32_t cached_has_bits = 0;
  // Prevent compiler warnings about cached_has_bits being unused
  (void) cached_has_bits;

  cached_has_bits = _impl_._has_bits_[0];
  if (cached_has_bits & 0x00000001u) {
    ABSL_DCHECK(_impl_.partial_success_ != nullptr);
    _impl_.partial_success_->Clear();
  }
  _impl_._has_bits_.Clear();
  _internal_metadata_.Clear<::PROTOBUF_NAMESPACE_ID::UnknownFieldSet>();
}

const char* ExportTraceServiceResponse::_InternalParse(const char* ptr, ::_pbi::ParseContext* ctx) {
#define CHK_(x) if (PROTOBUF_PREDICT_FALSE(!(x))) goto failure
  _Internal::HasBits has_bits{};
  while (!ctx->Done(&ptr)) {
    ::uint32_t tag;
    ptr = ::_pbi::ReadTag(ptr, &tag);
    switch (tag >> 3) {
      // .opentelemetry.proto.collector.trace.v1.ExportTracePartialSuccess partial_success = 1;
      case 1:
        if (PROTOBUF_PREDICT_TRUE(static_cast<::uint8_t>(tag) == 10)) {
          ptr = ctx->ParseMessage(_internal_mutable_partial_success(), ptr);
          CHK_(ptr);
        } else {
          goto handle_unusual;
        }
        continue;
      default:
        goto handle_unusual;
    }  // switch
  handle_unusual:
    if ((tag == 0) || ((tag & 7) == 4)) {
      CHK_(ptr);
      ctx->SetLastTag(tag);
      goto message_done;
    }
    ptr = UnknownFieldParse(
        tag,
        _internal_metadata_.mutable_unknown_fields<::PROTOBUF_NAMESPACE_ID::UnknownFieldSet>(),
        ptr, ctx);
    CHK_(ptr != nullptr);
  }  // while
message_done:
  _impl_._has_bits_.Or(has_bits);
  return ptr;
failure:
  ptr = nullptr;
  goto message_done;
#undef CHK_
}

::uint8_t* ExportTraceServiceResponse::_InternalSerialize(
    ::uint8_t* target, ::PROTOBUF_NAMESPACE_ID::io::EpsCopyOutputStream* stream) const {
  // @@protoc_insertion_point(serialize_to_array_start:opentelemetry.proto.collector.trace.v1.ExportTraceServiceResponse)
  ::uint32_t cached_has_bits = 0;
  (void) cached_has_bits;

  cached_has_bits = _impl_._has_bits_[0];
  // .opentelemetry.proto.collector.trace.v1.ExportTracePartialSuccess partial_success = 1;
  if (cached_has_bits & 0x00000001u) {
    target = ::PROTOBUF_NAMESPACE_ID::internal::WireFormatLite::
      InternalWriteMessage(1, _Internal::partial_success(this),
        _Internal::partial_success(this).GetCachedSize(), target, stream);
  }

  if (PROTOBUF_PREDICT_FALSE(_internal_metadata_.have_unknown_fields())) {
    target = ::_pbi::WireFormat::InternalSerializeUnknownFieldsToArray(
        _internal_metadata_.unknown_fields<::PROTOBUF_NAMESPACE_ID::UnknownFieldSet>(::PROTOBUF_NAMESPACE_ID::UnknownFieldSet::default_instance), target, stream);
  }
  // @@protoc_insertion_point(serialize_to_array_end:opentelemetry.proto.collector.trace.v1.ExportTraceServiceResponse)
  return target;
}

::size_t ExportTraceServiceResponse::ByteSizeLong() const {
// @@protoc_insertion_point(message_byte_size_start:opentelemetry.proto.collector.trace.v1.ExportTraceServiceResponse)
  ::size_t total_size = 0;

  ::uint32_t cached_has_bits = 0;
  // Prevent compiler warnings about cached_has_bits being unused
  (void) cached_has_bits;

  // .opentelemetry.proto.collector.trace.v1.ExportTracePartialSuccess partial_success = 1;
  cached_has_bits = _impl_._has_bits_[0];
  if (cached_has_bits & 0x00000001u) {
    total_size += 1 +
      ::PROTOBUF_NAMESPACE_ID::internal::WireFormatLite::MessageSize(
        *_impl_.partial_success_);
  }

  return MaybeComputeUnknownFieldsSize(total_size, &_impl_._cached_size_);
}

const ::PROTOBUF_NAMESPACE_ID::Message::ClassData ExportTraceServiceResponse::_class_data_ = {
    ::PROTOBUF_NAMESPACE_ID::Message::CopyWithSourceCheck,
    ExportTraceServiceResponse::MergeImpl
};
const ::PROTOBUF_NAMESPACE_ID::Message::ClassData*ExportTraceServiceResponse::GetClassData() const { return &_class_data_; }


void ExportTraceServiceResponse::MergeImpl(::PROTOBUF_NAMESPACE_ID::Message& to_msg, const ::PROTOBUF_NAMESPACE_ID::Message& from_msg) {
  auto* const _this = static_cast<ExportTraceServiceResponse*>(&to_msg);
  auto& from = static_cast<const ExportTraceServiceResponse&>(from_msg);
  // @@protoc_insertion_point(class_specific_merge_from_start:opentelemetry.proto.collector.trace.v1.ExportTraceServiceResponse)
  ABSL_DCHECK_NE(&from, _this);
  ::uint32_t cached_has_bits = 0;
  (void) cached_has_bits;

  if ((from._impl_._has_bits_[0] & 0x00000001u) != 0) {
    _this->_internal_mutable_partial_success()->::opentelemetry::proto::collector::trace::v1::ExportTracePartialSuccess::MergeFrom(
        from._internal_partial_success());
  }
  _this->_internal_metadata_.MergeFrom<::PROTOBUF_NAMESPACE_ID::UnknownFieldSet>(from._internal_metadata_);
}

void ExportTraceServiceResponse::CopyFrom(const ExportTraceServiceResponse& from) {
// @@protoc_insertion_point(class_specific_copy_from_start:opentelemetry.proto.collector.trace.v1.ExportTraceServiceResponse)
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}

bool ExportTraceServiceResponse::IsInitialized() const {
  return true;
}

void ExportTraceServiceResponse::InternalSwap(ExportTraceServiceResponse* other) {
  using std::swap;
  _internal_metadata_.InternalSwap(&other->_internal_metadata_);
  swap(_impl_._has_bits_[0], other->_impl_._has_bits_[0]);
  swap(_impl_.partial_success_, other->_impl_.partial_success_);
}

::PROTOBUF_NAMESPACE_ID::Metadata ExportTraceServiceResponse::GetMetadata() const {
  return ::_pbi::AssignDescriptors(
      &descriptor_table_opentelemetry_2fproto_2fcollector_2ftrace_2fv1_2ftrace_5fservice_2eproto_getter, &descriptor_table_opentelemetry_2fproto_2fcollector_2ftrace_2fv1_2ftrace_5fservice_2eproto_once,
      file_level_metadata_opentelemetry_2fproto_2fcollector_2ftrace_2fv1_2ftrace_5fservice_2eproto[1]);
}
// ===================================================================

class ExportTracePartialSuccess::_Internal {
 public:
};

ExportTracePartialSuccess::ExportTracePartialSuccess(::PROTOBUF_NAMESPACE_ID::Arena* arena)
  : ::PROTOBUF_NAMESPACE_ID::Message(arena) {
  SharedCtor(arena);
  // @@protoc_insertion_point(arena_constructor:opentelemetry.proto.collector.trace.v1.ExportTracePartialSuccess)
}
ExportTracePartialSuccess::ExportTracePartialSuccess(const ExportTracePartialSuccess& from)
  : ::PROTOBUF_NAMESPACE_ID::Message() {
  ExportTracePartialSuccess* const _this = this; (void)_this;
  new (&_impl_) Impl_{
      decltype(_impl_.error_message_) {}

    , decltype(_impl_.rejected_spans_) {}

    , /*decltype(_impl_._cached_size_)*/{}};

  _internal_metadata_.MergeFrom<::PROTOBUF_NAMESPACE_ID::UnknownFieldSet>(from._internal_metadata_);
  _impl_.error_message_.InitDefault();
  #ifdef PROTOBUF_FORCE_COPY_DEFAULT_STRING
        _impl_.error_message_.Set("", GetArenaForAllocation());
  #endif  // PROTOBUF_FORCE_COPY_DEFAULT_STRING
  if (!from._internal_error_message().empty()) {
    _this->_impl_.error_message_.Set(from._internal_error_message(), _this->GetArenaForAllocation());
  }
  _this->_impl_.rejected_spans_ = from._impl_.rejected_spans_;
  // @@protoc_insertion_point(copy_constructor:opentelemetry.proto.collector.trace.v1.ExportTracePartialSuccess)
}

inline void ExportTracePartialSuccess::SharedCtor(::_pb::Arena* arena) {
  (void)arena;
  new (&_impl_) Impl_{
      decltype(_impl_.error_message_) {}

    , decltype(_impl_.rejected_spans_) { ::int64_t{0} }

    , /*decltype(_impl_._cached_size_)*/{}
  };
  _impl_.error_message_.InitDefault();
  #ifdef PROTOBUF_FORCE_COPY_DEFAULT_STRING
        _impl_.error_message_.Set("", GetArenaForAllocation());
  #endif  // PROTOBUF_FORCE_COPY_DEFAULT_STRING
}

ExportTracePartialSuccess::~ExportTracePartialSuccess() {
  // @@protoc_insertion_point(destructor:opentelemetry.proto.collector.trace.v1.ExportTracePartialSuccess)
  if (auto *arena = _internal_metadata_.DeleteReturnArena<::PROTOBUF_NAMESPACE_ID::UnknownFieldSet>()) {
  (void)arena;
    return;
  }
  SharedDtor();
}

inline void ExportTracePartialSuccess::SharedDtor() {
  ABSL_DCHECK(GetArenaForAllocation() == nullptr);
  _impl_.error_message_.Destroy();
}

void ExportTracePartialSuccess::SetCachedSize(int size) const {
  _impl_._cached_size_.Set(size);
}

void ExportTracePartialSuccess::Clear() {
// @@protoc_insertion_point(message_clear_start:opentelemetry.proto.collector.trace.v1.ExportTracePartialSuccess)
  ::uint32_t cached_has_bits = 0;
  // Prevent compiler warnings about cached_has_bits being unused
  (void) cached_has_bits;

  _impl_.error_message_.ClearToEmpty();
  _impl_.rejected_spans_ = ::int64_t{0};
  _internal_metadata_.Clear<::PROTOBUF_NAMESPACE_ID::UnknownFieldSet>();
}

const char* ExportTracePartialSuccess::_InternalParse(const char* ptr, ::_pbi::ParseContext* ctx) {
#define CHK_(x) if (PROTOBUF_PREDICT_FALSE(!(x))) goto failure
  while (!ctx->Done(&ptr)) {
    ::uint32_t tag;
    ptr = ::_pbi::ReadTag(ptr, &tag);
    switch (tag >> 3) {
      // int64 rejected_spans = 1;
      case 1:
        if (PROTOBUF_PREDICT_TRUE(static_cast<::uint8_t>(tag) == 8)) {
          _impl_.rejected_spans_ = ::PROTOBUF_NAMESPACE_ID::internal::ReadVarint64(&ptr);
          CHK_(ptr);
        } else {
          goto handle_unusual;
        }
        continue;
      // string error_message = 2;
      case 2:
        if (PROTOBUF_PREDICT_TRUE(static_cast<::uint8_t>(tag) == 18)) {
          auto str = _internal_mutable_error_message();
          ptr = ::_pbi::InlineGreedyStringParser(str, ptr, ctx);
          CHK_(ptr);
          CHK_(::_pbi::VerifyUTF8(str, "opentelemetry.proto.collector.trace.v1.ExportTracePartialSuccess.error_message"));
        } else {
          goto handle_unusual;
        }
        continue;
      default:
        goto handle_unusual;
    }  // switch
  handle_unusual:
    if ((tag == 0) || ((tag & 7) == 4)) {
      CHK_(ptr);
      ctx->SetLastTag(tag);
      goto message_done;
    }
    ptr = UnknownFieldParse(
        tag,
        _internal_metadata_.mutable_unknown_fields<::PROTOBUF_NAMESPACE_ID::UnknownFieldSet>(),
        ptr, ctx);
    CHK_(ptr != nullptr);
  }  // while
message_done:
  return ptr;
failure:
  ptr = nullptr;
  goto message_done;
#undef CHK_
}

::uint8_t* ExportTracePartialSuccess::_InternalSerialize(
    ::uint8_t* target, ::PROTOBUF_NAMESPACE_ID::io::EpsCopyOutputStream* stream) const {
  // @@protoc_insertion_point(serialize_to_array_start:opentelemetry.proto.collector.trace.v1.ExportTracePartialSuccess)
  ::uint32_t cached_has_bits = 0;
  (void) cached_has_bits;

  // int64 rejected_spans = 1;
  if (this->_internal_rejected_spans() != 0) {
    target = stream->EnsureSpace(target);
    target = ::_pbi::WireFormatLite::WriteInt64ToArray(
        1, this->_internal_rejected_spans(), target);
  }

  // string error_message = 2;
  if (!this->_internal_error_message().empty()) {
    const std::string& _s = this->_internal_error_message();
    ::PROTOBUF_NAMESPACE_ID::internal::WireFormatLite::VerifyUtf8String(
        _s.data(), static_cast<int>(_s.length()), ::PROTOBUF_NAMESPACE_ID::internal::WireFormatLite::SERIALIZE, "opentelemetry.proto.collector.trace.v1.ExportTracePartialSuccess.error_message");
    target = stream->WriteStringMaybeAliased(2, _s, target);
  }

  if (PROTOBUF_PREDICT_FALSE(_internal_metadata_.have_unknown_fields())) {
    target = ::_pbi::WireFormat::InternalSerializeUnknownFieldsToArray(
        _internal_metadata_.unknown_fields<::PROTOBUF_NAMESPACE_ID::UnknownFieldSet>(::PROTOBUF_NAMESPACE_ID::UnknownFieldSet::default_instance), target, stream);
  }
  // @@protoc_insertion_point(serialize_to_array_end:opentelemetry.proto.collector.trace.v1.ExportTracePartialSuccess)
  return target;
}

::size_t ExportTracePartialSuccess::ByteSizeLong() const {
// @@protoc_insertion_point(message_byte_size_start:opentelemetry.proto.collector.trace.v1.ExportTracePartialSuccess)
  ::size_t total_size = 0;

  ::uint32_t cached_has_bits = 0;
  // Prevent compiler warnings about cached_has_bits being unused
  (void) cached_has_bits;

  // string error_message = 2;
  if (!this->_internal_error_message().empty()) {
    total_size += 1 + ::PROTOBUF_NAMESPACE_ID::internal::WireFormatLite::StringSize(
                                    this->_internal_error_message());
  }

  // int64 rejected_spans = 1;
  if (this->_internal_rejected_spans() != 0) {
    total_size += ::_pbi::WireFormatLite::Int64SizePlusOne(
        this->_internal_rejected_spans());
  }

  return MaybeComputeUnknownFieldsSize(total_size, &_impl_._cached_size_);
}

const ::PROTOBUF_NAMESPACE_ID::Message::ClassData ExportTracePartialSuccess::_class_data_ = {
    ::PROTOBUF_NAMESPACE_ID::Message::CopyWithSourceCheck,
    ExportTracePartialSuccess::MergeImpl
};
const ::PROTOBUF_NAMESPACE_ID::Message::ClassData*ExportTracePartialSuccess::GetClassData() const { return &_class_data_; }


void ExportTracePartialSuccess::MergeImpl(::PROTOBUF_NAMESPACE_ID::Message& to_msg, const ::PROTOBUF_NAMESPACE_ID::Message& from_msg) {
  auto* const _this = static_cast<ExportTracePartialSuccess*>(&to_msg);
  auto& from = static_cast<const ExportTracePartialSuccess&>(from_msg);
  // @@protoc_insertion_point(class_specific_merge_from_start:opentelemetry.proto.collector.trace.v1.ExportTracePartialSuccess)
  ABSL_DCHECK_NE(&from, _this);
  ::uint32_t cached_has_bits = 0;
  (void) cached_has_bits;

  if (!from._internal_error_message().empty()) {
    _this->_internal_set_error_message(from._internal_error_message());
  }
  if (from._internal_rejected_spans() != 0) {
    _this->_internal_set_rejected_spans(from._internal_rejected_spans());
  }
  _this->_internal_metadata_.MergeFrom<::PROTOBUF_NAMESPACE_ID::UnknownFieldSet>(from._internal_metadata_);
}

void ExportTracePartialSuccess::CopyFrom(const ExportTracePartialSuccess& from) {
// @@protoc_insertion_point(class_specific_copy_from_start:opentelemetry.proto.collector.trace.v1.ExportTracePartialSuccess)
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}

bool ExportTracePartialSuccess::IsInitialized() const {
  return true;
}

void ExportTracePartialSuccess::InternalSwap(ExportTracePartialSuccess* other) {
  using std::swap;
  auto* lhs_arena = GetArenaForAllocation();
  auto* rhs_arena = other->GetArenaForAllocation();
  _internal_metadata_.InternalSwap(&other->_internal_metadata_);
  ::_pbi::ArenaStringPtr::InternalSwap(&_impl_.error_message_, lhs_arena,
                                       &other->_impl_.error_message_, rhs_arena);

  swap(_impl_.rejected_spans_, other->_impl_.rejected_spans_);
}

::PROTOBUF_NAMESPACE_ID::Metadata ExportTracePartialSuccess::GetMetadata() const {
  return ::_pbi::AssignDescriptors(
      &descriptor_table_opentelemetry_2fproto_2fcollector_2ftrace_2fv1_2ftrace_5fservice_2eproto_getter, &descriptor_table_opentelemetry_2fproto_2fcollector_2ftrace_2fv1_2ftrace_5fservice_2eproto_once,
      file_level_metadata_opentelemetry_2fproto_2fcollector_2ftrace_2fv1_2ftrace_5fservice_2eproto[2]);
}
// @@protoc_insertion_point(namespace_scope)
}  // namespace v1
}  // namespace trace
}  // namespace collector
}  // namespace proto
}  // namespace opentelemetry
PROTOBUF_NAMESPACE_OPEN
template<> PROTOBUF_NOINLINE ::opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest*
Arena::CreateMaybeMessage< ::opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest >(Arena* arena) {
  return Arena::CreateMessageInternal< ::opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest >(arena);
}
template<> PROTOBUF_NOINLINE ::opentelemetry::proto::collector::trace::v1::ExportTraceServiceResponse*
Arena::CreateMaybeMessage< ::opentelemetry::proto::collector::trace::v1::ExportTraceServiceResponse >(Arena* arena) {
  return Arena::CreateMessageInternal< ::opentelemetry::proto::collector::trace::v1::ExportTraceServiceResponse >(arena);
}
template<> PROTOBUF_NOINLINE ::opentelemetry::proto::collector::trace::v1::ExportTracePartialSuccess*
Arena::CreateMaybeMessage< ::opentelemetry::proto::collector::trace::v1::ExportTracePartialSuccess >(Arena* arena) {
  return Arena::CreateMessageInternal< ::opentelemetry::proto::collector::trace::v1::ExportTracePartialSuccess >(arena);
}
PROTOBUF_NAMESPACE_CLOSE
// @@protoc_insertion_point(global_scope)
#include "google/protobuf/port_undef.inc"
