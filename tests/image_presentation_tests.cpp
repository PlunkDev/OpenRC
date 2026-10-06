#include "openrc/image_presentation.hpp"
#include <iostream>

using namespace openrc;
namespace {
void expect(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
template<class F> void reject(F f) { try{f();}catch(const ImagePresentationError&){return;}throw std::runtime_error("Invalid image presentation accepted"); }
ImagePresentationV1 fixture() {
  ImagePresentationV1 image;image.width=2;image.height=1;
  image.display_aspect_numerator=2;image.display_aspect_denominator=1;
  image.updates_per_second=50;image.initialization_clock_hz=1000;
  image.initialization_clock_modulus=4096;image.initialization_credit_divisor=20;
  image.minimum_initialization_updates=50;
  image.rgba={std::byte{200},std::byte{80},std::byte{10},std::byte{255},std::byte{100},std::byte{40},std::byte{5},std::byte{255}};
  image.color_transfers.resize(1);for(unsigned i=0;i<256;++i) image.color_transfers[0][i]=static_cast<std::byte>(i/2);
  return image;
}
}
int main() try {
  auto image=fixture();auto bytes=encode_image_presentation_v1(image);
  expect(decode_image_presentation_v1(bytes)==image,"Image roundtrip changed pixels or timing");
  expect(image_initialization_remaining_updates_v1(image,19'999'999)==50,"Initialization clock rounded early");
  expect(image_initialization_remaining_updates_v1(image,20'000'000)==49,"Initialization credit boundary failed");
  expect(image_initialization_remaining_updates_v1(image,1'000'000'000)==0,"Completed initialization still waits");
  expect(image_initialization_remaining_updates_v1(image,4'096'000'000)==50,"Initialization clock did not wrap");
  expect(image_initialization_remaining_updates_v1(image,UINT64_MAX)<=50,"Initialization clock overflowed");
  auto pixels=image.rgba;apply_image_color_transfer_v1(pixels,image.color_transfers[0]);
  expect(pixels[0]==std::byte{100}&&pixels[6]==std::byte{2}&&pixels[7]==std::byte{255},"Feedback transfer changed alpha or rounded RGB incorrectly");
  reject([&]{apply_image_color_transfer_v1(std::span(pixels).first(3),image.color_transfers[0]);});
  auto bad=image;bad.rgba[3]=std::byte{128};reject([&]{validate_image_presentation_v1(bad);});
  bad=image;bad.width=UINT32_MAX;reject([&]{validate_image_presentation_v1(bad);});
  bad=image;bad.initialization_credit_divisor=0;reject([&]{(void)image_initialization_remaining_updates_v1(bad,0);});
  ImagePresentationLimitsV1 limits;limits.max_transfers=0;reject([&]{(void)decode_image_presentation_v1(bytes,limits);});
  bytes.back()^=std::byte{1};reject([&]{(void)decode_image_presentation_v1(bytes);});
  for(unsigned length:{0,63,111}) reject([&]{(void)decode_image_presentation_v1(std::span(bytes).first(length));});
  FrameColorTransferSequenceV1 sequence{50U,1U,1U,{image.color_transfers[0],image.color_transfers[0]}};
  auto sequence_bytes=encode_frame_color_transfer_sequence_v1(sequence);
  expect(decode_frame_color_transfer_sequence_v1(sequence_bytes)==sequence,"Feedback sequence lost clock or ordered tables");
  pixels=image.rgba;pixels[0]=std::byte{11};
  for(const auto& table:sequence.transfers)apply_image_color_transfer_v1(pixels,table);
  expect(pixels[0]==std::byte{2},"Feedback sequence did not consume the previous quantized result");
  auto bad_sequence=sequence;bad_sequence.transfers.clear();reject([&]{validate_frame_color_transfer_sequence_v1(bad_sequence);});
  bad_sequence=sequence;bad_sequence.updates_per_second=0;reject([&]{validate_frame_color_transfer_sequence_v1(bad_sequence);});
  FrameColorTransferSequenceLimitsV1 transfer_limits;transfer_limits.max_transfers=1U;
  reject([&]{(void)decode_frame_color_transfer_sequence_v1(sequence_bytes,transfer_limits);});
  transfer_limits={};transfer_limits.max_bytes=79U;
  reject([&]{(void)decode_frame_color_transfer_sequence_v1(sequence_bytes,transfer_limits);});
  for(std::size_t size=0;size<sequence_bytes.size();++size)
    reject([&]{(void)decode_frame_color_transfer_sequence_v1(std::span(sequence_bytes).first(size));});
  sequence_bytes.back()^=std::byte{1};reject([&]{(void)decode_frame_color_transfer_sequence_v1(sequence_bytes);});
  std::cout<<"Image presentation pixels, feedback and initialization clock tests passed\n";return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
