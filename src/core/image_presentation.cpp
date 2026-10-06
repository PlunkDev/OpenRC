#include "openrc/image_presentation.hpp"
#include "openrc/prepared_game_v2.hpp"
#include <algorithm>

namespace openrc {
namespace {
constexpr std::array<std::byte,8> magic{std::byte{'O'},std::byte{'R'},std::byte{'I'},std::byte{'M'},
    std::byte{'G'},std::byte{'P'},std::byte{'R'},std::byte{'1'}};
[[noreturn]] void fail(const char* message) { throw ImagePresentationError(message); }
void put(std::vector<std::byte>& out,std::uint64_t value,unsigned count=4) {
  for(unsigned i=0;i<count;++i) { out.push_back(static_cast<std::byte>(value&255));value>>=8; }
}
struct Reader {
  std::span<const std::byte> bytes;std::size_t at=0;
  std::span<const std::byte> take(std::size_t count) {
    if(count>bytes.size()-at) fail("Truncated image presentation");
    auto data=bytes.subspan(at,count);at+=count;return data;
  }
  std::uint64_t get(unsigned count=4) {
    auto data=take(count);std::uint64_t value=0;
    for(unsigned i=0;i<count;++i) value|=std::uint64_t(std::to_integer<unsigned>(data[i]))<<(i*8);
    return value;
  }
  std::uint32_t word() { return static_cast<std::uint32_t>(get()); }
};
void timing(const ImagePresentationV1& image) {
  if(!image.updates_per_second||image.updates_per_second>1000||image.transfer_lead_updates>1000||
      image.transfer_tail_updates>1000||!image.initialization_clock_hz||image.initialization_clock_hz>1'000'000'000U||
      !image.initialization_clock_modulus||!image.initialization_credit_divisor||
      image.minimum_initialization_updates>1'000'000U) fail("Image presentation timing exceeds bounds");
}
}
void validate_image_presentation_v1(const ImagePresentationV1& image,ImagePresentationLimitsV1 limits) {
  timing(image);
  if(!image.width||!image.height||image.width>limits.max_width||image.height>limits.max_height||
      !image.display_aspect_numerator||!image.display_aspect_denominator||
      image.display_aspect_numerator>65535||image.display_aspect_denominator>65535||
      image.color_transfers.size()>limits.max_transfers||
      image.rgba.size()!=std::uint64_t(image.width)*image.height*4U||
      112U+image.rgba.size()+std::uint64_t(image.color_transfers.size())*256U>limits.max_bytes)
    fail("Image presentation geometry exceeds bounds");
  for(std::size_t i=3;i<image.rgba.size();i+=4)
    if(image.rgba[i]!=std::byte{255}) fail("Image presentation must contain opaque neutral pixels");
}
std::vector<std::byte> encode_image_presentation_v1(const ImagePresentationV1& image,ImagePresentationLimitsV1 limits) {
  validate_image_presentation_v1(image,limits);std::vector<std::byte> body;
  for(auto word:{image.width,image.height,image.display_aspect_numerator,image.display_aspect_denominator,
      image.updates_per_second,image.transfer_lead_updates,image.transfer_tail_updates,
      image.initialization_clock_hz,image.initialization_clock_modulus,image.initialization_credit_divisor,
      image.minimum_initialization_updates,0U}) put(body,word);
  for(const auto& transfer:image.color_transfers) body.insert(body.end(),transfer.begin(),transfer.end());
  body.insert(body.end(),image.rgba.begin(),image.rgba.end());
  std::vector<std::byte> out(magic.begin(),magic.end());put(out,1);put(out,64);put(out,64U+body.size(),8);
  put(out,image.color_transfers.size());put(out,0);
  auto digest=prepared_content_sha256_v1(body);out.insert(out.end(),digest.begin(),digest.end());
  out.insert(out.end(),body.begin(),body.end());return out;
}
ImagePresentationV1 decode_image_presentation_v1(std::span<const std::byte> bytes,ImagePresentationLimitsV1 limits) {
  if(bytes.size()<112||bytes.size()>limits.max_bytes) fail("Image presentation byte envelope exceeds limits");
  Reader in{bytes};auto signature=in.take(8);
  if(!std::equal(signature.begin(),signature.end(),magic.begin())||in.get()!=1||in.get()!=64||in.get(8)!=bytes.size()) fail("Invalid image presentation header");
  auto count=in.word();if(count>limits.max_transfers||in.get()!=0) fail("Invalid image transfer count or flags");
  auto stored=in.take(32);auto digest=prepared_content_sha256_v1(bytes.subspan(64));
  if(!std::equal(stored.begin(),stored.end(),digest.begin())) fail("Image presentation digest mismatch");
  ImagePresentationV1 image;image.width=in.word();image.height=in.word();
  image.display_aspect_numerator=in.word();image.display_aspect_denominator=in.word();
  image.updates_per_second=in.word();image.transfer_lead_updates=in.word();image.transfer_tail_updates=in.word();
  image.initialization_clock_hz=in.word();image.initialization_clock_modulus=in.word();
  image.initialization_credit_divisor=in.word();image.minimum_initialization_updates=in.word();
  if(in.word()!=0||!image.width||!image.height||image.width>limits.max_width||image.height>limits.max_height||
      112U+std::uint64_t(count)*256U+std::uint64_t(image.width)*image.height*4U!=bytes.size()) fail("Image dimensions do not partition its payload");
  image.color_transfers.resize(count);
  for(auto& transfer:image.color_transfers) { auto data=in.take(256);std::copy(data.begin(),data.end(),transfer.begin()); }
  auto pixels=in.take(bytes.size()-in.at);image.rgba.assign(pixels.begin(),pixels.end());
  validate_image_presentation_v1(image,limits);return image;
}
std::uint32_t image_initialization_remaining_updates_v1(const ImagePresentationV1& image,std::uint64_t elapsed) {
  timing(image);
  const std::uint64_t modulus=image.initialization_clock_modulus;
  const auto whole=((elapsed/1'000'000'000U)%modulus)*image.initialization_clock_hz;
  const auto fraction=(elapsed%1'000'000'000U)*image.initialization_clock_hz/1'000'000'000U;
  const auto credit=((whole%modulus+fraction)%modulus)/image.initialization_credit_divisor;
  return credit>=image.minimum_initialization_updates?0U:
      image.minimum_initialization_updates-static_cast<std::uint32_t>(credit);
}
void apply_image_color_transfer_v1(std::span<std::byte> rgba,const std::array<std::byte,256>& transfer) {
  if(rgba.size()%4) fail("Color transfer requires complete RGBA pixels");
  for(std::size_t i=0;i<rgba.size();i+=4) {
    for(std::size_t c=0;c<3;++c) rgba[i+c]=transfer[std::to_integer<unsigned>(rgba[i+c])];
    rgba[i+3]=std::byte{255};
  }
}
namespace {
constexpr std::array<std::byte,8> transfer_magic{std::byte{'O'},std::byte{'R'},std::byte{'F'},std::byte{'C'},
    std::byte{'T'},std::byte{'S'},std::byte{'Q'},std::byte{'1'}};
}
void validate_frame_color_transfer_sequence_v1(const FrameColorTransferSequenceV1& value,
    FrameColorTransferSequenceLimitsV1 limits) {
  if(!value.updates_per_second||value.updates_per_second>1000U||value.lead_updates>1000U||
      value.tail_updates>1000U||value.transfers.empty()||value.transfers.size()>limits.max_transfers||
      limits.max_bytes<80U||value.transfers.size()>(limits.max_bytes-80U)/256U)
    fail("Frame color transfer sequence exceeds its timing/count limits");
}
std::vector<std::byte> encode_frame_color_transfer_sequence_v1(const FrameColorTransferSequenceV1& value,
    FrameColorTransferSequenceLimitsV1 limits) {
  validate_frame_color_transfer_sequence_v1(value,limits);std::vector<std::byte> body;
  put(body,value.updates_per_second);put(body,value.lead_updates);put(body,value.tail_updates);put(body,0U);
  for(const auto& table:value.transfers)body.insert(body.end(),table.begin(),table.end());
  std::vector<std::byte> out(transfer_magic.begin(),transfer_magic.end());put(out,1U);put(out,64U);put(out,64U+body.size(),8U);
  put(out,value.transfers.size());put(out,0U);const auto hash=prepared_content_sha256_v1(body);
  out.insert(out.end(),hash.begin(),hash.end());out.insert(out.end(),body.begin(),body.end());return out;
}
FrameColorTransferSequenceV1 decode_frame_color_transfer_sequence_v1(std::span<const std::byte> bytes,
    FrameColorTransferSequenceLimitsV1 limits) {
  if(bytes.size()<80U||bytes.size()>limits.max_bytes)fail("Frame color transfer envelope exceeds limits");
  Reader in{bytes};const auto signature=in.take(8U);
  if(!std::equal(signature.begin(),signature.end(),transfer_magic.begin())||in.get()!=1U||in.get()!=64U||
      in.get(8U)!=bytes.size())fail("Invalid frame color transfer header");
  const auto count=in.word();
  if(!count||count>limits.max_transfers||80U+std::uint64_t(count)*256U!=bytes.size()||in.get()!=0U)
    fail("Frame color transfer count does not partition its payload");
  const auto stored=in.take(32U);const auto hash=prepared_content_sha256_v1(bytes.subspan(64U));
  if(!std::equal(stored.begin(),stored.end(),hash.begin()))fail("Frame color transfer digest mismatch");
  FrameColorTransferSequenceV1 out;out.updates_per_second=in.word();out.lead_updates=in.word();out.tail_updates=in.word();
  if(in.word())fail("Invalid frame color transfer flags");
  out.transfers.resize(count);
  for(auto& table:out.transfers){const auto data=in.take(256U);std::copy(data.begin(),data.end(),table.begin());}
  validate_frame_color_transfer_sequence_v1(out,limits);return out;
}
} // namespace openrc
